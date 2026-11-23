// OptLevi Fabric-screen renderer — independently implemented ImGui frontend.
//
// Do not add this file as a separate CMake translation unit: it is included
// inside ModMenuScreen.cpp's anonymous namespace after the native mod logic.
// The existing ModMenuBE registry, config, update and icon APIs remain intact.
// Inspired by the layout of user-supplied Fabric Mod Menu 21.0.1, without
// importing Java UI assets, copyrighted Minecraft fonts or Java bytecode.

static ImU32 ofc(unsigned rgb, unsigned alpha=255) {
    return col::rgb(rgb, alpha);
}

// Bedrock UI palette sampled from the supplied 26.60 UI reference archive:
 // NormalButtonStroke center ~#C6C6C6, black frame, white upper bevel;
 // ScrollHandle center ~#C6C6C6, edit_box_indent_hover ~#5E5E5E.
 // Use original ImGui primitives instead of copying bundled game PNG assets.
static constexpr unsigned kBedrockButtonFace = 0xC6C6C6;
static constexpr unsigned kBedrockButtonHover = 0xE0E0E0;
static constexpr unsigned kBedrockButtonPressed = 0x919191;
static constexpr unsigned kBedrockButtonDisabled = 0x8B8B8B;
static constexpr unsigned kBedrockPanel = 0x303137;
static constexpr unsigned kBedrockPanelDark = 0x202126;

static float ofu(ImVec2 display) {
    // Fabric reference canvas is 1096x609. Keep physical sizes reasonable on
    // Android regardless of the launcher's ImGui uiScale.
    return std::clamp(display.y / 609.0f, 0.72f, 1.38f);
}

static void ofText(ImDrawList* dl, ImFont* font, ImVec2 pos,
                   char const* text, ImU32 color, float size=0.0f,
                   float maxWidth=FLT_MAX) {
    if (!text || !*text) return;
    fonts::note(text);
    if (!font) font = ImGui::GetFont();
    float h = size > 0 ? size : font->FontSize;
    dl->AddText(font, h, pos, color, text, nullptr, maxWidth);
}

// Native Minecraft/ModMenu-like pixel button. Deliberately not OreTheme's
// 3-D themed button: this is an independent rendering path.
#include "OptLeviBedrockTexture.inc"

static bool ofButton(char const* id, char const* label, ImVec2 pos,
                     ImVec2 dims, float unit, bool enabled=true) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushID(id);
    bool clicked=ImGui::InvisibleButton("##fabric_button", dims);
    bool hovered=ImGui::IsItemHovered();
    bool held=ImGui::IsItemActive();
    ImGui::PopID();
    ImDrawList* dl=ImGui::GetWindowDrawList();
    float edge=std::max(1.0f, std::round(unit));
    ImVec2 max{pos.x+dims.x,pos.y+dims.y};
    ImVec2 imin{pos.x+edge*2,pos.y+edge*2};
    ImVec2 imax{max.x-edge*2,max.y-edge*2};
    // The actual UI source PNG is stretched using a nine-slice GPU image,
    // not approximated with a rectangle-only button.
    OptSprite sprite=!enabled?OptSprite::ButtonDisabled:
            held?(hovered?OptSprite::ButtonHoverPressed:OptSprite::ButtonPressed):
            hovered?OptSprite::ButtonHover:OptSprite::ButtonNormal;
    if(!ofNineSprite(dl,sprite,pos,max,unit)) {
        dl->AddRectFilled(pos,max,ofc(0x101010,255));
        dl->AddRectFilled(imin,imax,
            !enabled?ofc(kBedrockButtonDisabled):held?ofc(kBedrockButtonPressed):
            hovered?ofc(kBedrockButtonHover):ofc(kBedrockButtonFace));
    }
    if (label && *label) {
        ImFont* font=fonts::body();
        fonts::note(label);
        float fontSize=font?font->FontSize:ImGui::GetFontSize();
        float tw=font->CalcTextSizeA(fontSize,FLT_MAX,0,label).x;
        float x=pos.x+(dims.x-tw)*0.5f;
        float y=pos.y+(dims.y-fontSize)*0.5f+(held?edge:0);
        dl->PushClipRect(imin,imax,true);
        dl->AddText(font,fontSize,{x,y},enabled?ofc(0x202020):ofc(0x494949),label);
        dl->PopClipRect();
    }
    return clicked && enabled;
}

