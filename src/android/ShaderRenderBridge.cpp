#include "ShaderRenderBridge.h"
#include "ShaderWorldRuntime.h"
#include "ShaderCameraMath.h"
#include "ShaderDiagnostics.h"
#include "ShaderHookTrace.h"
#include "ShaderNativeMeshSample.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pl/memory/Hook.hpp>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace modmenu::android::shaderbridge {
namespace {
using namespace shaderworld;
std::mutex reportMu;
std::string lastReport="Native draw bridge not initialized";
std::atomic<unsigned> hookCount{0};
std::atomic<unsigned> foreignDraws{0},outerDraws{0};
std::mutex installMu;
std::vector<void*> installedTargets;
void* glesLibrary=nullptr;
struct DeferredSampler { GLuint program; GLint location; unsigned index; };
struct Frame {
    EGLContext context=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE;
    bool inPresent=false;
    bool terrainCandidate=false;
    GLuint terrainFramebuffer=0;
    int terrainWidth=0,terrainHeight=0;
    std::vector<GLuint> terrainPrograms;
    std::string terrainEffectReport,programSignatures,drawEvidence;
    std::array<GLuint,128> seen{};
    unsigned seenCount=0,markerCount=0,captureCount=0,terrainCount=0;
    std::string details;
    HookTrace trace;
    // Saved across frames until EGL context/surface replacement.
    std::string firstTerrainLayout;
    std::string firstMeshSample;
    bool meshSampleAttempted=false;
    std::vector<DeferredSampler> deferredSamplers;
};
thread_local Frame frame;
thread_local unsigned drawNesting=0;

void note(std::string const& message){
    if(frame.details.size()+message.size()<3500){
        frame.details+=message;
        frame.details+='\n';
    }
}
int indexOf(std::string_view name,std::string_view prefix,unsigned max){
    if(!name.starts_with(prefix))return -1;
    auto suffix=name.substr(prefix.size());
    unsigned index=0;
    auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),index);
    if(parsed.ec!=std::errc{}||parsed.ptr!=suffix.data()+suffix.size()||index>=max)return -1;
    return int(index);
}
char const* formatName(GLint format){
    switch(format){
    case GL_RGBA8:return "RGBA8";
    case GL_RGB8:return "RGB8";
    case GL_R8:return "R8";
    case GL_RG8:return "RG8";
    case GL_RGB10_A2:return "RGB10_A2";
    case GL_R11F_G11F_B10F:return "R11F_G11F_B10F";
    case 0x805B:return "RGBA16";
    case GL_RGB16F:return "RGB16F";
    case GL_RGBA16F:return "RGBA16F";
    case GL_R16F:return "R16F";
    case GL_RG16F:return "RG16F";
    case GL_RGBA32F:return "RGBA32F";
    default:return nullptr;
    }
}
struct Resource {
    enum class Kind { SceneDepth,SceneColor,ShadowDepth,ShadowColor };
    Kind kind;
    unsigned index;
    GLint location;
    std::string name;
};
struct ObservedDraw {
    GLenum mode=0;
    GLsizei count=0,instances=1;
    bool indexed=false;
    GLenum indexType=0;
    std::uintptr_t indexOffset=0;
    GLint first=0;
    GLint baseVertex=0;
    unsigned variant=0; // 0 ordinary, 1 range, 2 base vertex, 3 instanced base vertex
};
// Describe the first observed draw for a linked program. This is not a
// semantic classification: it records only actual GLES state, with bounded
// logging and no mapping, replay or framebuffer writes.
void describeDraw(GLuint program,ObservedDraw const& call,bool terrainCandidate){
    if(frame.drawEvidence.size()>6500)return;
    GLint vao=0,ebo=0,fbo=0,samples=0,viewport[4]{},draw0=0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&ebo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
    glGetIntegerv(GL_SAMPLES,&samples);
    glGetIntegerv(GL_VIEWPORT,viewport);
    glGetIntegerv(GL_DRAW_BUFFER0,&draw0);
    GLboolean depthWrite=GL_FALSE;glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);
    std::string out="\nDraw program="+std::to_string(program)+
        (terrainCandidate?" terrain-candidate":" unclassified")+
        " mode="+std::to_string(call.mode)+" count="+std::to_string(call.count)+
        " instances="+std::to_string(call.instances)+
        " VAO="+std::to_string(vao)+" EBO="+std::to_string(ebo);
    if(call.indexed){
        out+=" indexType="+std::to_string(call.indexType);
        out+=ebo?" indexOffset="+std::to_string(call.indexOffset):" client-indices/unbound";
    }else out+=" first="+std::to_string(call.first);
    if(call.variant>=2)out+=" baseVertex="+std::to_string(call.baseVertex);
    out+=" variant="+std::to_string(call.variant);
    out+=" FBO="+std::to_string(fbo)+" viewport="+std::to_string(viewport[0])+","+
        std::to_string(viewport[1])+","+std::to_string(viewport[2])+","+std::to_string(viewport[3])+
        " samples="+std::to_string(samples)+" draw0="+std::to_string(draw0)+
        " depthTest="+std::to_string(glIsEnabled(GL_DEPTH_TEST))+
        " depthWrite="+std::to_string(depthWrite)+
        " blend="+std::to_string(glIsEnabled(GL_BLEND));
    // GLES 3.0 exposes attachment types, component bits and texture level.
    // A viewport is not an attachment extent. Do not invent dimensions or
    // use GLES 3.1 glGetTexLevelParameteriv on a GLES 3.0 driver.
    if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE){
        auto attachment=[&](char const* label,GLenum slot,GLenum bits){
            GLint type=0;
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,slot,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&type);
            out+=" "+std::string(label)+"Type="+std::to_string(type);
            if(type==GL_NONE)return;
            GLint size=0,component=0;
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,slot,bits,&size);
            out+=" bits="+std::to_string(size);
            if(slot!=GL_STENCIL_ATTACHMENT&&slot!=GL_STENCIL){
                glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,slot,
                    GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE,&component);
                out+=" component="+std::to_string(component);
            }
            if(fbo){
                GLint object=0;
                glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,slot,
                    GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&object);
                out+=" object="+std::to_string(object);
                if(type==GL_TEXTURE){
                    GLint level=0;
                    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,slot,
                        GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL,&level);
                    out+=" level="+std::to_string(level);
                }
            }
        };
        if(draw0!=GL_NONE)attachment("color",GLenum(draw0),GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE);
        attachment("depth",fbo?GL_DEPTH_ATTACHMENT:GL_DEPTH,GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE);
        attachment("stencil",fbo?GL_STENCIL_ATTACHMENT:GL_STENCIL,GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE);
    }else out+=" incomplete-FBO";
    GLint count=0;glGetProgramiv(program,GL_ACTIVE_ATTRIBUTES,&count);
    for(GLint i=0;i<count&&i<16&&out.size()<3000;++i){
        GLchar name[128]{};GLsizei len=0;GLint arraySize=0;GLenum shaderType=0;
        glGetActiveAttrib(program,GLuint(i),sizeof(name),&len,&arraySize,&shaderType,name);
        if(len<=0)continue;
        GLint location=glGetAttribLocation(program,name);if(location<0)continue;
        GLint enabled=0,size=0,type=0,normalized=0,stride=0,vbo=0,divisor=0,integer=0;
        void* offset=nullptr;
        glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);
        glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_SIZE,&size);
        glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_TYPE,&type);
        glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&normalized);
        glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_STRIDE,&stride);
        glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&vbo);
        glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_DIVISOR,&divisor);
        glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_INTEGER,&integer);
        glGetVertexAttribPointerv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_POINTER,&offset);
        out+="\n  "+std::string(name,size_t(len))+" loc="+std::to_string(location)+
            " shaderType="+std::to_string(shaderType)+" array="+std::to_string(arraySize)+
            " enabled="+std::to_string(enabled)+" components="+std::to_string(size)+
            " storageType="+std::to_string(type)+" normalized="+std::to_string(normalized)+
            " integer="+std::to_string(integer)+" stride="+std::to_string(stride)+
            " VBO="+std::to_string(vbo)+" divisor="+std::to_string(divisor);
        if(vbo)out+=" offset="+std::to_string(reinterpret_cast<std::uintptr_t>(offset));
    }
    if(frame.drawEvidence.size()+out.size()<=9000)frame.drawEvidence+=out;
}
// Read a small, bounded buffer-object range through a temporary staging
// buffer. Never map the game's live VBO/EBO, dereference client addresses,
// or retain a buffer name for replay. Called once per accepted EGL context.
bool sampleBuffer(GLuint source,std::uint64_t offset,std::size_t length,unsigned char* output){
    if(!source||!length||length>64)return false;
    GLint oldRead=0,oldWrite=0;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING,&oldRead);
    glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING,&oldWrite);
    GLuint staging=0;
    auto restore=[&](){
        glBindBuffer(GL_COPY_READ_BUFFER,GLuint(oldRead));
        glBindBuffer(GL_COPY_WRITE_BUFFER,GLuint(oldWrite));
        if(staging)glDeleteBuffers(1,&staging);
    };
    glBindBuffer(GL_COPY_READ_BUFFER,source);
    GLint64 size=0;GLint mapped=0;
    glGetBufferParameteri64v(GL_COPY_READ_BUFFER,GL_BUFFER_SIZE,&size);
    glGetBufferParameteriv(GL_COPY_READ_BUFFER,GL_BUFFER_MAPPED,&mapped);
    if(mapped||size<0||offset>std::uint64_t(size)||length>std::uint64_t(size)-offset){
        restore();return false;
    }
    glGenBuffers(1,&staging);
    if(!staging){restore();return false;}
    glBindBuffer(GL_COPY_WRITE_BUFFER,staging);
    glBufferData(GL_COPY_WRITE_BUFFER,GLsizeiptr(length),nullptr,GL_STREAM_READ);
    glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,GLintptr(offset),0,GLsizeiptr(length));
    auto ptr=static_cast<unsigned char const*>(glMapBufferRange(GL_COPY_WRITE_BUFFER,0,GLsizeiptr(length),GL_MAP_READ_BIT));
    bool ok=false;
    if(ptr){
        std::copy_n(ptr,length,output);
        ok=glUnmapBuffer(GL_COPY_WRITE_BUFFER)==GL_TRUE;
    }
    restore();
    // Existing native errors cannot be reliably separated from diagnostic
    // errors; no data is accepted if the staging operations failed.
    return ok&&glGetError()==GL_NO_ERROR;
}
void sampleNativeTerrain(GLuint program,ObservedDraw const& call){
    if(frame.meshSampleAttempted||!call.indexed||call.mode!=GL_TRIANGLES||
       call.count<3||call.instances!=1)return;
    GLint ebo=0;glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&ebo);
    unsigned indexBytes=call.indexType==GL_UNSIGNED_BYTE?1:call.indexType==GL_UNSIGNED_SHORT?2:
        call.indexType==GL_UNSIGNED_INT?4:0;
    if(!ebo||!indexBytes)return;
    struct Attr { char const* name; GLint location,buffer,size,type,stride,normalized,divisor,integer;std::uintptr_t offset; };
    std::array<Attr,4> attrs{{{"a_position"},{"a_color0"},{"a_texcoord0"},{"a_texcoord1"}}};
    for(auto& a:attrs){
        a.location=glGetAttribLocation(program,a.name);if(a.location<0)return;
        auto i=GLuint(a.location);GLint enabled=0;void* pointer=nullptr;
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&a.buffer);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_SIZE,&a.size);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_TYPE,&a.type);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&a.stride);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&a.normalized);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_DIVISOR,&a.divisor);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_INTEGER,&a.integer);
        glGetVertexAttribPointerv(i,GL_VERTEX_ATTRIB_ARRAY_POINTER,&pointer);
        a.offset=reinterpret_cast<std::uintptr_t>(pointer);
        if(!enabled||!a.buffer||a.divisor||a.integer||a.stride<=0)return;
    }
    // Only the exact reflected storage types reported by the device.
    // No fixed offsets, stride, VAO number or VBO identity.
    if(attrs[0].type!=GL_HALF_FLOAT||attrs[0].size!=4||attrs[0].normalized||
       attrs[1].type!=GL_UNSIGNED_BYTE||attrs[1].size!=4||!attrs[1].normalized)return;
    for(unsigned i=2;i<4;++i)if(attrs[i].type!=GL_UNSIGNED_SHORT||attrs[i].size!=2||!attrs[i].normalized)return;
    frame.meshSampleAttempted=true;
    unsigned char indices[12]{};
    if(!sampleBuffer(GLuint(ebo),call.indexOffset,3*indexBytes,indices)){
        frame.firstMeshSample="Native mesh sample: index staging read failed";return;
    }
    std::string info="Native mesh sample: first indexed triangle (raw attribute semantics, not mapped Java geometry)";
    for(unsigned i=0;i<3;++i){
        auto p=indices+i*indexBytes;
        std::uint32_t index=indexBytes==1?*p:indexBytes==2?meshsample::u16(p):meshsample::u32(p);
        auto vertex=std::int64_t(index)+call.baseVertex;
        info+="\n index="+std::to_string(index)+" effective="+std::to_string(vertex);
        for(unsigned j=0;j<attrs.size();++j){
            auto const& a=attrs[j];
            std::uint64_t start=0,bytes=j==0?8:4;
            unsigned char data[8]{};
            if(!meshsample::attributeRange(a.offset,a.stride,vertex,bytes,
                   std::uint64_t(std::numeric_limits<GLintptr>::max()),start)||
               !sampleBuffer(GLuint(a.buffer),start,bytes,data)){
                frame.firstMeshSample=info+"\n Attribute staging read failed: "+a.name;return;
            }
            info+=" "+std::string(a.name)+"=(";
            for(unsigned k=0;k<unsigned(a.size);++k){
                if(k)info+=",";
                float v=j==0?meshsample::half(meshsample::u16(data+k*2)):
                    j==1?float(data[k])/255.0f:float(meshsample::u16(data+k*2))/65535.0f;
                info+=std::to_string(v);
            }
            info+=")";
        }
    }
    frame.firstMeshSample=std::move(info);
}
void captureBeforeDraw(ObservedDraw const& call){
    if(frame.inPresent||frame.context==EGL_NO_CONTEXT||!captureRequested())return;
    if(eglGetCurrentContext()!=frame.context||eglGetCurrentSurface(EGL_DRAW)!=frame.surface)return;
    GLint program=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    if(program<=0||frame.seenCount>=frame.seen.size())return;
    auto end=frame.seen.begin()+frame.seenCount;
    if(std::find(frame.seen.begin(),end,GLuint(program))!=end)return;
    frame.seen[frame.seenCount++]=GLuint(program);

    GLint count=0;
    glGetProgramiv(GLuint(program),GL_ACTIVE_UNIFORMS,&count);
    if(count<0||count>512)return;
    std::vector<Resource> resources;
    std::vector<UniformMapping> uniforms;
    GLint viewProjection=-1,viewPosition=-1,view=-1,projection=-1,atlas=-1,lightmap=-1,mesh=-1;
    bool terrainAtlas=false;
    std::string signature;
    GLint deferredMarker=-1;
    for(GLint i=0;i<count;++i){
        GLchar raw[256]{};
        GLsizei length=0;GLint size=0;GLenum type=0;
        glGetActiveUniform(GLuint(program),GLuint(i),sizeof(raw),&length,&size,&type,raw);
        if(length<=0||size!=1)continue;
        std::string name(raw,size_t(length));
        if(signature.size()<280){if(!signature.empty())signature+=", ";signature+=name;}
        if(name=="u_viewProj"&&type==GL_FLOAT_MAT4)viewProjection=glGetUniformLocation(GLuint(program),raw);
        if(name=="ViewPositionAndTime"&&type==GL_FLOAT_VEC4)viewPosition=glGetUniformLocation(GLuint(program),raw);
        if(name=="u_view"&&type==GL_FLOAT_MAT4)view=glGetUniformLocation(GLuint(program),raw);
        if(name=="u_proj"&&type==GL_FLOAT_MAT4)projection=glGetUniformLocation(GLuint(program),raw);
        if(name=="s_MatTexture"&&type==GL_SAMPLER_2D)atlas=glGetUniformLocation(GLuint(program),raw);
        if(name=="s_LightMapTexture"&&type==GL_SAMPLER_2D)lightmap=glGetUniformLocation(GLuint(program),raw);
        if(name=="MeshContext")mesh=glGetUniformLocation(GLuint(program),raw);
        terrainAtlas=atlas>=0;
        if(name=="OptLeviDeferred"&&type==GL_INT_VEC2){deferredMarker=glGetUniformLocation(GLuint(program),raw);continue;}
        int deferredIndex=indexOf(name,"s_OptLeviDeferred",16);
        if(deferredIndex>=0&&type==GL_SAMPLER_2D){
            auto loc=glGetUniformLocation(GLuint(program),raw);
            if(loc>=0&&frame.deferredSamplers.size()<256)
                frame.deferredSamplers.push_back({GLuint(program),loc,unsigned(deferredIndex)});
            continue;
        }
        constexpr std::string_view prefix="OptLeviWorld_";
        if(name.starts_with(prefix)&&name.size()>prefix.size()){
            uniforms.push_back({name,name.substr(prefix.size()),type});continue;
        }
        auto add=[&](std::string_view key,unsigned max,Resource::Kind kind,GLenum wanted){
            int index=indexOf(name,key,max);
            if(index<0)return false;
            GLint location=glGetUniformLocation(GLuint(program),name.c_str());
            if(location>=0&&type==wanted)
                resources.push_back({kind,unsigned(index),location,name});
            else note("Wrong marker type: "+name);
            return true;
        };
        if(add("OptLeviDepth",3,Resource::Kind::SceneDepth,GL_INT_VEC3))continue;
        if(add("OptLeviColor",16,Resource::Kind::SceneColor,GL_INT_VEC4))continue;
        if(add("OptLeviShadowDepth",2,Resource::Kind::ShadowDepth,GL_INT_VEC3))continue;
        add("OptLeviShadowColor",2,Resource::Kind::ShadowColor,GL_INT_VEC4);
    }
    // Actual user-device signature: separate u_view/u_proj, atlas,
    // lightmap, MeshContext and both UV attributes. Do not match actors
    // or UI merely because they use s_MatTexture.
    const bool splitTerrain=terrainAtlas&&lightmap>=0&&mesh>=0&&view>=0&&projection>=0&&
        glGetAttribLocation(GLuint(program),"a_position")>=0&&
        glGetAttribLocation(GLuint(program),"a_texcoord0")>=0&&
        glGetAttribLocation(GLuint(program),"a_texcoord1")>=0&&
        glGetAttribLocation(GLuint(program),"a_color0")>=0;
    const bool terrainCandidate=splitTerrain||(terrainAtlas&&viewProjection>=0&&viewPosition>=0&&lightmap>=0);
    describeDraw(GLuint(program),call,terrainCandidate);
    if(splitTerrain){
        sampleNativeTerrain(GLuint(program),call);
        // Resolve sampler units from the linked program, never assume
        // texture unit numbers. Restore the native active texture.
        GLint active=GL_TEXTURE0,maxUnits=0;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&maxUnits);
        auto sampler=[&](char const* name,GLint location){
            GLint unit=-1,texture=0;
            glGetUniformiv(GLuint(program),location,&unit);
            if(unit>=0&&unit<maxUnits){
                glActiveTexture(GL_TEXTURE0+unit);
                glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
                note(std::string("Native terrain ")+name+" unit="+std::to_string(unit)+
                    " texture="+std::to_string(texture));
            }
        };
        sampler("atlas",atlas);sampler("lightmap",lightmap);
        glActiveTexture(GLenum(active));
    }
    if(frame.programSignatures.size()<1500){
        frame.programSignatures+="\nProgram "+std::to_string(program)+": "+signature;
    }
    // Record the real VAO layout of the first recognized terrain draw.
    // This is needed to build the future native mesh adapter without
    // hardcoding offsets for a different Minecraft build. It makes no draw.
    if(terrainCandidate&&frame.terrainPrograms.size()<128)
        frame.terrainPrograms.push_back(GLuint(program));
    if(terrainCandidate&&frame.firstTerrainLayout.empty()){
        GLint count=0,vao=0,elementBuffer=0;
        glGetProgramiv(GLuint(program),GL_ACTIVE_ATTRIBUTES,&count);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&elementBuffer);
        std::string info="Unverified terrain candidate VAO="+std::to_string(vao)+
            " elementBuffer="+std::to_string(elementBuffer);
        for(GLint i=0;i<count&&i<12&&info.size()<1800;++i){
            GLchar raw[128]{};GLsizei size=0;GLint elements=0;GLenum type=0;
            glGetActiveAttrib(GLuint(program),GLuint(i),sizeof(raw),&size,&elements,&type,raw);
            if(size<=0)continue;
            GLint location=glGetAttribLocation(GLuint(program),raw);
            if(location<0)continue;
            GLint enabled=0,components=0,componentType=0,normalized=0,stride=0,
                  buffer=0,divisor=0,integer=0;
            void* pointer=nullptr;
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_SIZE,&components);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_TYPE,&componentType);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&normalized);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_STRIDE,&stride);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&buffer);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_DIVISOR,&divisor);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_INTEGER,&integer);
            glGetVertexAttribPointerv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_POINTER,&pointer);
            info+="\n  "+std::string(raw,size_t(size))+" at "+std::to_string(location)+
                  " enabled="+std::to_string(enabled)+
                  " comps="+std::to_string(components)+
                  " type="+std::to_string(componentType)+
                  " normalized="+std::to_string(normalized)+
                  " stride="+std::to_string(stride)+
                  " VBO="+std::to_string(buffer)+
                  " divisor="+std::to_string(divisor)+
                  " integer="+std::to_string(integer)+
                  // In a VBO this is an offset, never dereference a client pointer.
                  (buffer?" offset="+std::to_string(reinterpret_cast<std::uintptr_t>(pointer)):" client-array/unbound");
        }
        if(glGetError()==GL_NO_ERROR)frame.firstTerrainLayout=std::move(info);
    }
    // Recover only the projection component of the observed terrain draw.
    // A terrain draw is not a phase barrier, and native world coordinates
    // must not be passed off as Java cameraPosition or model-view values.
    if(terrainCandidate){
        if(frame.terrainCount==0){
            camera::Projection p;
            bool valid=false;
            if(splitTerrain){
                camera::Matrix direct{};
                glGetUniformfv(GLuint(program),projection,direct.data());
                valid=camera::directProjection(direct,p);
                note("Native split view/projection signature observed; projection validation="+std::to_string(valid));
                if(!valid){
                    std::string values="Native u_proj:";
                    for(float v:direct)values+=" "+std::to_string(v);
                    note(values);
                }
            }else{
                camera::Matrix vp{};
                std::array<float,4> eye{};
                glGetUniformfv(GLuint(program),viewProjection,vp.data());
                glGetUniformfv(GLuint(program),viewPosition,eye.data());
                valid=camera::recoverProjection(vp,eye,p);
            }
            if(valid){
                std::vector<WorldUniform> values(2);
                values[0].name="gbufferProjection";values[0].glType=GL_FLOAT_MAT4;values[0].floats=p.matrix;
                values[1].name="gbufferProjectionInverse";values[1].glType=GL_FLOAT_MAT4;values[1].floats=p.inverse;
                std::string error;
                if(publishWorldUniforms(values,error)){
                    frame.captureCount+=2;
                    ++frame.terrainCount;
                    note("Native terrain perspective captured (projection and inverse), near="+
                        std::to_string(p.nearClip)+" far="+std::to_string(p.farClip));
                }else note("Native camera capture: "+error);
            }else note("Native terrain signature observed; projection validation failed");
        }
    }
    if(resources.empty()&&uniforms.empty()&&deferredMarker<0)return;
    ++frame.markerCount;
    GLint framebuffer=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);
    std::string error;
    if(!uniforms.empty()){
        if(captureProgramUniforms(GLuint(program),uniforms,error))frame.captureCount+=unsigned(uniforms.size());
        else note("World uniform capture: "+error);
    }
    for(auto const& resource:resources){
        GLint v[4]{};
        glGetUniformiv(GLuint(program),resource.location,v);
        bool ok=false;error.clear();
        // Depth: (width,height,actual GL internal format).
        // Color: (attachment index,width,height,actual GL internal format).
        // All metadata is supplied by the explicit native material contract.
        switch(resource.kind){
        case Resource::Kind::SceneDepth:
            ok=captureSceneDepth(DepthPhase(resource.index),unsigned(framebuffer),unsigned(v[2]),v[0],v[1],error);break;
        case Resource::Kind::ShadowDepth:
            ok=captureShadowDepth(resource.index,unsigned(framebuffer),unsigned(v[2]),v[0],v[1],error);break;
        case Resource::Kind::SceneColor:
        case Resource::Kind::ShadowColor:{
            auto* format=formatName(v[3]);
            if(!format||v[0]<0){error="Invalid marker color format/attachment";break;}
            if(resource.kind==Resource::Kind::SceneColor)
                ok=captureSceneColor(resource.index,unsigned(framebuffer),unsigned(v[0]),format,v[1],v[2],error);
            else ok=captureShadowColor(resource.index,unsigned(framebuffer),unsigned(v[0]),format,v[1],v[2],error);
            break;
        }
        }
        if(ok)++frame.captureCount;
        else note(resource.name+": "+error);
    }
    if(deferredMarker>=0){
        GLint dimensions[2]{};glGetUniformiv(GLuint(program),deferredMarker,dimensions);
        if(renderDeferred(dimensions[0],dimensions[1],error))note("Opaque deferred stage executed");
        else note("Opaque deferred stage: "+error);
    }
}

