// OptLevi 0.11: Android shaderpack import, GLES3 test preview, and opt-in
// single-pass world postprocess. Full Java/Iris multi-pass rendering is not supported.
#include "OptLeviShaderCatalog.inc"
#include "OptLeviShaderGlesTest.inc"
#include "ShaderRenderBridge.h"

static bool gOptShadersVisible=false;
static bool gOptShadersScanned=false;
static bool gOptCustomBuffersFound=false;
static bool gOptPreviewEnabled=true;
static bool gOptSavedEnabled=true;
static std::string gOptShadersError;
static std::string gOptShadersSelection;  // pending in UI, not Minecraft render state
static std::string gOptSavedSelection;    // last accepted TEST choice, not world effects
static std::string gOptImportStatus;

static std::filesystem::path ofShaderpacksPath() {
    return paths::selfDir()/"shaderpacks";
}

static void ofScanShaderpacks() {
    gOptShaderFiles.clear();
    gOptShadersError.clear();
    std::error_code ec;
    auto folder=ofShaderpacksPath();
    std::filesystem::create_directories(folder,ec);
    if(ec)gOptShadersError=ec.message();
    else {
        std::filesystem::directory_iterator it(folder,ec),end;
        for(;!ec && it!=end && gOptShaderFiles.size()<200;it.increment(ec))
            if(optCandidate(*it))gOptShaderFiles.push_back(optReadPack(it->path()));
        if(ec)gOptShadersError=ec.message();
    }
    std::sort(gOptShaderFiles.begin(),gOptShaderFiles.end(),
              [](auto const& a,auto const& b) {
                  return a.path.filename().string()<b.path.filename().string();
              });
    if(!gOptShadersSelection.empty() &&
       std::none_of(gOptShaderFiles.begin(),gOptShaderFiles.end(),
            [](auto const& p){return p.path.filename().string()==gOptShadersSelection;}))
       gOptShadersSelection.clear();
    gOptShadersScanned=true;
}

static void ofReceiveImportedShaders() {
    modmenu::android::shaderimport::Result result;
    if(!modmenu::android::shaderimport::consumeResult(result))return;
    gOptImportStatus=result.message;
    if(result.success) {
        ofScanShaderpacks();
        // A newly imported archive is discoverable immediately.
        // Selection remains a separate, explicit player action.
    }
}

static void ofTestShaderSelection() {
    auto pack=std::find_if(gOptShaderFiles.begin(),gOptShaderFiles.end(),
        [](OptPackInfo const& p) {
            return p.path.filename().string()==gOptShadersSelection;
        });
    optTestSelectedShader(pack==gOptShaderFiles.end()?nullptr:&*pack);
}