static void ofModEntry(ModInfo const& mod, ImVec2 pos,
                       float width, float height, float unit, bool child) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushID(mod.name.c_str());
    bool chosen=ImGui::InvisibleButton("##of_entry",{width,height});
    bool hovered=ImGui::IsItemHovered();
    ImGui::PopID();
    if(chosen)gState.selected=mod.name;
    bool selected=(gState.selected==mod.name);
    ImDrawList* dl=ImGui::GetWindowDrawList();
    float inset=child?14.0f*unit:0.0f;
    ImVec2 p{pos.x+inset,pos.y}, m{pos.x+width,pos.y+height};
    // Real nine-sliced Bedrock inset textures back every selectable row.
    if(!ofNineSprite(dl,OptSprite::PanelDark,p,m,unit,
                     IM_COL32(125,135,150,255)))
        dl->AddRectFilled(p,m,ofc(0x282B32,255));
    if(selected){
        dl->AddRectFilled(p,m,ofc(0x435679,140));
        ofNineSprite(dl,OptSprite::PanelOutline,p,m,unit);
        dl->AddRect(p,m,ofc(0xEEEEEE),0,0,std::max(1.0f,unit));
    }else if(hovered){
        dl->AddRectFilled(p,m,ofc(0x78889B,55));
    }
    float iconSize=46.0f*unit;
    ImVec2 iconPos{p.x+5.0f*unit,p.y+(height-iconSize)*0.5f};
    drawIcon(dl,mod,iconPos,iconSize);
    float tx=iconPos.x+iconSize+7.0f*unit;
    float right=m.x-7.0f*unit;
    dl->PushClipRect({tx,p.y},{right,m.y},true);
    ImFont* title=fonts::bold();
    ImFont* body=fonts::body();
    ofText(dl,title,{tx,p.y+4.0f*unit},mod.name.c_str(),
           mod.enabled?ofc(0xFFFFFF):ofc(0xC9CCD0),0.0f,right-tx);
    // Fabric's subtle Client/Library badge occupies the name row, not a
    // separate line of large enabled/disabled badges.
    if(!mod.type.empty()){
        char const* tag=(mod.type.find("preload")!=std::string::npos)?"Client":
                        (mod.type.find("library")!=std::string::npos)?"Library":nullptr;
        if(tag){
            float nameW=title->CalcTextSizeA(title->FontSize,FLT_MAX,0,mod.name.c_str()).x;
            float bw=body->CalcTextSizeA(body->FontSize*0.79f,FLT_MAX,0,tag).x+6.0f*unit;
            float bx=tx+nameW+8.0f*unit;
            if(bx+bw<right){
                dl->AddRectFilled({bx,p.y+5.0f*unit},
                                  {bx+bw,p.y+21.0f*unit},ofc(0x435365,237));
                ofText(dl,body,{bx+3.0f*unit,p.y+5.0f*unit},tag,
                       ofc(0xC7E8FF),body->FontSize*0.79f);
            }
        }
    }
    std::string desc=descriptionFor(mod);
    if(desc.empty())desc=mod.type;
    if(!desc.empty()){
        // Clip to two readable, compact lines, not arbitrary status panels.
        std::string first=desc.substr(0,desc.find('\n'));
        ofText(dl,body,{tx,p.y+28.0f*unit},first.c_str(),ofc(0xCDCFD2),
               body->FontSize*0.88f,right-tx);
    }
    dl->PopClipRect();
}

