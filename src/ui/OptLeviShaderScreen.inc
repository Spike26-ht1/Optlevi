// OptLevi 0.7.0: Android shader-pack discovery screen.
// This is an Iris-inspired pack browser, NOT a Java shader compiler.
// Only UI state is changed; no material hooks or render pipeline are touched.
// This file is included after OptLeviFabricScreen helpers in the same TU.

static bool gOptShadersVisible = false;
static bool gOptShadersScanned = false;
static bool gOptCustomBuffersFound = false;
static std::string gOptShadersError;
static std::string gOptShadersSelection;
static std::vector<std::filesystem::path> gOptShaderFiles;

static std::filesystem::path ofShaderpacksPath() {
    // Paths::selfDir() comes from the native mod loader, not Android's cwd.
    return paths::selfDir() / "shaderpacks";
}

static void ofScanShaderpacks() {
    gOptShaderFiles.clear();
    gOptShadersError.clear();
    std::error_code ec;
    auto folder = ofShaderpacksPath();
    if (!std::filesystem::exists(folder, ec) && !ec)
        std::filesystem::create_directories(folder, ec);
    if (ec) {
        gOptShadersError = ec.message();
    } else {
        std::filesystem::directory_iterator it(folder, ec), end;
        for (; !ec && it != end && gOptShaderFiles.size() < 200; it.increment(ec)) {
            std::error_code entryError;
            if (!it->is_regular_file(entryError)) continue;
            auto ext = it->path().extension().string();
            for (char& c : ext) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (ext == ".zip" || ext == ".mcpack")
                gOptShaderFiles.push_back(it->path());
        }
        if (ec) gOptShadersError = ec.message();
    }
    std::sort(gOptShaderFiles.begin(), gOptShaderFiles.end(),
              [](auto const& a, auto const& b) {
                  return a.filename().string() < b.filename().string();
              });
    if (!gOptShadersSelection.empty() &&
        std::none_of(gOptShaderFiles.begin(), gOptShaderFiles.end(),
                     [](auto const& p) {
                         return p.filename().string() == gOptShadersSelection;
                     }))
        gOptShadersSelection.clear();

    gOptCustomBuffersFound = false;
    for (char const* name : {"CustomBuffers", "CustomBuffer", "custombuffer"}) {
        std::error_code foundError;
        if (std::filesystem::is_directory(paths::modsDir() / name, foundError)) {
            gOptCustomBuffersFound = true;
            break;
        }
    }
    gOptShadersScanned = true;
}