static void drawOptLeviShadersScreen(ImVec2 display) {
    if(!gOptShadersScanned)ofScanShaderpacks();
    ofReceiveImportedShaders();
    optPreviewCheckContext();
    float u=ofu(display);
    ImDrawList* dl=ImGui::GetWindowDrawList();

    // Iris-style centered browser; dark world background and Minecraft's own
    // textured borders via the same Bedrock 26.60 texture atlas as ModMenu.
    dl->AddRectFilled({0,0},display,ofc(0x111319,195));
    float left=display.x*0.20f,right=display.x*0.80f;
    float w=right-left;
    float top=10*u,barY=74*u;
    float bottom=display.y-175*u;
    float line=32*u;
    if(w<240*u||bottom<barY+100*u){
        ofText(dl,fonts::bold(),{20*u,20*u},"Shaders: enlarge the screen",
               ofc(0xFFFFFF));
        if(ofButton("##sh_small_close","Done",{30*u,display.y-43*u},
                    {125*u,30*u},u))gOptShadersVisible=false;
        return;
    }

    // Two-line title and top enable switch mimic Iris, but the scope is
    // explicitly the GPU TEST bench, not a claim of world-wide shader load.
    std::string title="Shader Packs";
    fonts::note(title);
    ImFont* bold=fonts::bold();
    float titleW=bold->CalcTextSizeA(bold->FontSize,FLT_MAX,0,title.c_str()).x;
    ofText(dl,bold,{display.x*0.5f-titleW*0.5f,top},
           title.c_str(),ofc(0xFFFFFF));
    std::string sub="Java composite pipeline / GLES3 (experimental)";
    fonts::note(sub);
    ImFont* font=fonts::body();
    float subW=font->CalcTextSizeA(font->FontSize,FLT_MAX,0,sub.c_str()).x;
    ofText(dl,font,{display.x*0.5f-subW*0.5f,top+26*u},
           sub.c_str(),ofc(0xDADADA));

    const float refreshW=37*u;
    std::string enabled=gOptPreviewEnabled?"Shaders: Enabled (experimental)":"Shaders: Disabled";
    if(ofButton("##iris_enable",enabled.c_str(),{left,barY},
                {w-refreshW-6*u,line},u))
        gOptPreviewEnabled=!gOptPreviewEnabled;
    if(ofButton("##iris_refresh","R",
                {right-refreshW,barY},{refreshW,line},u))
        ofScanShaderpacks();

    float listTop=barY+line+7*u;
    dl->AddRectFilled({left,listTop},{right,bottom},ofc(0x161A1E,192));
    ofNineSprite(dl,OptSprite::PanelDark,{left,listTop},{right,bottom},u,
                 IM_COL32(115,123,133,220));
    ImGui::SetCursorScreenPos({left+3*u,listTop+3*u});
    ImGui::BeginChild("##iris_pack_list",
                     {w-6*u,bottom-listTop-6*u},ImGuiChildFlags_None);
    auto row=[&](char const* id,std::string const& name,bool selected) {
        ImVec2 pos=ImGui::GetCursorScreenPos();
        float width=ImGui::GetContentRegionAvail().x-2*u;
        ImGui::PushID(id);
        bool clicked=ImGui::InvisibleButton("##iris_pack_row",{width,36*u});
        bool hover=ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList* rowDraw=ImGui::GetWindowDrawList();
        if(selected) {
            rowDraw->AddRectFilled(pos,{pos.x+width,pos.y+36*u},
                                   ofc(0x0B0C0F,255));
            rowDraw->AddRect(pos,{pos.x+width,pos.y+36*u},
                             ofc(0xE9E9E9),0,0,u);
        }else if(hover)
            rowDraw->AddRectFilled(pos,{pos.x+width,pos.y+36*u},
                                   ofc(0x41494E,220));
        fonts::note(name);
        float maxW=width-12*u;
        float tw=font->CalcTextSizeA(font->FontSize,FLT_MAX,0,name.c_str()).x;
        rowDraw->PushClipRect({pos.x+5*u,pos.y},
                              {pos.x+width-5*u,pos.y+36*u},true);
        ofText(rowDraw,font,{pos.x+std::max(8*u,(width-tw)/2),
                             pos.y+6*u},name.c_str(),
               selected?ofc(0xFFF1A5):ofc(0xFFFFFF),0,maxW);
        rowDraw->PopClipRect();
        return clicked;
    };
    if(row("vanilla","Off (vanilla rendering)",gOptShadersSelection.empty()))
        gOptShadersSelection.clear();
    for(size_t i=0;i<gOptShaderFiles.size();i++){
        auto name=gOptShaderFiles[i].path.filename().string();
        ImGui::PushID(int(i));
        if(row("pack",name,name==gOptShadersSelection))gOptShadersSelection=name;
        ImGui::PopID();
    }
    if(gOptShaderFiles.empty())ImGui::TextDisabled("No shader packs. Choose Import.");
    ImGui::EndChild();

    float infoY=bottom+4*u;
    std::string label=gOptShadersSelection.empty()?
        "Vanilla (no Java shader selected)" : gOptShadersSelection;
    auto world=modmenu::android::shaderworld::status();
    std::string status=world.message;
    auto picked=std::find_if(gOptShaderFiles.begin(),gOptShaderFiles.end(),
        [](OptPackInfo const& p){
            return p.path.filename().string()==gOptShadersSelection;
        });
    if(picked!=gOptShaderFiles.end()){
        label += " | ";
        label += optPackLabel(picked->type);
    }
    dl->PushClipRect({left,infoY},{right,display.y-77*u},true);
    ofText(dl,font,{left+5*u,infoY},label.c_str(),
           ofc(0xDDE6EC),font->FontSize*0.84f,w-10*u);
    // A driver failure must never be hidden behind stale import toast text.
    if(world.failed) status=world.message;
    else if(!gOptImportStatus.empty())status=gOptImportStatus;
    else if(!gOptShadersError.empty())status=gOptShadersError;
    float textSize=font->FontSize*0.78f;
    dl->AddText(font,textSize,{left+5*u,infoY+22*u},ofc(world.failed?0xFFD0A0:0xD7D7D7),
                status.c_str(),nullptr,w-10*u);
    dl->PopClipRect();

    const float gap=7*u,buttonH=30*u;
    float by=display.y-70*u;
    float importW=w*0.32f;
    float testW=w*0.32f;
    float folderW=w-importW-testW-2*gap;
    if(ofButton("##iris_import","Import Shader Pack",
                {left,by},{importW,buttonH},u,
                modmenu::android::shaderimport::available())){
        if(!modmenu::android::shaderimport::openPicker())
            gOptImportStatus="Android document picker unavailable";
        else gOptImportStatus="Choose a ZIP or .mcpack in Android's document picker";
    }
    if(ofButton("##iris_test","Test GLES Shader",
                {left+importW+gap,by},{testW,buttonH},u,gOptPreviewEnabled))
        ofTestShaderSelection();
    if(ofButton("##iris_folder","Shader Pack Folder",
                {right-folderW,by},{folderW,buttonH},u)){
        if(!openFolder(ofShaderpacksPath()))
            gOptImportStatus=ofShaderpacksPath().string();
    }

    float by2=display.y-35*u;
    float third=(w-2*gap)/3;
    if(ofButton("##iris_cancel","Cancel",{left,by2},{third,buttonH},u)) {
        gOptPreviewReport.clear();
        gOptShadersSelection=gOptSavedSelection;
        gOptPreviewEnabled=gOptSavedEnabled;
        gOptImportStatus="Selection reverted (world effect unchanged)";
    }
    if(ofButton("##iris_apply","Apply",{left+third+gap,by2},
                {third,buttonH},u)) {
        // Show the shader compiler's real status after Apply.
        gOptImportStatus.clear();
        if(!gOptPreviewEnabled||gOptShadersSelection.empty()) {
            modmenu::android::shaderworld::requestDisable();
            gOptSavedSelection=gOptShadersSelection;
            gOptSavedEnabled=gOptPreviewEnabled;
            gOptImportStatus="Experimental world postprocess disabled";
        }else{
            auto selected=std::find_if(gOptShaderFiles.begin(),gOptShaderFiles.end(),
                [](OptPackInfo const& p) {
                    return p.path.filename().string()==gOptShadersSelection;
                });
            std::vector<modmenu::android::shaderworld::Pass> passes;
            std::string error;
            if(selected!=gOptShaderFiles.end() &&
               optBuildJavaPipeline(*selected,passes,error)) {
                modmenu::android::shaderworld::requestPipeline(
                    std::move(passes),selected->path.filename().string());
                gOptSavedSelection=gOptShadersSelection;
                gOptSavedEnabled=gOptPreviewEnabled;
                gOptImportStatus.clear();
            }else {
                modmenu::android::shaderworld::requestDisable();
                gOptImportStatus=error.empty()?
                    "Select a supported Java composite pack":error;
                gOptPreviewReport=gOptImportStatus;
            }
        }
    }
    if(ofButton("##iris_done","Done",
                {right-third,by2},{third,buttonH},u)) {
        gOptShadersVisible=false;
    }

    // Live GPU preview thumbnail, only after the explicit Test button succeeds.
    // It uses a synthetic input gradient, NOT the RenderDragon world image.
    if(gOptPreviewSuccess && gOptPreviewTexture &&
       gOptPreviewContext==eglGetCurrentContext()){
        float pw=std::min(148*u,std::max(64*u,left-30*u));
        float ph=pw*0.5625f;
        ImVec2 pt{std::max(8*u,left-pw-10*u),barY+line+7*u};
        dl->AddRectFilled({pt.x-4*u,pt.y-4*u},
                          {pt.x+pw+4*u,pt.y+ph+27*u},
                           ofc(0x111111,255));
        dl->AddImage(ImTextureID(intptr_t(gOptPreviewTexture)),pt,
                     {pt.x+pw,pt.y+ph});
        ofText(dl,font,{pt.x,pt.y+ph+4*u},"GLES3 test preview (not world)",
               ofc(0xFFFFFF),font->FontSize*0.72f,pw);
    }
    if(!gOptPreviewReport.empty()||world.failed) {
        // Compilation errors are not discarded. Use a scrollable popup to
        // inspect the real driver log instead of pretending support.
        if(ofButton("##iris_compilelog","Compiler log",
                    {left+5*u,bottom-35*u},
                    {150*u,29*u},u))
            ImGui::OpenPopup("##iris_compile_details");
        if(ImGui::BeginPopup("##iris_compile_details")){
            ImGui::TextWrapped("%s",gOptPreviewFile.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("%s",gOptPreviewReport.c_str());
            ImGui::TextWrapped("World: %s",world.message.c_str());
            ImGui::Separator();
            auto nativeReport=modmenu::android::shaderbridge::report();
            ImGui::TextWrapped("%s",nativeReport.c_str());
            ImGui::EndPopup();
        }
    }
}