// Explicit native material samplers may borrow the deferred outputs during
// every translucent draw. Restore the game's texture and sampler bindings
// immediately afterward; a missing output never substitutes a fake image.
struct DeferredInputs {
    struct Saved { unsigned unit; GLint texture,sampler; };
    std::vector<Saved> saved;
    GLint active=GL_TEXTURE0;
    DeferredInputs(){
        if(drawNesting!=1||frame.inPresent||frame.deferredSamplers.empty()||
            !captureRequested()||frame.context!=eglGetCurrentContext()||
            frame.surface!=eglGetCurrentSurface(EGL_DRAW))return;
        GLint program=0,maxUnits=0;
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&maxUnits);
        struct Input { unsigned unit; GLuint texture; };
        std::vector<Input> inputs;
        for(auto const& binding:frame.deferredSamplers)if(binding.program==GLuint(program)){
            GLuint texture=deferredTexture(binding.index);
            GLint unit=-1;glGetUniformiv(GLuint(program),binding.location,&unit);
            if(!texture||unit<0||unit>=maxUnits){
                note("Deferred material input unavailable: "+std::to_string(binding.index));return;
            }
            if(std::any_of(inputs.begin(),inputs.end(),[&](auto const& i){return i.unit==unsigned(unit);})){
                note("Conflicting deferred sampler units");return;
            }
            inputs.push_back({unsigned(unit),texture});
        }
        if(inputs.empty())return;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        for(auto const& input:inputs){
            glActiveTexture(GL_TEXTURE0+input.unit);
            Saved old{input.unit,0,0};
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&old.texture);
            glGetIntegerv(GL_SAMPLER_BINDING,&old.sampler);
            saved.push_back(old);
            glBindSampler(input.unit,0);glBindTexture(GL_TEXTURE_2D,input.texture);
        }
        glActiveTexture(GLenum(active));
    }
    ~DeferredInputs(){
        for(auto const& old:saved){
            glActiveTexture(GL_TEXTURE0+old.unit);
            glBindTexture(GL_TEXTURE_2D,GLuint(old.texture));glBindSampler(old.unit,GLuint(old.sampler));
        }
        if(!saved.empty())glActiveTexture(GLenum(active));
    }
};
void noteTerrainDraw(){
    if(!captureRequested()||frame.inPresent||
       frame.context!=eglGetCurrentContext()||frame.surface!=eglGetCurrentSurface(EGL_DRAW))return;
    GLint p=0;glGetIntegerv(GL_CURRENT_PROGRAM,&p);
    if(std::find(frame.terrainPrograms.begin(),frame.terrainPrograms.end(),GLuint(p))==frame.terrainPrograms.end())return;
    GLboolean write=GL_FALSE;glGetBooleanv(GL_DEPTH_WRITEMASK,&write);
    if(!write||!glIsEnabled(GL_DEPTH_TEST)||glIsEnabled(GL_BLEND))return;
    GLint viewport[4]{},fbo=0;glGetIntegerv(GL_VIEWPORT,viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
    if(viewport[0]||viewport[1]||viewport[2]<=0||viewport[3]<=0)return;
    frame.terrainCandidate=true;frame.terrainFramebuffer=GLuint(fbo);
    frame.terrainWidth=viewport[2];frame.terrainHeight=viewport[3];
}
// A uniform-name match plus an FBO switch cannot establish phase ownership
// or prove that HUD/postprocessing has not already drawn. Observe only;
// a verified world adapter must explicitly call renderNativeTerrainPhase
// or renderJavaWorldScene. Never write to a guessed native framebuffer.
void describeTerrainCandidate(){
    if(!frame.terrainCandidate)return;
    GLint samples=0,draw=0;glGetIntegerv(GL_SAMPLES,&samples);
    glGetIntegerv(GL_DRAW_BUFFER0,&draw);
    frame.terrainEffectReport="Candidate FBO="+std::to_string(frame.terrainFramebuffer)+
        " viewport="+std::to_string(frame.terrainWidth)+"x"+std::to_string(frame.terrainHeight)+
        " samples="+std::to_string(samples)+" draw0="+std::to_string(draw)+
        " (observation only; no verified world phase)";
    frame.terrainCandidate=false;
}
template<class Fn,class... Args>
void draw(ObservedDraw const& evidence,Fn original,Args... args){
    struct Guard { Guard(){++drawNesting;} ~Guard(){--drawNesting;} } guard;
    if(drawNesting==1&&!frame.inPresent&&captureRequested()){
        outerDraws.fetch_add(1,std::memory_order_relaxed);
        if(frame.context!=EGL_NO_CONTEXT&&frame.context==eglGetCurrentContext()&&
           frame.surface==eglGetCurrentSurface(EGL_DRAW)){
            if(!frame.trace.full()){
                GLint p=0,fbo=0;
                glGetIntegerv(GL_CURRENT_PROGRAM,&p);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
                frame.trace.add({TraceKind::Draw,unsigned(p),unsigned(fbo),evidence.mode,evidence.variant});
            }else frame.trace.omit();
            captureBeforeDraw(evidence);
        }else foreignDraws.fetch_add(1,std::memory_order_relaxed);
    }
    DeferredInputs inputs;
    original(args...);
    if(drawNesting==1)noteTerrainDraw();
}

using BindFramebufferFn=void(*)(GLenum,GLuint);
BindFramebufferFn BindFramebufferOriginal[2]{};
void bindFramebuffer(BindFramebufferFn original,GLenum target,GLuint id){
    if(!drawNesting&&!frame.inPresent&&frame.terrainCandidate&&
       frame.context==eglGetCurrentContext()&&frame.surface==eglGetCurrentSurface(EGL_DRAW)&&
       (target==GL_FRAMEBUFFER||target==GL_DRAW_FRAMEBUFFER)&&id!=frame.terrainFramebuffer){
        GLint current=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&current);
        if(GLuint(current)==frame.terrainFramebuffer)describeTerrainCandidate();
    }
    if(!drawNesting&&!frame.inPresent&&captureRequested()&&frame.context!=EGL_NO_CONTEXT&&
       frame.context==eglGetCurrentContext()&&frame.surface==eglGetCurrentSurface(EGL_DRAW)&&
       (target==GL_FRAMEBUFFER||target==GL_DRAW_FRAMEBUFFER||target==GL_READ_FRAMEBUFFER)){
        GLint current=0;glGetIntegerv(target==GL_READ_FRAMEBUFFER?GL_READ_FRAMEBUFFER_BINDING:
            GL_DRAW_FRAMEBUFFER_BINDING,&current);
        if(GLuint(current)!=id)frame.trace.add({TraceKind::Bind,target,unsigned(current),id,0});
    }
    original(target,id);
}
void BindFramebufferDirect(GLenum target,GLuint id){bindFramebuffer(BindFramebufferOriginal[0],target,id);}
void BindFramebufferResolved(GLenum target,GLuint id){bindFramebuffer(BindFramebufferOriginal[1],target,id);}