static void ofDetailPane(ModInfo const& mod, ImVec2 start,
                         ImVec2 end, float u) {
    ImDrawList* dl=ImGui::GetWindowDrawList();
    float width=end.x-start.x;
    float headerY=start.y+5.0f*u;
    float icon=56.0f*u;
    drawIcon(dl,mod,{start.x+6.0f*u,headerY},icon);
    float nameX=start.x+(icon+14.0f)*u;
    float right=end.x-5.0f*u;
    dl->PushClipRect({nameX,headerY},{right,headerY+icon+6.0f*u},true);
    ofText(dl,fonts::bold(),{nameX,headerY+0.0f*u},mod.name.c_str(),ofc(0xFFFFFF));
    if(!mod.version.empty()){
        std::string ver=fmt(tr("version"),mod.version.c_str());
        ofText(dl,fonts::body(),{nameX,headerY+21.0f*u},ver.c_str(),ofc(0xCACDCE));
    }
    if(!mod.authors.empty()){
        std::string authors=fmt(tr("by"),joined(mod.authors).c_str());
        ofText(dl,fonts::body(),{nameX,headerY+41.0f*u},authors.c_str(),ofc(0xC7C9CB),
               fonts::body()->FontSize*0.85f,right-nameX);
    }
    dl->PopClipRect();

    // Enrich hyperlinks with known catalog fields without inventing URLs.
    auto entry=lipEntry(mod);
    auto detail=entry?LipCatalog::instance().detail(entry->tooth):nullptr;
    std::map<std::string,std::string> links=mod.links;
    std::string license=mod.license;
    if(entry){
        if(license.empty()) license=entry->license;
        if(!entry->page.empty()) links.try_emplace("levimc",entry->page);
    }
    if(detail){
        if(license.empty())license=detail->license;
        if(!detail->repository.empty())links.try_emplace("source",detail->repository);
        if(!detail->issues.empty())links.try_emplace("issues",detail->issues);
    }
    auto link=[&](std::initializer_list<char const*> keys)->std::string{
        for(auto key:keys){
            auto it=links.find(key);
            if(it!=links.end()&&!it->second.empty())return it->second;
        }
        return {};
    };
    std::string site=link({"website","homepage","source","levimc"});
    std::string issues=link({"issues","bugs"});
    float actionsY=start.y+72.0f*u;
    float gap=7.0f*u;
    float each=(width-gap-12.0f*u)*0.5f;
    if(ofButton("##of_website","Website",{start.x+5.0f*u,actionsY},
                {each,32.0f*u},u,!site.empty()))openExternal(site);
    if(ofButton("##of_issues","Issues",{start.x+5.0f*u+each+gap,actionsY},
                {each,32.0f*u},u,!issues.empty()))openExternal(issues);

    float contentY=start.y+110.0f*u;
    dl->AddRectFilled({start.x+3.0f*u,contentY},{end.x,end.y},
                      ofc(kBedrockPanelDark,255));
    ofNineSprite(dl,OptSprite::PanelDark,
                 {start.x+3.0f*u,contentY},{end.x,end.y},u,
                 IM_COL32(112,121,136,255));
    ImGui::SetCursorScreenPos({start.x+8.0f*u,contentY+8.0f*u});
    ImGui::BeginChild("##of_details_scroll",{width-16.0f*u,
                     std::max(20.0f,end.y-contentY-14.0f*u)},
                     ImGuiChildFlags_None);
    std::string desc=descriptionFor(mod);
    if(!desc.empty()){
        fonts::note(desc);
        ImGui::TextWrapped("%s",desc.c_str());
        ImGui::Dummy({0,4.0f*u});
    }
    if(!links.empty()){
        ImGui::TextDisabled("Links:");
        for(auto const& [key,url]:links){
            if(url.empty())continue;
            ImGui::PushID(key.c_str());
            std::string text=key;
            fonts::note(text);
            if(ImGui::Selectable(text.c_str(),false))openExternal(url);
            ore::tooltip(url.c_str());
            ImGui::PopID();
        }
        ImGui::Dummy({0,4.0f*u});
    }
    if(!license.empty()){
        ImGui::TextDisabled("License:");
        fonts::note(license);
        ImGui::TextWrapped("%s",license.c_str());
        ImGui::Dummy({0,5.0f*u});
    }
    if(!mod.authors.empty()){
        ImGui::TextDisabled("Credits:");
        std::string creators=joined(mod.authors);
        fonts::note(creators);
        ImGui::TextWrapped("Authors: %s",creators.c_str());
        ImGui::Dummy({0,5.0f*u});
    }

    // Bedrock-specific controls are not present in Java Fabric's list.
    // Keep them in a collapsible secondary section rather than dominating
    // the details area.
    if(ImGui::CollapsingHeader("Mod settings")){
        bool configurable=hasConfig(mod)&&!gState.faultedScreens.count(mod.name);
        float configWidth=120.0f*u;
        ImVec2 loc=ImGui::GetCursorScreenPos();
        if(ofButton("##of_config","Configure",loc,{configWidth,30.0f*u},u,configurable))
            openConfigFor(mod);
        ImGui::SameLine(0,7.0f*u);
        loc=ImGui::GetCursorScreenPos();
        if(ofButton("##of_enable",mod.enabled?"Disable":"Enable",loc,
                    {110.0f*u,30.0f*u},u,!mod.self)){
            if(mod.name=="LeviLamina" && mod.enabled)gState.confirmDisableLL=true;
            else toggleEnabled(mod);
        }
        ImGui::Dummy({0,6.0f*u});
        auto result=UpdateChecker::instance().result(mod.name);
        if(result.state==UpdateResult::State::Available){
            ImGui::TextDisabled("Update available");
            if(!result.url.empty()&&ImGui::SmallButton("Open update"))
                openExternal(result.url);
        }
        if(ImGui::CollapsingHeader("Technical information")){
            ImGui::TextWrapped("Type: %s",mod.type.c_str());
            ImGui::TextWrapped("State: %s",mod.enabled?"Enabled":"Disabled");
            if(!mod.entry.empty())ImGui::TextWrapped("Entry: %s",mod.entry.c_str());
        }
    }
    drawReadme(mod);
    float detailsScroll=ImGui::GetScrollY();
    float detailsMax=ImGui::GetScrollMaxY();
    ImGui::EndChild();
    ofTexturedScrollbar(dl,{end.x-12*u,contentY+3*u},
                        {end.x-4*u,end.y-6*u},detailsScroll,detailsMax,u);
}

