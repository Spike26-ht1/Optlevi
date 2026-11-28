#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace modmenu::android::shaderworld::custom {
using Number=std::optional<double>;
using Lookup=std::function<Number(std::string const&)>;
struct Node { std::string op; double number=0;std::vector<unsigned> args; };
struct Rule { std::string name;bool uniform=false;std::vector<Node> nodes;unsigned root=0; };
inline bool ident(char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_';}
inline bool digit(char c){return c>='0'&&c<='9';}
inline std::string trim(std::string const& s){
    auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos)return {};
    return s.substr(a,s.find_last_not_of(" \t\r")-a+1);
}
struct Parser {
    std::string const& s;std::vector<Node>& nodes;
    size_t pos=0;bool ok=true;
    void ws(){while(pos<s.size()&&(s[pos]==' '||s[pos]=='\t'||s[pos]=='\r'))++pos;}
    bool take(std::string const& token){ws();if(s.compare(pos,token.size(),token))return false;pos+=token.size();return true;}
    unsigned add(std::string op,std::vector<unsigned> args={},double n=0){
        if(nodes.size()>=512){ok=false;return 0;}
        nodes.push_back({std::move(op),n,std::move(args)});return unsigned(nodes.size()-1);
    }
    unsigned primary(unsigned depth){
        if(depth>48){ok=false;return 0;}
        ws();
        if(take("(")){auto n=expr(0,depth+1);if(!take(")"))ok=false;return n;}
        if(take("-"))return add("neg",{primary(depth+1)});
        if(take("+"))return primary(depth+1);
        if(take("!"))return add("not",{primary(depth+1)});
        if(pos>=s.size()){ok=false;return 0;}
        if(digit(s[pos])||s[pos]=='.'){
            auto start=pos;
            while(pos<s.size()&&digit(s[pos]))++pos;
            if(pos<s.size()&&s[pos]=='.'){++pos;while(pos<s.size()&&digit(s[pos]))++pos;}
            if(pos<s.size()&&(s[pos]=='e'||s[pos]=='E')){
                ++pos;if(pos<s.size()&&(s[pos]=='+'||s[pos]=='-'))++pos;
                while(pos<s.size()&&digit(s[pos]))++pos;
            }
            auto tok=s.substr(start,pos-start);char* end=nullptr;double n=std::strtod(tok.c_str(),&end);
            if(end!=tok.c_str()+tok.size()||end==tok.c_str()||!std::isfinite(n)){ok=false;return 0;}
            return add("#",{},n);
        }
        if(!ident(s[pos])){ok=false;return 0;}
        auto start=pos++;
        while(pos<s.size()&&(ident(s[pos])||digit(s[pos])||s[pos]=='.'))++pos;
        auto name=s.substr(start,pos-start);
        if(!take("("))return add("$"+name);
        std::vector<unsigned> args;
        if(!take(")")){
            do{args.push_back(expr(0,depth+1));if(!ok||args.size()>64){ok=false;return 0;}}while(take(","));
            if(!take(")"))ok=false;
        }
        static const std::map<std::string,std::pair<unsigned,unsigned>> arity={
            {"if",{3,3}},{"in",{2,64}},{"frac",{1,1}},{"abs",{1,1}},
            {"sin",{1,1}},{"cos",{1,1}},{"min",{2,2}},{"max",{2,2}},
            {"clamp",{3,3}},{"smooth",{4,4}},{"sqrt",{1,1}},{"floor",{1,1}}
        };
        auto a=arity.find(name);
        if(a==arity.end()||args.size()<a->second.first||args.size()>a->second.second)ok=false;
        return add(std::move(name),std::move(args));
    }
    unsigned expr(int prec,unsigned depth){
        if(depth>48){ok=false;return 0;}
        unsigned lhs=primary(depth+1);
        static const std::vector<std::pair<std::string,int>> ops={
            {"||",1},{"&&",2},{"==",3},{"!=",3},{"<=",4},{">=",4},
            {"<",4},{">",4},{"+",5},{"-",5},{"*",6},{"/",6},{"%",6}};
        while(ok){
            ws();bool found=false;
            for(auto const& op:ops)if(s.compare(pos,op.first.size(),op.first)==0){
                if(op.second<prec)return lhs;
                pos+=op.first.size();auto rhs=expr(op.second+1,depth+1);
                lhs=add(op.first,{lhs,rhs});found=true;break;
            }
            if(!found)break;
        }
        return lhs;
    }
};
inline bool parseExpression(std::string const& value,Rule& rule){
    if(value.empty()||value.size()>16384)return false;
    Parser p{value,rule.nodes};rule.root=p.expr(0,0);p.ws();
    return p.ok&&p.pos==value.size()&&!rule.nodes.empty();
}
// Only MC_VERSION branches are understood for calculated uniform declarations.
// Unknown conditional blocks elsewhere (e.g. image settings) are not evaluated.
inline bool parse(std::string const& properties,std::vector<Rule>& out,std::string& error,int version=12100){
    struct Branch {bool parent,active,taken,known,elseSeen;};
    std::vector<Branch> branches;std::vector<Rule> rules;std::set<std::string> names;
    std::istringstream input(properties);std::string line,logical;unsigned lineNo=0;
    auto fail=[&](std::string const& message){error="shaders.properties line "+std::to_string(lineNo)+": "+message;return false;};
    auto active=[&](){return branches.empty()||branches.back().active;};
    auto condition=[&](std::string const& text,bool& yes){
        std::istringstream in(text);std::string key,op,extra;int n=0;
        if(!(in>>key>>op>>n)||in>>extra||key!="MC_VERSION")return false;
        if(op==">=")yes=version>=n;else if(op==">")yes=version>n;
        else if(op=="<=")yes=version<=n;else if(op=="<")yes=version<n;
        else if(op=="==")yes=version==n;else if(op=="!=")yes=version!=n;else return false;
        return true;
    };
    while(std::getline(input,line)){
        ++lineNo;line=trim(line);if(line.empty())continue;
        if(line[0]=='#'){
            if(!logical.empty())return fail("directive inside continuation");
            if(line.rfind("#if ",0)==0||line.rfind("#ifdef ",0)==0||line.rfind("#ifndef ",0)==0){
                bool yes=false,known=line.rfind("#if ",0)==0&&condition(line.substr(4),yes);
                if(branches.size()>=32)return fail("conditional nesting limit");
                bool parent=active();branches.push_back({parent,parent&&known&&yes,yes,known,false});
            }else if(line.rfind("#elif ",0)==0){
                if(branches.empty()||branches.back().elseSeen)return fail("unexpected elif");
                auto& b=branches.back();bool yes=false;b.known=b.known&&condition(line.substr(6),yes);
                b.active=b.parent&&b.known&&!b.taken&&yes;b.taken|=yes;
            }else if(line=="#else"){
                if(branches.empty()||branches.back().elseSeen)return fail("unexpected else");
                auto& b=branches.back();b.active=b.parent&&b.known&&!b.taken;b.taken=b.elseSeen=true;
            }else if(line=="#endif"){
                if(branches.empty())return fail("unexpected endif");branches.pop_back();
            }
            continue;
        }
        bool continued=line.back()=='\\';if(continued)line.pop_back();
        logical+=line+" ";if(logical.size()>65536)return fail("property line too long");
        if(continued)continue;
        auto entry=trim(logical);logical.clear();
        bool uniform=entry.rfind("uniform.",0)==0,variable=entry.rfind("variable.",0)==0;
        if(!uniform&&!variable)continue;
        if(std::any_of(branches.begin(),branches.end(),[](auto const& b){return b.parent&&!b.known;}))
            return fail("unsupported conditional custom uniform");
        if(!active())continue;
        auto eq=entry.find('=');auto key=trim(entry.substr(0,eq));
        auto prefix=uniform?std::string("uniform.float."):std::string("variable.float.");
        if(eq==std::string::npos||key.rfind(prefix,0)!=0)return fail("only scalar float custom uniforms are implemented");
        Rule rule;rule.uniform=uniform;rule.name=key.substr(prefix.size());
        if(rule.name.empty()||rule.name.size()>96||!ident(rule.name[0])||
           !std::all_of(rule.name.begin(),rule.name.end(),[](char c){return ident(c)||digit(c);})||
           !names.insert(rule.name).second||rules.size()>=256)return fail("invalid/duplicate custom uniform "+rule.name);
        if(!parseExpression(trim(entry.substr(eq+1)),rule))return fail("unsupported expression for "+rule.name);
        rules.push_back(std::move(rule));
    }
    if(!logical.empty()||!branches.empty())return fail("unterminated property");
    out=std::move(rules);return true;
}
using Smooth=std::map<int,double>;
struct Evaluation {
    std::vector<Rule> const& rules;Lookup input;Smooth const& previous;double ticks;
    Smooth next;std::map<std::string,Number> cache;std::set<std::string> busy;
    unsigned budget=200000;
    Number get(std::string const& name){
        auto saved=cache.find(name);if(saved!=cache.end())return saved->second;
        auto r=std::find_if(rules.begin(),rules.end(),[&](auto const& x){return x.name==name;});
        if(r==rules.end())return input(name);
        if(busy.size()>=48||!busy.insert(name).second)return {};
        auto value=eval(*r,r->root,0);busy.erase(name);cache[name]=value;return value;
    }
    Number eval(Rule const& r,unsigned index,unsigned depth){
        if(!budget||depth>64||index>=r.nodes.size())return {};--budget;
        auto const& n=r.nodes[index];auto const& op=n.op;
        if(op=="#")return n.number;
        if(op[0]=='$')return get(op.substr(1));
        auto arg=[&](unsigned i){return eval(r,n.args[i],depth+1);};
        auto a=arg(0);if(!a)return {};
        if(op=="if")return arg(*a!=0?1:2);
        if(op=="&&"&&*a==0)return 0;
        if(op=="||"&&*a!=0)return 1;
        if(op=="in"){
            bool unknown=false;
            for(unsigned i=1;i<n.args.size();++i){auto x=arg(i);if(!x)unknown=true;else if(*x==*a)return 1;}
            return unknown?Number{}:Number{0};
        }
        std::vector<double> v{*a};
        for(unsigned i=1;i<n.args.size();++i){auto x=arg(i);if(!x)return {};v.push_back(*x);}
        double x=v[0],y=v.size()>1?v[1]:0,result=0;
        if(op=="neg")result=-x;else if(op=="not")result=x==0;
        else if(op=="+")result=x+y;else if(op=="-")result=x-y;else if(op=="*")result=x*y;
        else if(op=="/"){if(y==0)return {};result=x/y;}
        else if(op=="%"){if(y==0)return {};result=std::fmod(x,y);}
        else if(op=="<")result=x<y;else if(op==">")result=x>y;else if(op=="<=")result=x<=y;
        else if(op==">=")result=x>=y;else if(op=="==")result=x==y;else if(op=="!=")result=x!=y;
        else if(op=="&&")result=x!=0&&y!=0;else if(op=="||")result=x!=0||y!=0;
        else if(op=="frac")result=x-std::floor(x);else if(op=="floor")result=std::floor(x);
        else if(op=="abs")result=std::abs(x);else if(op=="sqrt")result=std::sqrt(x);
        else if(op=="sin")result=std::sin(x);else if(op=="cos")result=std::cos(x);
        else if(op=="min")result=std::min(x,y);else if(op=="max")result=std::max(x,y);
        else if(op=="clamp"){if(y>v[2])return {};result=std::clamp(x,y,v[2]);}
        else if(op=="smooth"){
            if(x<0||x>65535||std::floor(x)!=x||v[2]<0||v[3]<0)return {};
            int id=int(x);auto old=previous.find(id);result=y;
            if(old!=previous.end()){
                double half=y>=old->second?v[2]:v[3];
                if(half>0)result=y+(old->second-y)*std::exp2(-std::max(0.0,ticks)/half);
            }
            // Repeated evaluation within a frame reads previous-frame state only.
            auto prior=next.find(id);if(prior!=next.end()&&prior->second!=result)return {};
            if(next.size()>=256&&prior==next.end())return {};
            next[id]=result;
        }else return {};
        return std::isfinite(result)?Number{result}:Number{};
    }
};
}
