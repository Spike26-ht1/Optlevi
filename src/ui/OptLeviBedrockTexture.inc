// GPU renderer for the exact Bedrock 26.60 UI sprite pixels embedded below.
// This file is included INSIDE ModMenuScreen.cpp's anonymous namespace.
// Global GL/EGL/stb_image headers are inserted before the namespace by
// scripts/prepare-fabric-real-screen.py for Android native compilation.
#include "OptLeviBedrockAtlas.inc"

static std::vector<unsigned char> ofDecodeAtlasPng() {
    std::vector<unsigned char> out;
    int bits=0;
    unsigned acc=0;
    for(char c : kOptAtlasPngBase64) {
        if(!c || c=='=')break;
        int v=-1;
        if(c>='A'&&c<='Z')v=c-'A';
        else if(c>='a'&&c<='z')v=c-'a'+26;
        else if(c>='0'&&c<='9')v=c-'0'+52;
        else if(c=='+')v=62;
        else if(c=='/')v=63;
        if(v<0)continue;
        acc=(acc<<6)|unsigned(v);
        bits+=6;
        if(bits>=8) {
            bits-=8;
            out.push_back(static_cast<unsigned char>((acc>>bits)&255u));
            acc&=(1u<<bits)-1u;
        }
    }
    return out;
}

// Lazily decode the embedded PNG and upload once per live EGL context.
// Works without an external texture-pack installation. On context switch,
// old texture names are never deleted in the new context.
static GLuint ofAtlasTexture() {
    EGLContext ctx=eglGetCurrentContext();
    if(ctx==EGL_NO_CONTEXT)return 0;
    static EGLContext cached=EGL_NO_CONTEXT;
    static GLuint texture=0;
    static bool failed=false;
    if(ctx!=cached) {
        cached=ctx;
        texture=0;
        failed=false;
    }
    if(texture)return texture;
    if(failed)return 0;
    const auto png=ofDecodeAtlasPng();
    int w=0,h=0,n=0;
    unsigned char* rgba=png.empty()?nullptr:stbi_load_from_memory(
        png.data(),static_cast<int>(png.size()),&w,&h,&n,4);
    if(!rgba || w!=kOptAtlasWidth || h!=kOptAtlasHeight) {
        if(rgba)stbi_image_free(rgba);
        failed=true;
        return 0;
    }
    GLint previous=0,oldAlign=4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&oldAlign);
    GLuint result=0;
    glGenTextures(1,&result);
    if(result){
        glBindTexture(GL_TEXTURE_2D,result);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT,oldAlign);
    glBindTexture(GL_TEXTURE_2D,GLuint(previous));
    stbi_image_free(rgba);
    if(!result || !glIsTexture(result)) {
        failed=true;
        return 0;
    }
    texture=result;
    return texture;
}

static bool ofSpriteImage(ImDrawList* dl,OptSprite sprite,
                          ImVec2 p0,ImVec2 p1,ImU32 tint=IM_COL32_WHITE) {
    GLuint texture=ofAtlasTexture();
    if(!texture)return false;
    auto const& rect=kOptAtlasRects[static_cast<unsigned>(sprite)];
    ImVec2 uv0{float(rect.x)/kOptAtlasWidth,float(rect.y)/kOptAtlasHeight};
    ImVec2 uv1{float(rect.x+rect.w)/kOptAtlasWidth,
               float(rect.y+rect.h)/kOptAtlasHeight};
    dl->AddImage(ImTextureID(intptr_t(texture)),p0,p1,uv0,uv1,tint);
    return true;
}

// A real nine-slice: corners keep original dimensions, the center and edges
// stretch. UVs select exact texels of the unscaled Bedrock source textures.
static bool ofNineSprite(ImDrawList* dl,OptSprite sprite,
                         ImVec2 p0,ImVec2 p1,float scale=1.0f,
                         ImU32 tint=IM_COL32_WHITE) {
    GLuint texture=ofAtlasTexture();
    if(!texture)return false;
    auto const& r=kOptAtlasRects[static_cast<unsigned>(sprite)];
    if(r.corner<=0) return ofSpriteImage(dl,sprite,p0,p1,tint);
    const int c=std::min({r.corner,r.w/2,r.h/2});
    float xCorner=std::min(float(c)*scale,(p1.x-p0.x)*0.5f);
    float yCorner=std::min(float(c)*scale,(p1.y-p0.y)*0.5f);
    float sx[4]={0.0f,float(c),float(r.w-c),float(r.w)};
    float sy[4]={0.0f,float(c),float(r.h-c),float(r.h)};
    float dx[4]={p0.x,p0.x+xCorner,p1.x-xCorner,p1.x};
    float dy[4]={p0.y,p0.y+yCorner,p1.y-yCorner,p1.y};
    for(int iy=0;iy<3;++iy){
        if(dy[iy+1]<=dy[iy])continue;
        for(int ix=0;ix<3;++ix){
            if(dx[ix+1]<=dx[ix])continue;
            ImVec2 u0{(r.x+sx[ix])/kOptAtlasWidth,
                      (r.y+sy[iy])/kOptAtlasHeight};
            ImVec2 u1{(r.x+sx[ix+1])/kOptAtlasWidth,
                      (r.y+sy[iy+1])/kOptAtlasHeight};
            dl->AddImage(ImTextureID(intptr_t(texture)),
                         {dx[ix],dy[iy]},{dx[ix+1],dy[iy+1]},u0,u1,tint);
        }
    }
    return true;
}

// The pixels are from the supplied Minecraft 26.60 scroll-handle source.
static void ofTexturedScrollbar(ImDrawList* dl,ImVec2 start,ImVec2 end,
                                float scroll,float maximum,float u) {
    if(maximum<=1.0f || end.y<=start.y)return;
    float gutterW=8.0f*u;
    ImVec2 track0{end.x-gutterW,start.y};
    ImVec2 track1{end.x,end.y};
    ofSpriteImage(dl,OptSprite::ScrollRail,track0,track1);
    float trackHeight=end.y-start.y;
    float grabHeight=std::clamp(trackHeight*trackHeight/(trackHeight+maximum),
                                18.0f*u,trackHeight);
    float y=start.y+std::clamp(scroll/maximum,0.0f,1.0f)*(trackHeight-grabHeight);
    ofNineSprite(dl,OptSprite::ScrollHandle,
                 {end.x-gutterW,y},{end.x,y+grabHeight},u);
}

static bool ofTexturedCheckbox(char const* id,char const* label,
                               bool& checked, float u) {
    ImVec2 pos=ImGui::GetCursorScreenPos();
    float h=22.0f*u,boxW=26.0f*u;
    float textW=ImGui::CalcTextSize(label).x;
    ImGui::PushID(id);
    bool clicked=ImGui::InvisibleButton("##atlas_check",{boxW+textW+6.0f*u,h});
    bool hovered=ImGui::IsItemHovered();
    ImGui::PopID();
    if(clicked)checked=!checked;
    OptSprite sprite=checked?
        (hovered?OptSprite::CheckOnHover:OptSprite::CheckOn):
        (hovered?OptSprite::CheckOffHover:OptSprite::CheckOff);
    ofSpriteImage(ImGui::GetWindowDrawList(),sprite,
                  {pos.x,pos.y+3.0f*u},{pos.x+21.0f*u,pos.y+20.0f*u});
    ofText(ImGui::GetWindowDrawList(),fonts::body(),
           {pos.x+boxW,pos.y+2.0f*u},label,ofc(0xEEEEEE));
    return clicked;
}