// These hooks observe real clear/copy operations before forwarding them.
// They do not infer opaque/HUD phase ownership or alter native attachments.
bool traceAllowed(){
    return !drawNesting&&!frame.inPresent&&captureRequested()&&frame.context!=EGL_NO_CONTEXT&&
        frame.context==eglGetCurrentContext()&&frame.surface==eglGetCurrentSurface(EGL_DRAW);
}
thread_local unsigned phaseNesting=0;
struct PhaseGuard { PhaseGuard(){++phaseNesting;} ~PhaseGuard(){--phaseNesting;} };
using ClearFn=void(*)(GLbitfield);
ClearFn ClearOriginal[2]{};
void clear(ClearFn original,GLbitfield mask){
    PhaseGuard guard;
    if(phaseNesting==1&&traceAllowed()){
        GLint fbo=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
        frame.trace.add({TraceKind::Clear,unsigned(fbo),mask,0,0});
    }
    original(mask);
}
void ClearDirect(GLbitfield m){clear(ClearOriginal[0],m);}
void ClearResolved(GLbitfield m){clear(ClearOriginal[1],m);}
using BlitFn=void(*)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
BlitFn BlitOriginal[2]{};
void blit(BlitFn original,GLint a,GLint b,GLint c,GLint d,GLint e,GLint f,GLint g,GLint h,GLbitfield mask,GLenum filter){
    PhaseGuard guard;
    if(phaseNesting==1&&traceAllowed()){
        GLint read=0,write=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&write);
        frame.trace.add({TraceKind::Blit,unsigned(read),unsigned(write),mask,filter});
    }
    original(a,b,c,d,e,f,g,h,mask,filter);
}
void BlitDirect(GLint a,GLint b,GLint c,GLint d,GLint e,GLint f,GLint g,GLint h,GLbitfield m,GLenum n){blit(BlitOriginal[0],a,b,c,d,e,f,g,h,m,n);}
void BlitResolved(GLint a,GLint b,GLint c,GLint d,GLint e,GLint f,GLint g,GLint h,GLbitfield m,GLenum n){blit(BlitOriginal[1],a,b,c,d,e,f,g,h,m,n);}

