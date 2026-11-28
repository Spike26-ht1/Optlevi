#pragma once
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace modmenu::android::shaderworld::profiles {
using Settings=std::map<std::string,std::string>;
using Definitions=std::map<std::string,std::vector<std::string>>;
inline bool ident(char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_';}
inline bool digit(char c){return c>='0'&&c<='9';}
inline bool name(std::string const& s){return !s.empty()&&s.size()<=96&&ident(s.front())&&
    std::all_of(s.begin(),s.end(),[](char c){return ident(c)||digit(c);});}
inline std::string trim(std::string const& s){
    auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos)return {};
    return s.substr(a,s.find_last_not_of(" \t\r")-a+1);
}
inline bool numeric(std::string const& s){
    if(s.empty()||s.size()>32||!std::all_of(s.begin(),s.end(),[](char c){
        return digit(c)||c=='.'||c=='-'||c=='+'||c=='e'||c=='E';}))return false;
    char* end=nullptr;auto x=std::strtod(s.c_str(),&end);
    return end==s.c_str()+s.size()&&end!=s.c_str()&&std::isfinite(x);
}
inline bool read(std::string const& text,Definitions& out,std::string& error){
    Definitions result;std::istringstream in(text);std::string line,logical;
    while(std::getline(in,line)){
        line=trim(line);if(line.empty()||line.front()=='#')continue;
        bool more=line.back()=='\\';if(more)line.pop_back();logical+=line+" ";
        if(logical.size()>65536){error="Profile line exceeds limit";return false;}
        if(more)continue;
        auto entry=trim(logical);logical.clear();
        if(entry.rfind("profile.",0)!=0)continue;
        auto eq=entry.find('=');auto key=trim(entry.substr(8,eq==std::string::npos?eq:eq-8));
        if(eq==std::string::npos||!name(key)||result.count(key)||result.size()>=64){error="Invalid profile declaration";return false;}
        std::istringstream tokens(entry.substr(eq+1));std::string token;
        auto& values=result[key];
        while(tokens>>token){
            if(values.size()>=256){error="Profile option limit exceeded";return false;}
            values.push_back(std::move(token));
        }
    }
    if(!logical.empty()){error="Unterminated profile property";return false;}
    out=std::move(result);return true;
}
inline bool resolve(Definitions const& defs,std::string const& selected,Settings& out,std::string& error){
    Settings result;std::set<std::string> stack;unsigned budget=4096;
    auto expand=[&](auto&& self,std::string const& key)->bool{
        auto it=defs.find(key);
        if(it==defs.end()||stack.size()>=32||!stack.insert(key).second){error="Unknown/cyclic profile "+key;return false;}
        for(auto const& token:it->second){
            if(!budget--){error="Profile expansion limit";return false;}
            if(token.rfind("profile.",0)==0){if(!self(self,token.substr(8)))return false;continue;}
            auto eq=token.find('=');bool off=!token.empty()&&token.front()=='!';
            auto option=off?token.substr(1):token.substr(0,eq);
            auto value=off?"false":eq==std::string::npos?"true":token.substr(eq+1);
            if(!name(option)||(off&&eq!=std::string::npos)||
                (value!="true"&&value!="false"&&!numeric(value))){
                error="Unsupported profile option "+token;return false;
            }
            result[option]=value;
            if(result.size()>256){error="Profile settings limit";return false;}
        }
        stack.erase(key);return true;
    };
    if(!selected.empty()&&!expand(expand,selected))return false;
    out=std::move(result);return true;
}
// Rewrite option declarations in expanded sources, not references or arbitrary
// code. Numeric changes require an explicit allowed-value list in the pack.
inline bool apply(std::string& source,Settings const& settings,std::set<std::string>& applied,std::string& error){
    if(settings.empty())return true;
    std::istringstream in(source);std::string line,result;bool block=false;
    while(std::getline(in,line)){
        auto code=trim(line);bool startedInBlock=block;
        for(size_t i=0;i+1<line.size();++i){
            if(block){if(line.compare(i,2,"*/")==0){block=false;++i;}}
            else if(line.compare(i,2,"//")==0)break;
            else if(line.compare(i,2,"/*")==0){block=true;++i;}
        }
        if(startedInBlock||block){result+=line+'\n';continue;}
        bool disabled=code.rfind("//",0)==0;
        if(disabled)code=trim(code.substr(2));
        auto comment=code.find("//");
        std::string annotation=comment==std::string::npos?"":code.substr(comment+2);
        auto statement=trim(code.substr(0,comment));
        std::string key,value,type;
        bool macro=statement.rfind("#define ",0)==0;
        bool constant=statement.rfind("const ",0)==0&&!disabled;
        if(macro){
            std::istringstream words(statement.substr(8));words>>key;std::getline(words,value);value=trim(value);
        }else if(constant){
            auto eq=statement.find('=');std::string extra;
            if(eq!=std::string::npos&&statement.back()==';'){
                std::istringstream words(statement.substr(6,eq-6));words>>type>>key;
                if(words>>extra||!(type=="int"||type=="float"||type=="bool"))key.clear();
                value=trim(statement.substr(eq+1,statement.size()-eq-2));
            }
        }
        auto chosen=settings.find(key);
        if(chosen==settings.end()){result+=line+'\n';continue;}
        auto const& desired=chosen->second;
        bool boolean=desired=="true"||desired=="false";
        if(macro&&value.empty()&&boolean){
            result+=(desired=="true"?"#define ":"// #define ")+key+" //"+annotation+'\n';
        }else if((macro&&!disabled&&numeric(value)&&!boolean)||
                 (constant&&numeric(value)&&!boolean)||
                 (constant&&type=="bool"&&boolean&&(value=="true"||value=="false"))){
            if(!boolean){
                auto a=annotation.find('['),b=annotation.find(']');
                if(a==std::string::npos||b==std::string::npos||b<=a){
                    error="Profile option lacks allowed values: "+key;return false;
                }
                std::istringstream allowed(annotation.substr(a+1,b-a-1));std::string candidate;bool match=false;
                while(allowed>>candidate)if(candidate==desired)match=true;
                if(!match){error="Profile value is not allowed: "+key;return false;}
            }
            if(macro)result+="#define "+key+" "+desired+" //"+annotation+'\n';
            else result+="const "+type+" "+key+" = "+desired+"; //"+annotation+'\n';
        }else{error="Unsupported declaration for profile option "+key;return false;}
        applied.insert(key);
    }
    source=std::move(result);return true;
}
}
