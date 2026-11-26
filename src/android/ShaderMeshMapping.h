#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

namespace modmenu::android::shaderworld::mesh {
using State=std::vector<std::pair<std::string,std::string>>;
struct BlockRule { std::string name;State states;int id=0; };
inline std::string trim(std::string s){
    auto first=s.find_first_not_of(" \t\r");
    if(first==std::string::npos)return {};
    return s.substr(first,s.find_last_not_of(" \t\r")-first+1);
}
inline bool number(std::string const& s,int& n){
    auto r=std::from_chars(s.data(),s.data()+s.size(),n);
    return r.ec==std::errc{}&&r.ptr==s.data()+s.size();
}
// Supports the version condition and continuation syntax used by BSL v10.
// Unsupported preprocessor expressions are rejected, not silently ignored.
inline bool parseBlockRules(std::string const& text,std::vector<BlockRule>& out,std::string& error,int version=12100){
    std::vector<BlockRule> rules;
    struct Branch { bool parent,taken,seenElse; };
    std::vector<Branch> branches;
    bool active=true;
    std::string logical,line;
    auto fail=[&](std::string message){error=std::move(message);return false;};
    auto condition=[&](std::string const& expr,bool& yes){
        std::istringstream in(expr);std::string macro,op,value,extra;int n=0;
        if(!(in>>macro>>op>>value)||in>>extra||macro!="MC_VERSION"||!number(value,n))return false;
        if(op==">=")yes=version>=n;else if(op==">")yes=version>n;
        else if(op=="<=")yes=version<=n;else if(op=="<")yes=version<n;
        else if(op=="==")yes=version==n;else if(op=="!=")yes=version!=n;else return false;
        return true;
    };
    std::istringstream input(text);
    while(std::getline(input,line)){
        line=trim(line);
        if(line.empty())continue;
        if(line[0]=='#'){
            if(!logical.empty())return fail("Directive inside block mapping continuation");
            bool yes=false;
            if(line.rfind("#if ",0)==0){
                if(branches.size()>=16||!condition(line.substr(4),yes))return fail("Unsupported block mapping condition");
                branches.push_back({active,yes,false});active=active&&yes;
            }else if(line.rfind("#elif ",0)==0){
                if(branches.empty()||branches.back().seenElse||!condition(line.substr(6),yes))return fail("Invalid block mapping elif");
                auto& b=branches.back();active=b.parent&&!b.taken&&yes;b.taken|=yes;
            }else if(line=="#else"){
                if(branches.empty()||branches.back().seenElse)return fail("Invalid block mapping else");
                auto& b=branches.back();active=b.parent&&!b.taken;b.taken=b.seenElse=true;
            }else if(line=="#endif"){
                if(branches.empty())return fail("Unexpected block mapping endif");
                active=branches.back().parent;branches.pop_back();
            }else if(line.rfind("#ifdef",0)==0||line.rfind("#ifndef",0)==0||line.rfind("#define",0)==0)
                return fail("Unsupported block mapping macro");
            continue;
        }
        if(!active)continue;
        bool continued=line.back()=='\\';
        if(continued)line.pop_back();
        logical+=line+' ';
        if(logical.size()>256*1024)return fail("Oversized block mapping line");
        if(continued)continue;
        auto eq=logical.find('=');
        if(logical.rfind("block.",0)!=0||eq==std::string::npos)return fail("Unsupported block mapping entry");
        int id=0;
        if(!number(trim(logical.substr(6,eq-6)),id)||id<0||id>16777216)return fail("Invalid block mapping ID");
        std::istringstream tokens(logical.substr(eq+1));std::string token;
        while(tokens>>token){
            if(token[0]=='#')break;
            BlockRule rule;rule.id=id;
            auto first=token.find(':');
            if(first==std::string::npos){rule.name="minecraft:"+token;}
            else{
                auto second=token.find(':',first+1);
                if(token.substr(first+1).find('=')==std::string::npos)
                    rule.name=token;
                else if(second==std::string::npos||token.substr(first+1,second-first-1).find('=')!=std::string::npos){
                    rule.name="minecraft:"+token.substr(0,first);second=first;
                }else rule.name=token.substr(0,second);
                if(rule.name.empty())return fail("Empty block name");
                if(second!=std::string::npos&&second<token.size()&&rule.name!=token){
                    size_t at=second+1;
                    while(at<token.size()){
                        auto end=token.find(':',at);auto part=token.substr(at,end-at);
                        auto split=part.find('=');
                        if(split==std::string::npos||split==0||split+1==part.size()||part.find(',')!=std::string::npos)
                            return fail("Unsupported block state selector");
                        rule.states.emplace_back(part.substr(0,split),part.substr(split+1));
                        if(end==std::string::npos)break;at=end+1;
                    }
                }
            }
            if(rule.name.size()>256||rules.size()>=32768)return fail("Block mapping limit exceeded");
            rules.push_back(std::move(rule));
        }
        logical.clear();
    }
    if(!logical.empty()||!branches.empty())return fail("Unterminated block mapping");
    out=std::move(rules);return true;
}
// State-specific mappings outrank generic mappings. Conflicting equally
// specific matches are reported rather than choosing an arbitrary material.
inline bool blockId(std::vector<BlockRule> const& rules,std::string const& canonicalName,State const& states,int& id){
    id=0;int best=-1;bool ambiguous=false;
    for(auto const& r:rules){
        if(r.name!=canonicalName)continue;
        bool match=true;
        for(auto const& state:r.states)if(std::find(states.begin(),states.end(),state)==states.end()){match=false;break;}
        if(!match)continue;
        int n=int(r.states.size());
        if(n>best){best=n;id=r.id;ambiguous=false;}
        else if(n==best&&r.id!=id)ambiguous=true;
    }
    return best>=0&&!ambiguous;
}
using V2=std::array<float,2>;using V3=std::array<float,3>;using V4=std::array<float,4>;
struct NativeVertex { V3 position;V4 color;V2 packedUV;float packedLight; };
struct JavaVertex {
    V4 position,color;V3 normal;V4 uv0,uv1,entity,midUV,tangent;V3 midBlock;
};
inline V3 sub(V3 a,V3 b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
inline V3 mul(V3 a,float n){return {a[0]*n,a[1]*n,a[2]*n};}
inline float dot(V3 a,V3 b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline V3 cross(V3 a,V3 b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline bool unit(V3& v){float n=std::sqrt(dot(v,v));if(!std::isfinite(n)||n<1e-8f)return false;v=mul(v,1/n);return true;}
inline float fract(float x){return x-std::floor(x);}
inline float atlasUV(float x){float a=2*x;return fract(a)+(std::floor(a)-0.5f)/16384.0f;}
// Only the explicitly identified packed RenderChunk quad layout from the
// supplied Custom Buffers example. Caller verifies vertex ordering, block
// center and canonical Java state; arbitrary models need a separate adapter.
inline bool translateQuad(std::array<NativeVertex,4> const& vertices,V3 center,int id,
                          std::array<JavaVertex,4>& output,std::string& error){
    if(id<0||id>16777216){error="Invalid Java block ID";return false;}
    std::array<V2,4> uv{};V2 mid{};
    for(unsigned i=0;i<4;++i){
        auto const& v=vertices[i];
        for(float p:v.position)if(!std::isfinite(p)){error="Nonfinite position";return false;}
        for(float p:v.color)if(!std::isfinite(p)){error="Nonfinite color";return false;}
        if(!std::isfinite(v.packedUV[0])||!std::isfinite(v.packedUV[1])||!std::isfinite(v.packedLight)){
            error="Nonfinite packed UV";return false;
        }
        uv[i]={atlasUV(v.packedUV[0]),atlasUV(v.packedUV[1])};
        for(unsigned k=0;k<2;++k)mid[k]+=uv[i][k]*0.25f;
    }
    V3 e1=sub(vertices[1].position,vertices[0].position),e2=sub(vertices[2].position,vertices[0].position);
    V3 normal=cross(e1,e2);if(!unit(normal)){error="Degenerate face";return false;}
    V3 fourth=sub(vertices[3].position,vertices[0].position);
    if(std::abs(dot(normal,fourth))>0.0001f){error="Nonplanar face";return false;}
    float du1=uv[1][0]-uv[0][0],dv1=uv[1][1]-uv[0][1],du2=uv[2][0]-uv[0][0],dv2=uv[2][1]-uv[0][1];
    float det=du1*dv2-du2*dv1;
    if(!std::isfinite(det)||std::abs(det)<1e-12f){error="Degenerate face UV";return false;}
    V3 tangent=mul(sub(mul(e1,dv2),mul(e2,dv1)),1/det);
    V3 bitangent=mul(sub(mul(e2,du1),mul(e1,du2)),1/det);
    tangent=sub(tangent,mul(normal,dot(normal,tangent)));
    if(!unit(tangent)){error="Degenerate tangent";return false;}
    float handedness=dot(cross(normal,tangent),bitangent)<0?-1.0f:1.0f;
    std::array<JavaVertex,4> result{};
    for(unsigned i=0;i<4;++i){
        auto const& v=vertices[i];auto& j=result[i];
        j.position={v.position[0],v.position[1],v.position[2],1};j.color=v.color;j.normal=normal;
        j.uv0={uv[i][0],uv[i][1],0,1};
        // Native normalized light values are converted to the centers of
        // Java's 16 light levels, as expected by BSL's lightmap remap.
        float block=fract(v.packedLight*256),sky=fract(v.packedLight*4096);
        j.uv1={(std::round(block*15)+0.5f)/16,(std::round(sky*15)+0.5f)/16,0,1};
        j.entity={float(id),0,0,1};j.midUV={mid[0],mid[1],0,1};
        j.tangent={tangent[0],tangent[1],tangent[2],handedness};
        j.midBlock=mul(sub(center,v.position),64);
        for(float p:j.midBlock)if(!std::isfinite(p)){error="Nonfinite block center";return false;}
    }
    output=result;return true;
}
}