// Both the public GLES export and the resolved EGL dispatch address may
// be used by a native renderer. Hook each distinct address once.
// Use the comma-containing signatures through parenthesized argument tuples.
#define OPTLEVI_UNWRAP(...) __VA_ARGS__
#define OPTLEVI_DRAW_ENTRY(NAME,PARAMS,ARGS,EVIDENCE) \
    using NAME##Fn=void (*) PARAMS; \
    NAME##Fn NAME##Original[2]{}; \
    void NAME##Direct PARAMS { draw(ObservedDraw{OPTLEVI_UNWRAP EVIDENCE},NAME##Original[0],OPTLEVI_UNWRAP ARGS); } \
    void NAME##Resolved PARAMS { draw(ObservedDraw{OPTLEVI_UNWRAP EVIDENCE},NAME##Original[1],OPTLEVI_UNWRAP ARGS); }
OPTLEVI_DRAW_ENTRY(DrawElements,(GLenum m,GLsizei n,GLenum t,void const* p),(m,n,t,p),(m,n,1,true,t,reinterpret_cast<std::uintptr_t>(p),0))
OPTLEVI_DRAW_ENTRY(DrawArrays,(GLenum m,GLint f,GLsizei n),(m,f,n),(m,n,1,false,0,0,f))
OPTLEVI_DRAW_ENTRY(DrawElementsInstanced,(GLenum m,GLsizei n,GLenum t,void const* p,GLsizei c),(m,n,t,p,c),(m,n,c,true,t,reinterpret_cast<std::uintptr_t>(p),0))
OPTLEVI_DRAW_ENTRY(DrawArraysInstanced,(GLenum m,GLint f,GLsizei n,GLsizei c),(m,f,n,c),(m,n,c,false,0,0,f))
OPTLEVI_DRAW_ENTRY(DrawRangeElements,(GLenum m,GLuint a,GLuint b,GLsizei n,GLenum t,void const* p),(m,a,b,n,t,p),(m,n,1,true,t,reinterpret_cast<std::uintptr_t>(p),0,0,1))
OPTLEVI_DRAW_ENTRY(DrawElementsBaseVertex,(GLenum m,GLsizei n,GLenum t,void const* p,GLint v),(m,n,t,p,v),(m,n,1,true,t,reinterpret_cast<std::uintptr_t>(p),0,v,2))
OPTLEVI_DRAW_ENTRY(DrawElementsBaseVertexOES,(GLenum m,GLsizei n,GLenum t,void const* p,GLint v),(m,n,t,p,v),(m,n,1,true,t,reinterpret_cast<std::uintptr_t>(p),0,v,2))
OPTLEVI_DRAW_ENTRY(DrawElementsBaseVertexEXT,(GLenum m,GLsizei n,GLenum t,void const* p,GLint v),(m,n,t,p,v),(m,n,1,true,t,reinterpret_cast<std::uintptr_t>(p),0,v,2))
OPTLEVI_DRAW_ENTRY(DrawElementsInstancedBaseVertex,(GLenum m,GLsizei n,GLenum t,void const* p,GLsizei c,GLint v),(m,n,t,p,c,v),(m,n,c,true,t,reinterpret_cast<std::uintptr_t>(p),0,v,3))
OPTLEVI_DRAW_ENTRY(DrawElementsInstancedBaseVertexOES,(GLenum m,GLsizei n,GLenum t,void const* p,GLsizei c,GLint v),(m,n,t,p,c,v),(m,n,c,true,t,reinterpret_cast<std::uintptr_t>(p),0,v,3))
OPTLEVI_DRAW_ENTRY(DrawElementsInstancedBaseVertexEXT,(GLenum m,GLsizei n,GLenum t,void const* p,GLsizei c,GLint v),(m,n,t,p,c,v),(m,n,c,true,t,reinterpret_cast<std::uintptr_t>(p),0,v,3))
#undef OPTLEVI_DRAW_ENTRY
#undef OPTLEVI_UNWRAP