static void drawOptLeviShadersScreen(ImVec2 display) {
    if (!gOptShadersScanned) ofScanShaderpacks();
    float u = ofu(display);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({0, 0}, display, ofc(0x111318, 235));
    ofNineSprite(dl, OptSprite::PanelDark,
                 {8*u, 8*u}, {display.x-8*u, display.y-8*u}, u,
                 IM_COL32(130, 136, 145, 255));

    float left = 20*u, split = display.x*0.475f;
    float right = display.x*0.505f, rightEdge = display.x-20*u;
    float listTop = 85*u, bottom = display.y-65*u;
    float leftWidth = split-left, rightWidth = rightEdge-right;
    if (leftWidth < 100*u || rightWidth < 100*u || bottom <= listTop+30*u) {
        ofText(dl, fonts::body(), {20*u, 30*u},
               "Shaders UI: screen too small", ofc(0xFFFFFF));
        if (ofButton("##shaders_small_back", "Back to Mods",
                     {20*u, display.y-49*u}, {155*u, 32*u}, u))
            gOptShadersVisible = false;
        return;
    }

    ofText(dl, fonts::bold(), {left, 17*u}, "Shaders",
           ofc(0xFFFFFF));
    ofText(dl, fonts::body(), {left, 47*u},
           "Select a shader pack (preview only)", ofc(0xD5D5D5));
    dl->AddRectFilled({left, listTop}, {split, bottom}, ofc(0x202126, 255));
    ofNineSprite(dl, OptSprite::PanelDark, {left, listTop}, {split, bottom},
                 u, IM_COL32(112, 122, 136, 255));

    ImGui::SetCursorScreenPos({left+4*u, listTop+4*u});
    ImGui::BeginChild("##shader_pack_list",
                      {leftWidth-8*u, bottom-listTop-8*u}, ImGuiChildFlags_None);
    auto row = [&](char const* id, std::string const& title, bool selected) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(id);
        bool clicked = ImGui::InvisibleButton("##shader_row", {width, 49*u});
        ImGui::PopID();
        ImDrawList* child = ImGui::GetWindowDrawList();
        child->AddRectFilled(pos, {pos.x+width,pos.y+49*u},
                             selected ? ofc(0x506A83, 245) : ofc(0x2B2E33, 255));
        if (selected)
            child->AddRect(pos,{pos.x+width,pos.y+49*u},
                           ofc(0xEEEEEE),0,0,std::max(1.0f,u));
        child->PushClipRect({pos.x+6*u,pos.y},{pos.x+width-6*u,pos.y+49*u},true);
        ofText(child,fonts::body(),{pos.x+9*u,pos.y+12*u},
               title.c_str(),ofc(0xFFFFFF),0,width-18*u);
        child->PopClipRect();
        ImGui::Dummy({0,3*u});
        return clicked;
    };
    if (row("vanilla", "Off (vanilla Minecraft)",
            gOptShadersSelection.empty())) gOptShadersSelection.clear();
    for (size_t i=0; i<gOptShaderFiles.size(); ++i) {
        auto name = gOptShaderFiles[i].filename().string();
        ImGui::PushID(int(i));
        bool selected = gOptShadersSelection == name;
        if (row("archive",name,selected)) gOptShadersSelection=name;
        ImGui::PopID();
    }
    if (gOptShaderFiles.empty())
        ImGui::TextDisabled("No .zip or .mcpack files found.");
    ImGui::EndChild();

    ofNineSprite(dl, OptSprite::PanelDark,
                 {right,listTop},{rightEdge,bottom},u,
                 IM_COL32(112,122,136,255));
    float tx = right+12*u;
    float ty = listTop+15*u;
    ofText(dl, fonts::bold(), {tx,ty}, "Shader pack details", ofc(0xFFFFFF));
    ty += 38*u;
    std::string picked = gOptShadersSelection.empty()
        ? "Vanilla / Off" : gOptShadersSelection;
    ofText(dl,fonts::body(),{tx,ty},picked.c_str(),
           ofc(0xE3E8EF),0,rightWidth-25*u);
    ty += 42*u;
    ofText(dl,fonts::body(),{tx,ty},
           "Backend: Android OpenGL ES / RenderDragon",
           ofc(0xD2D2D2),0,rightWidth-25*u);
    ty += 36*u;
    ofText(dl,fonts::body(),{tx,ty},
           gOptCustomBuffersFound ? "Custom Buffers: directory detected"
                                 : "Custom Buffers: not detected",
           ofc(gOptCustomBuffersFound ? 0xB8F0B8 : 0xE5D49C));
    ty += 36*u;
    ofText(dl,fonts::body(),{tx,ty},
           "Java Iris / Arke backend: not available on Android",
           ofc(0xE5D49C),0,rightWidth-25*u);
    ty += 42*u;
    ofText(dl,fonts::body(),{tx,ty},
           "Detection only - no shader was loaded or activated.",
           ofc(0xFFFFFF),0,rightWidth-25*u);
    ty += 38*u;
    ofText(dl,fonts::body(),{tx,ty},
           "Copy shader packs to:",ofc(0xC5C9CD));
    ty += 26*u;
    std::string folder = ofShaderpacksPath().string();
    dl->PushClipRect({tx,ty},{rightEdge-10*u,bottom-5*u},true);
    ofText(dl,fonts::body(),{tx,ty},folder.c_str(),
           ofc(0xA7C6FF),fonts::body()->FontSize*0.79f,rightWidth-25*u);
    ty += 32*u;
    if (!gOptShadersError.empty())
        ofText(dl,fonts::body(),{tx,ty},gOptShadersError.c_str(),
               ofc(0xFFAA88),0,rightWidth-25*u);
    dl->PopClipRect();

    float by=display.y-50*u, bh=33*u;
    if (ofButton("##shader_refresh","Refresh",
                 {left,by},{155*u,bh},u)) ofScanShaderpacks();
    if (ofButton("##shader_back","Back to Mods",
                 {right,by},{170*u,bh},u)) gOptShadersVisible=false;
    if (ofButton("##shader_done","Done",
                 {rightEdge-145*u,by},{145*u,bh},u)) {
        gOptShadersVisible=false;
        requestClose("Shader browser Done");
    }
}