#include "OptLeviShaderScreen.inc"

static void drawOptLeviFabricScreen(ImVec2 display) {
    if (gOptShadersVisible) {
        drawOptLeviShadersScreen(display);
        return;
    }
    float u=ofu(display);
    ImDrawList* dl=ImGui::GetWindowDrawList();
    // Minecraft-like overlay rather than AFAlimru's large framed dialog.
    dl->AddRectFilled({0,0},display,ofc(0x101116,215));
    // Minecraft's menu texture reference uses a crisp panel outline, not
    // transparent cards exposing game buttons behind their text.
    dl->AddRectFilled({8*u,8*u},{display.x-8*u,display.y-9*u},
                      ofc(kBedrockPanel,251));
    ofNineSprite(dl,OptSprite::PanelDark,
                 {8*u,8*u},{display.x-8*u,display.y-9*u},u,
                 IM_COL32(135,140,148,255));
    dl->AddRect({8*u,8*u},{display.x-8*u,display.y-9*u},
                ofc(0x101010,255),0,0,std::max(1.0f,u));
    float xL=20.0f*u, xR=display.x*0.515f;
    float leftEnd=display.x*0.484f, rightEnd=display.x-20.0f*u;
    float leftW=leftEnd-xL, rightW=rightEnd-xR;
    float top=12.0f*u;
    float bottom=display.y-57.0f*u;
    // Opens the independently rendered Iris-inspired shaderpack browser.
    // The existing Mods controls and native registry remain unchanged.
    if (ofButton("##of_shaders","Shaders",
                 {xR+10.0f*u,top},{185.0f*u,36.0f*u},u)) {
        gOptShadersVisible=true;
        ofScanShaderpacks();
        drawOptLeviShadersScreen(display);
        return;
    }
    // Scoped scrollbar colors mimic the supplied Bedrock scroll track.
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,ImGui::ColorConvertU32ToFloat4(ofc(0x151515,255)));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,ImGui::ColorConvertU32ToFloat4(ofc(0xC6C6C6,255)));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,ImGui::ColorConvertU32ToFloat4(ofc(0xE2E2E2,255)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::ColorConvertU32ToFloat4(ofc(0x25272C,255)));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,std::max(9.0f,10.0f*u));

    // Distinct title centered above the list, as in Fabric ModsScreen.
    char const* heading="Mods";
    fonts::note(heading);
    ImFont* title=fonts::bold();
    float h=title->FontSize;
    float tw=title->CalcTextSizeA(h,FLT_MAX,0,heading).x;
    // A Bedrock-like slim gray title strip within the Fabric two-pane layout.
    dl->AddRectFilled({xL,top-2*u},{leftEnd,top+24*u},ofc(0xC6C6C6,255));
    ofNineSprite(dl,OptSprite::PanelLight,
                 {xL,top-2*u},{leftEnd,top+24*u},u);
    dl->AddRectFilled({xL,top-2*u},{leftEnd,top},ofc(0xFFFFFF,255));
    dl->AddRectFilled({xL,top+23*u},{leftEnd,top+24*u},ofc(0x484848,255));
    dl->AddText(title,h,{xL+leftW*0.5f-tw*0.5f,top},ofc(0x222222),heading);

    float searchY=39.0f*u, searchH=33.0f*u;
    float filterW=33.0f*u, searchW=leftW-filterW-7.0f*u;
    ImGui::SetCursorScreenPos({xL,searchY});
    ofNineSprite(dl,OptSprite::Input,{xL,searchY},
                 {xL+searchW,searchY+searchH},u,IM_COL32(95,95,95,255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImGui::ColorConvertU32ToFloat4(ofc(0x080808,0)));
    ImGui::PushStyleColor(ImGuiCol_Border,ImGui::ColorConvertU32ToFloat4(ofc(0xCCCCCC,0)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,std::max(1.0f,u));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2{5.0f*u,7.0f*u});
    ImGui::SetNextItemWidth(searchW);
    if(ImGui::InputTextWithHint("##of_search","Search mods...",
                                gState.search,sizeof(gState.search)))
        fonts::note(gState.search);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImVec2 filterPos{xL+leftW-filterW,searchY};
    if(ofButton("##of_filter","",filterPos,{filterW,searchH},u))
        ImGui::OpenPopup("##of_filters");
    // Pixel funnel drawn from ImGui primitives, no Java sprite copy.
    ImVec2 center{filterPos.x+filterW*0.5f,filterPos.y+searchH*0.47f};
    ImVec2 wedge[4]={{center.x-9*u,center.y-7*u},
                     {center.x+9*u,center.y-7*u},
                     {center.x+2*u,center.y+2*u},
                     {center.x-2*u,center.y+2*u}};
    dl->AddConvexPolyFilled(wedge,4,ofc(0x282828));
    dl->AddRectFilled({center.x-2*u,center.y+1*u},
                      {center.x+2*u,center.y+9*u},ofc(0x282828));
    ImGui::SetNextWindowPos({filterPos.x-100.0f*u,searchY+searchH+2.0f*u},
                            ImGuiCond_Appearing);
    if(ImGui::BeginPopup("##of_filters")){
        bool libs=settings().showLibraries;
        if(ofTexturedCheckbox("##atlas_libraries","Show libraries",libs,u)){
            settings().showLibraries=libs;
            settings().save();
        }
        char const* keys[]={"sort.name_asc","sort.name_desc","sort.loaded"};
        int sort=std::clamp(settings().sortMode.load(),0,2);
        if(ImGui::BeginCombo("Sort",tr(keys[sort]))){
            for(int i=0;i<3;++i)
                if(ImGui::Selectable(tr(keys[i]),sort==i)){
                    settings().sortMode=i;
                    settings().save();
                }
            ImGui::EndCombo();
        }
        if(ImGui::Button("Check updates"))UpdateChecker::instance().checkAll();
        ImGui::EndPopup();
    }

    auto all=ModRegistry::instance().mods();
    auto mods=visibleMods(*all);
    char count[64];
    snprintf(count,sizeof(count),"Showing %d mods",(int)mods.size());
    ofText(dl,fonts::body(),{xL,88.0f*u},count,ofc(0xFFFFFF));
    float listY=111.0f*u;
    // Fabric list has its own scroll and a dark but see-through background.
    dl->AddRectFilled({xL,listY},{leftEnd,bottom},ofc(kBedrockPanelDark,255));
    ofNineSprite(dl,OptSprite::PanelDark,{xL,listY},{leftEnd,bottom},u,
                 IM_COL32(124,135,146,255));
    dl->AddRect({xL,listY},{leftEnd,bottom},ofc(0x101010,255),0,0,std::max(1.0f,u));
    ImGui::SetCursorScreenPos({xL,listY});
    ImGui::BeginChild("##of_scroll_mods",{leftW,bottom-listY},ImGuiChildFlags_None);
    float entryHeight=59.0f*u;
    for(auto const& mod:mods){
        bool child=!mod->parent.empty()
            && std::any_of(mods.begin(),mods.end(),[&](auto const& possible){
                 return possible->name==mod->parent;
            });
        ImVec2 entryPos=ImGui::GetCursorScreenPos();
        ofModEntry(*mod,entryPos,ImGui::GetContentRegionAvail().x-1.0f*u,
                   entryHeight,u,child);
        ImGui::Dummy({0,2.0f*u});
    }
    if(mods.empty())ImGui::TextDisabled("No mods match the search.");
    float listScroll=ImGui::GetScrollY();
    float listScrollMax=ImGui::GetScrollMaxY();
    ImGui::EndChild();
    ofTexturedScrollbar(dl,{leftEnd-12*u,listY+3*u},
                        {leftEnd-4*u,bottom-5*u},listScroll,listScrollMax,u);

    // Details panel: header + two buttons + scrolling description/credits.
    // No white AFAlimru shell, huge Config/Enable strip or oversized footer.
    dl->AddRectFilled({xR,68.0f*u},{rightEnd,bottom},ofc(kBedrockPanelDark,255));
    ofNineSprite(dl,OptSprite::PanelDark,
                 {xR,68.0f*u},{rightEnd,bottom},u,
                 IM_COL32(112,121,136,255));
    dl->AddRect({xR,68.0f*u},{rightEnd,bottom},ofc(0x101010,255),0,0,std::max(1.0f,u));
    auto selected=ModRegistry::instance().find(gState.selected);
    if(!selected&&!mods.empty()){
        selected=mods.front();
        gState.selected=selected->name;
    }
    if(selected)ofDetailPane(*selected,{xR,68.0f*u},{rightEnd,bottom},u);
    else ofText(dl,fonts::body(),{xR+10*u,90*u},"Select a mod",ofc(0xCCCCCC));

    // Only two main actions across the bottom, matching Java Fabric's layout.
    float buttonW=std::min(leftW*0.59f,345.0f*u);
    float by=display.y-45.0f*u, bh=33.0f*u;
    if(ofButton("##of_refresh","Refresh",{leftEnd-buttonW-5*u,by},
                {buttonW,bh},u))ModRegistry::instance().rescan();
    if(ofButton("##of_done","Done",{xR+5.0f*u,by},
                {buttonW,bh},u))requestClose("Fabric Done");
    if(ModRegistry::instance().needsRestart())
        ofText(dl,fonts::body(),{xL,display.y-57.0f*u},
               "Restart Minecraft to apply changes",ofc(0xFFFF77));
    if(!gState.toast.empty() && ImGui::GetTime()<gState.toastUntil)
        ofText(dl,fonts::body(),{xL,display.y-57.0f*u},
               gState.toast.c_str(),ofc(0xFFFF99));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}