template<class Fn>
unsigned hookEntry(void* library,char const* name,Fn direct,Fn resolved,Fn (&original)[2]){
    void* targets[2]={dlsym(library,name),reinterpret_cast<void*>(eglGetProcAddress(name))};
    Fn detours[2]={direct,resolved};
    unsigned count=0;
    for(unsigned i=0;i<2;++i){
        std::string label=std::string(name)+(i?" [EGL resolved]":" [GLES export]");
        if(!targets[i]){shaderdiagnostics::record("Hook entry",label+": unavailable");continue;}
        if(std::find(installedTargets.begin(),installedTargets.end(),targets[i])!=installedTargets.end()){
            shaderdiagnostics::record("Hook entry",label+": address already hooked; skipped");continue;
        }
        // Each detour owns one immutable original slot. A different late
        // EGL address must not overwrite a live trampoline.
        if(original[i]){
            shaderdiagnostics::record("Hook entry",label+": address changed; existing trampoline retained");continue;
        }
        void* trampoline=nullptr;
        int result=pl::memory::hook(targets[i],reinterpret_cast<void*>(detours[i]),&trampoline);
        if(result==0&&trampoline){
            original[i]=reinterpret_cast<Fn>(trampoline);++count;
            installedTargets.push_back(targets[i]);
            shaderdiagnostics::record("Hook entry",label+": installed");
        }else{
            if(result==0&&!trampoline)pl::memory::unhook(targets[i],reinterpret_cast<void*>(detours[i]));
            shaderdiagnostics::record("Hook entry",label+": failed ("+std::to_string(result)+")");
        }
    }
    return count;
}
// Optional GLES 3.2 / OES / EXT calls are installed only after observing
// actual support from the current accepted game context. No private symbols.
void installContextHooks(){
    static bool attempted=false;
    std::lock_guard lock(installMu);
    if(attempted||!glesLibrary||eglGetCurrentContext()==EGL_NO_CONTEXT)return;
    attempted=true;
    GLint major=0,minor=0,n=0;
    glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    glGetIntegerv(GL_NUM_EXTENSIONS,&n);
    bool oes=false,ext=false;
    for(GLint i=0;i<n&&i<4096;++i){
        auto p=reinterpret_cast<char const*>(glGetStringi(GL_EXTENSIONS,GLuint(i)));
        if(!p)continue;
        oes|=std::string_view(p)=="GL_OES_draw_elements_base_vertex";
        ext|=std::string_view(p)=="GL_EXT_draw_elements_base_vertex";
    }
    unsigned count=0;
    // Retry unresolved core entrypoints after EGL initialization without
    // replacing already-installed trampolines.
    count+=hookEntry(glesLibrary,"glDrawElements",DrawElementsDirect,DrawElementsResolved,DrawElementsOriginal);
    count+=hookEntry(glesLibrary,"glDrawArrays",DrawArraysDirect,DrawArraysResolved,DrawArraysOriginal);
    count+=hookEntry(glesLibrary,"glDrawElementsInstanced",DrawElementsInstancedDirect,DrawElementsInstancedResolved,DrawElementsInstancedOriginal);
    count+=hookEntry(glesLibrary,"glDrawArraysInstanced",DrawArraysInstancedDirect,DrawArraysInstancedResolved,DrawArraysInstancedOriginal);
    count+=hookEntry(glesLibrary,"glBindFramebuffer",BindFramebufferDirect,BindFramebufferResolved,BindFramebufferOriginal);
    count+=hookEntry(glesLibrary,"glDrawRangeElements",DrawRangeElementsDirect,DrawRangeElementsResolved,DrawRangeElementsOriginal);
    count+=hookEntry(glesLibrary,"glClear",ClearDirect,ClearResolved,ClearOriginal);
    count+=hookEntry(glesLibrary,"glBlitFramebuffer",BlitDirect,BlitResolved,BlitOriginal);
    if(major>3||(major==3&&minor>=2)){
        count+=hookEntry(glesLibrary,"glDrawElementsBaseVertex",DrawElementsBaseVertexDirect,DrawElementsBaseVertexResolved,DrawElementsBaseVertexOriginal);
        count+=hookEntry(glesLibrary,"glDrawElementsInstancedBaseVertex",DrawElementsInstancedBaseVertexDirect,DrawElementsInstancedBaseVertexResolved,DrawElementsInstancedBaseVertexOriginal);
    }
    if(oes){
        count+=hookEntry(glesLibrary,"glDrawElementsBaseVertexOES",DrawElementsBaseVertexOESDirect,DrawElementsBaseVertexOESResolved,DrawElementsBaseVertexOESOriginal);
        count+=hookEntry(glesLibrary,"glDrawElementsInstancedBaseVertexOES",DrawElementsInstancedBaseVertexOESDirect,DrawElementsInstancedBaseVertexOESResolved,DrawElementsInstancedBaseVertexOESOriginal);
    }
    if(ext){
        count+=hookEntry(glesLibrary,"glDrawElementsBaseVertexEXT",DrawElementsBaseVertexEXTDirect,DrawElementsBaseVertexEXTResolved,DrawElementsBaseVertexEXTOriginal);
        count+=hookEntry(glesLibrary,"glDrawElementsInstancedBaseVertexEXT",DrawElementsInstancedBaseVertexEXTDirect,DrawElementsInstancedBaseVertexEXTResolved,DrawElementsInstancedBaseVertexEXTOriginal);
    }
    hookCount.fetch_add(count);
    shaderdiagnostics::record("Hooks","Context GLES "+std::to_string(major)+"."+std::to_string(minor)+
        "; OES base vertex="+std::to_string(oes)+" EXT base vertex="+std::to_string(ext)+
        "; additional hooks="+std::to_string(count));
}
}

bool install(){
    static bool attempted=false;
    std::lock_guard installLock(installMu);
    if(attempted)return hookCount.load()!=0;
    attempted=true;
    void* library=dlopen("libGLESv3.so",RTLD_NOW);
    if(!library){std::lock_guard lock(reportMu);lastReport="Native draw bridge: GLES library unavailable";shaderdiagnostics::record("Hooks",lastReport);return false;}
    glesLibrary=library;
    unsigned count=0;
    count+=hookEntry(library,"glDrawElements",DrawElementsDirect,DrawElementsResolved,DrawElementsOriginal);
    count+=hookEntry(library,"glDrawArrays",DrawArraysDirect,DrawArraysResolved,DrawArraysOriginal);
    count+=hookEntry(library,"glDrawElementsInstanced",DrawElementsInstancedDirect,DrawElementsInstancedResolved,DrawElementsInstancedOriginal);
    count+=hookEntry(library,"glDrawArraysInstanced",DrawArraysInstancedDirect,DrawArraysInstancedResolved,DrawArraysInstancedOriginal);
    count+=hookEntry(library,"glBindFramebuffer",BindFramebufferDirect,BindFramebufferResolved,BindFramebufferOriginal);
    count+=hookEntry(library,"glDrawRangeElements",DrawRangeElementsDirect,DrawRangeElementsResolved,DrawRangeElementsOriginal);
    count+=hookEntry(library,"glClear",ClearDirect,ClearResolved,ClearOriginal);
    count+=hookEntry(library,"glBlitFramebuffer",BlitDirect,BlitResolved,BlitOriginal);
    hookCount.store(count);
    std::lock_guard lock(reportMu);
    lastReport="Native GLES draw hook entrypoints: "+std::to_string(count);
    shaderdiagnostics::record("Hooks",lastReport);
    return count!=0;
}

PresentGuard::PresentGuard(){
    frame.inPresent=true;
    installContextHooks();
    auto ctx=eglGetCurrentContext();auto surface=eglGetCurrentSurface(EGL_DRAW);
    // Record a remaining candidate for diagnostics, not as a phase barrier.
    // The default framebuffer can already contain HUD or postprocessing.
    if(frame.context==ctx&&frame.surface==surface&&frame.terrainCandidate){
        GLint current=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&current);
        if(GLuint(current)==frame.terrainFramebuffer)describeTerrainCandidate();
    }
    if(captureRequested()&&frame.context==ctx&&frame.surface==surface){
        std::string current="Native GLES hook entrypoints: "+std::to_string(hookCount.load())+
            "\nOuter hooked draws (all contexts, cumulative): "+std::to_string(outerDraws.load())+
            "\nDraws outside accepted context (cumulative): "+std::to_string(foreignDraws.load())+
            "\nPrograms observed: "+std::to_string(frame.seenCount)+
            "\nExplicit phase marker programs: "+std::to_string(frame.markerCount)+
            "\nWorld resources captured: "+std::to_string(frame.captureCount)+
            "\nTerrain candidate programs (unverified): "+std::to_string(frame.terrainPrograms.size());
        if(!frame.markerCount)current+="\nNo verified OptLevi phase-marker material was observed.";
        if(!frame.firstTerrainLayout.empty())current+="\n"+frame.firstTerrainLayout;
        if(!frame.firstMeshSample.empty())current+="\n"+frame.firstMeshSample;
        if(!frame.terrainEffectReport.empty())current+="\n"+frame.terrainEffectReport;
        if(!frame.programSignatures.empty())
            current+="\nObserved native shader uniforms (unclassified):"+frame.programSignatures;
        if(!frame.details.empty())current+="\n"+frame.details;
        if(!frame.drawEvidence.empty())current+="\nDraw layout evidence:"+frame.drawEvidence.substr(0,4500);
        auto order=frame.trace.report();
        if(!order.empty())current+="\nNative GL event order (not classified phases):"+order;
        { // Include zero-draw reports; missing hook coverage is evidence too.
            auto vendor=reinterpret_cast<char const*>(glGetString(GL_VENDOR));
            auto renderer=reinterpret_cast<char const*>(glGetString(GL_RENDERER));
            auto version=reinterpret_cast<char const*>(glGetString(GL_VERSION));
            current+="\nGLES vendor: "+std::string(vendor?vendor:"unknown")+
                "\nGLES renderer: "+std::string(renderer?renderer:"unknown")+
                "\nGLES version: "+std::string(version?version:"unknown");
            shaderdiagnostics::record((!frame.terrainPrograms.empty()||frame.markerCount)?
                "World candidate observation":"Unclassified draw observation",current,true);
        }
        std::lock_guard lock(reportMu);lastReport=std::move(current);
    }
    if(frame.context!=ctx||frame.surface!=surface){
        frame.firstTerrainLayout.clear();frame.firstMeshSample.clear();frame.meshSampleAttempted=false;
    }
    frame.context=ctx;frame.surface=surface;
    frame.seenCount=frame.markerCount=frame.captureCount=frame.terrainCount=0;
    frame.details.clear();frame.deferredSamplers.clear();frame.terrainPrograms.clear();
    frame.terrainCandidate=false;frame.terrainEffectReport.clear();
    frame.programSignatures.clear();frame.drawEvidence.clear();frame.trace.reset();
}
PresentGuard::~PresentGuard(){frame.inPresent=false;}
std::string report(){std::lock_guard lock(reportMu);return lastReport;}
}
