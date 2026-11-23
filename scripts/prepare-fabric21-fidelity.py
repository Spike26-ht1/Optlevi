#!/usr/bin/env python3
"""Create a Fabric Mod Menu 21-inspired Android ImGui interface.

Reference: user-supplied modmenu-21.0.1.jar (MIT), ModsScreen and ModListWidget
behavior and layout. This is an independently written Bedrock/ImGui adaptation,
not a decompiled Java class or shipped Java bytecode. Apply after prepare-fabric-layout.py.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ModMenuBE" / "src" / "ui"
SCREEN = SRC / "ModMenuScreen.cpp"


def replace_once(text, original, replacement, name):
    n = text.count(original)
    if n != 1:
        raise RuntimeError(f"{name}: expected one instance, found {n}")
    return text.replace(original, replacement)


def replace_section(text, start, end, value):
    a = text.find(start)
    b = text.find(end, a + len(start))
    if a < 0 or b < 0 or text.find(start, a + 1) >= 0:
        raise RuntimeError(f"Function markers invalid: {start!r} ... {end!r}")
    return text[:a] + value.rstrip() + "\n\n" + text[b:]


CARD = r'''
void drawModCard(ModInfo const& mod, float width, bool child) {
    // Standard Fabric-like list entry: icon, name/version, status and summary.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float height = s(76);
    float indent = child ? s(18) : 0.0f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(mod.name.c_str());
    bool pressed = ImGui::InvisibleButton("mod_entry", {width, height});
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    if (pressed) gState.selected = mod.name;
    bool selected = gState.selected == mod.name;

    ImVec2 min{pos.x + indent, pos.y}, max{pos.x + width, pos.y + height};
    ImU32 bg = selected ? col::rgb(0x465062, 250)
                : hovered ? col::rgb(0x383E49, 245) : col::rgb(0x242831, 229);
    dl->AddRectFilled(min, max, bg);
    if (selected) {
        dl->AddRect(min, max, col::rgb(0xDBDFE7), 0, 0, s(1));
        dl->AddRectFilled(min, {min.x + s(3), max.y}, col::rgb(0xD2D7E0));
    }
    if (child)
        dl->AddRectFilled({pos.x + s(5), pos.y + s(7)},
                          {pos.x + s(7), pos.y + height - s(7)}, col::PanelEdge);

    float iconSize = s(52);
    ImVec2 iconPos{min.x + s(9), min.y + s(7)};
    drawIcon(dl, mod, iconPos, iconSize);

    float tx = iconPos.x + iconSize + s(10);
    float available = std::max(s(40), max.x - tx - s(10));
    dl->PushClipRect({tx, min.y + s(2)}, {max.x - s(7), max.y - s(3)}, true);
    ImFont* title = fonts::bold();
    fonts::note(mod.name);
    dl->AddText(title, title->FontSize * 0.94f, {tx, min.y + s(6)},
                mod.enabled ? col::Text : col::TextDim, mod.name.c_str());
    float nameWidth = title->CalcTextSizeA(title->FontSize * 0.94f, FLT_MAX,
                                         0, mod.name.c_str()).x;
    if (!mod.version.empty() && nameWidth < available * 0.68f) {
        fonts::note(mod.version);
        dl->AddText(fonts::body(), fonts::body()->FontSize * 0.80f,
                    {tx + nameWidth + s(10), min.y + s(8)},
                    col::TextDim, mod.version.c_str());
    }

    // Badges belong alongside the heading; the summary remains visible.
    drawBadges(dl, mod, {tx, min.y + s(28)}, max.x - s(8), true);
    std::string description = descriptionFor(mod);
    if (!description.empty()) {
        fonts::note(description);
        dl->AddText(fonts::body(), fonts::body()->FontSize * 0.82f,
                    {tx, min.y + s(47)}, col::TextDim,
                    description.c_str(), nullptr, available);
    }
    dl->PopClipRect();
    ImGui::Dummy({0, s(2)});
}
'''

DETAILS = r'''
void drawDetails(ModInfo const& mod) {
    // Fabric-like details: large icon/name, two link actions, description,
    // editable mod actions, README and expandable technical metadata.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float width = ImGui::GetContentRegionAvail().x;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const float iconSize = s(74);
    drawIcon(dl, mod, pos, iconSize);
    float tx = pos.x + iconSize + s(13);
    float textWidth = std::max(s(80), width - iconSize - s(18));
    dl->PushClipRect({tx, pos.y}, {pos.x + width, pos.y + s(116)}, true);
    fonts::note(mod.name);
    ImFont* title = fonts::title();
    dl->AddText(title, title->FontSize, {tx, pos.y + s(2)}, col::Text, mod.name.c_str());
    float y = pos.y + title->FontSize + s(3);
    if (!mod.version.empty()) {
        std::string v = fmt(tr("version"), mod.version.c_str());
        fonts::note(v);
        dl->AddText({tx, y}, col::TextDim, v.c_str());
        y += ImGui::GetFontSize() + s(2);
    }
    if (!mod.authors.empty()) {
        std::string authors = fmt(tr("by"), joined(mod.authors).c_str());
        fonts::note(authors);
        dl->AddText({tx, y}, col::TextDim, authors.c_str());
        y += ImGui::GetFontSize() + s(2);
    }
    dl->PopClipRect();

    // Prefer project-supplied manifest links; fill missing metadata from the catalog.
    auto entry = lipEntry(mod);
    auto detail = entry ? LipCatalog::instance().detail(entry->tooth) : nullptr;
    std::map<std::string, std::string> links = mod.links;
    std::string license = mod.license;
    if (entry) {
        if (license.empty()) license = entry->license;
        if (!entry->page.empty()) links.try_emplace("levimc", entry->page);
    }
    if (detail) {
        if (license.empty()) license = detail->license;
        if (!detail->repository.empty()) links.try_emplace("source", detail->repository);
        if (!detail->issues.empty()) links.try_emplace("issues", detail->issues);
    }

    auto linkUrl = [&](std::initializer_list<char const*> keys) -> std::string {
        for (char const* key : keys) {
            auto it = links.find(key);
            if (it != links.end() && !it->second.empty()) return it->second;
        }
        return {};
    };
    std::string homepage = linkUrl({"website", "homepage", "source", "levimc"});
    std::string issues = linkUrl({"issues", "bugs"});

    ImGui::SetCursorScreenPos({pos.x, std::max(pos.y + iconSize, y) + s(14)});
    float half = std::max(s(70), (width - s(9)) * 0.5f);
    if (ore::button("##fabric_website", "Website", ore::Variant::Secondary,
                    {half, s(32)}, homepage.empty())) openExternal(homepage);
    ImGui::SameLine(0, s(9));
    if (ore::button("##fabric_issues", "Issues", ore::Variant::Secondary,
                    {half, s(32)}, issues.empty())) openExternal(issues);
    ImGui::Dummy({0, s(9)});
    ImGui::Separator();
    ImGui::Dummy({0, s(4)});

    if (std::string desc = descriptionFor(mod); !desc.empty()) {
        fonts::note(desc);
        ore::textWrapped(desc.c_str(), col::Text);
        ImGui::Dummy({0, s(10)});
    }

    // Bedrock-native actions stay available; Fabric does not provide
    // identical runtime enable/disable controls.
    bool configurable = hasConfig(mod) && !gState.faultedScreens.count(mod.name);
    if (ore::button("##fabric_config", "Configure", ore::Variant::Secondary,
                    {s(130), s(34)}, !configurable)) openConfigFor(mod);
    ImGui::SameLine(0, s(10));
    bool isLL = mod.name == "LeviLamina";
    if (ore::button("##fabric_toggle", mod.enabled ? tr("disable") : tr("enable"),
                    mod.enabled ? ore::Variant::Dark : ore::Variant::Primary,
                    {s(110), s(34)}, mod.self)) {
        if (isLL && mod.enabled) gState.confirmDisableLL = true;
        else toggleEnabled(mod);
    }
    if (mod.self) ore::tooltip(tr("self_disable"));

    auto upd = UpdateChecker::instance().result(mod.name);
    std::string updateText;
    switch (upd.state) {
    case UpdateResult::State::Available: updateText = fmt(tr("update.available"), upd.latest.c_str()); break;
    case UpdateResult::State::NeedsNewerGame:
        updateText = fmt(tr("update.newer_game"), upd.newestAny.c_str()); break;
    case UpdateResult::State::Checking: updateText = tr("update.checking"); break;
    case UpdateResult::State::Error: updateText = fmt(tr("update.error"), upd.error.c_str()); break;
    case UpdateResult::State::UpToDate: updateText = tr("update.uptodate"); break;
    default: break;
    }
    if (!updateText.empty()) {
        ImGui::Dummy({0, s(8)});
        fonts::note(updateText);
        ore::textWrapped(updateText.c_str(), col::TextDim);
        if (!upd.url.empty() && (upd.state == UpdateResult::State::Available ||
                                 upd.state == UpdateResult::State::NeedsNewerGame)) {
            if (ore::button("##fabric_update", tr("update.open"), ore::Variant::Dark))
                openExternal(upd.url);
        }
    }

    ImGui::Dummy({0, s(11)});
    if (!links.empty()) {
        ore::text(tr("links"), col::TextDim, fonts::bold());
        for (auto const& [key, url] : links) {
            if (key == "website" || key == "homepage" || key == "issues") continue;
            if (url.empty()) continue;
            ImGui::PushID(key.c_str());
            std::string labelKey = "link." + key;
            char const* translated = tr(labelKey.c_str());
            std::string label = !translated || std::string(translated) == labelKey ? key : translated;
            fonts::note(label);
            if (ore::button("##fabric_link", label.c_str(), ore::Variant::Dark))
                openExternal(url);
            ore::tooltip(url.c_str());
            ImGui::SameLine(0, s(5));
            ImGui::PopID();
        }
        ImGui::NewLine();
        ImGui::Dummy({0, s(4)});
    }

    if (!license.empty()) {
        ore::text(tr("license"), col::TextDim, fonts::bold());
        fonts::note(license);
        ore::textWrapped(license.c_str());
        ImGui::Dummy({0, s(7)});
    }
    if (!mod.contributors.empty()) {
        ore::text(tr("contributors"), col::TextDim, fonts::bold());
        std::string contributors = joined(mod.contributors);
        fonts::note(contributors);
        ore::textWrapped(contributors.c_str());
        ImGui::Dummy({0, s(7)});
    }

    if (ImGui::CollapsingHeader("Technical details")) {
        if (ImGui::BeginTable("technical", 2, ImGuiTableFlags_SizingStretchProp)) {
            auto row = [](char const* key, std::string const& value) {
                if (value.empty()) return;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ore::text(key, col::TextDim);
                ImGui::TableNextColumn();
                fonts::note(value);
                ore::textWrapped(value.c_str());
            };
            std::string state = !mod.enabled ? tr("state.disabled")
                : (mod.loaded || mod.self) ? tr("state.loaded") : tr("state.not_loaded");
            row(tr("state"), state);
            row(tr("type"), mod.type);
            row(tr("entry"), mod.entry);
            row(tr("folder"), pathUtf8(mod.folder));
            ImGui::EndTable();
        }
    }
    drawReadme(mod);
}
'''

HEADER = r'''
void drawHeader(ImVec2 min, float width, float height, char const* titleText, bool showBack,
                char const* subtitle = nullptr) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Fabric's screen title floats above the list, not in an oversized header slab.
    float titleX = min.x + width * 0.25f;
    if (showBack) {
        titleX = min.x + s(115);
        ImGui::SetCursorScreenPos({min.x + s(6), min.y + s(2)});
        if (ore::button("##fabric_back", tr("back"), ore::Variant::Dark,
                        {s(92), s(29)})) finishConfig();
    }
    fonts::note(titleText);
    ImFont* title = fonts::title();
    float tw = title->CalcTextSizeA(title->FontSize, FLT_MAX, 0, titleText).x;
    dl->AddText(title, title->FontSize, {titleX - (showBack ? 0 : tw * 0.5f),
                    min.y + (height - title->FontSize) * 0.5f}, col::Text, titleText);
    // The mod count is displayed above the list rather than next to the title.
    (void)subtitle;
    ImGui::SetCursorScreenPos({min.x + width - s(39), min.y + s(2)});
    if (ore::iconButton("##fabric_close", "X", ore::Variant::Dark, s(29)))
        requestClose("close button");
}
'''

LIST_VIEW = r'''
void drawListView(ImVec2 bodyMin, ImVec2 bodyMax) {
    auto all = ModRegistry::instance().mods();
    auto mods = visibleMods(*all);
    float total = bodyMax.x - bodyMin.x;
    float gap = s(15);
    float leftW = std::max(s(250), total * 0.5f - gap * 0.5f);
    float h = bodyMax.y - bodyMin.y;

    // Fabric uses a search row, a filter row and a modest count line.
    ImGui::SetCursorScreenPos(bodyMin);
    ImGui::BeginChild("fabric_left", {leftW, h}, ImGuiChildFlags_None);
    float aw = ImGui::GetContentRegionAvail().x;
    float filterWidth = s(37);
    float searchWidth = std::max(s(90), aw - filterWidth - s(8));
    ore::searchField("##fabric_search", gState.search, sizeof(gState.search),
                     tr("search"), searchWidth);
    ImGui::SameLine(0, s(7));
    static bool showFilters = true;
    if (ore::button("##fabric_filters", "", ore::Variant::Dark,
                    {filterWidth, s(35)})) showFilters = !showFilters;
    // Draw an original small funnel directly in ImGui, matching Fabric's
    // icon-only filter button without embedding Java-edition sprite assets.
    ImVec2 fmin = ImGui::GetItemRectMin();
    ImVec2 fmax = ImGui::GetItemRectMax();
    ImVec2 center{(fmin.x + fmax.x) * 0.5f, (fmin.y + fmax.y) * 0.5f};
    ImVec2 funnel[4] = {
        {center.x - s(10), center.y - s(7)},
        {center.x + s(10), center.y - s(7)},
        {center.x + s(3), center.y + s(2)},
        {center.x - s(3), center.y + s(2)}
    };
    ImGui::GetWindowDrawList()->AddConvexPolyFilled(funnel, 4, col::Text);
    ImGui::GetWindowDrawList()->AddRectFilled(
        {center.x - s(3), center.y + s(1)},
        {center.x + s(3), center.y + s(10)}, col::Text);
    ore::tooltip("Filter options");

    if (showFilters) {
        bool showLibraries = settings().showLibraries;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {s(4), s(5)});
        if (ImGui::Checkbox("Show libraries##fabric_libs", &showLibraries)) {
            settings().showLibraries = showLibraries;
            settings().save();
        }
        ImGui::SameLine(0, s(14));
        char const* sortKeys[] = {"sort.name_asc", "sort.name_desc", "sort.loaded"};
        int selectedSort = std::clamp(settings().sortMode.load(), 0, 2);
        ImGui::SetNextItemWidth(std::min(s(154), aw * 0.33f));
        if (ImGui::BeginCombo("##fabric_sort", tr(sortKeys[selectedSort]))) {
            for (int i = 0; i < 3; ++i) {
                if (ImGui::Selectable(tr(sortKeys[i]), selectedSort == i)) {
                    settings().sortMode = i;
                    settings().save();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
    }

    ImGui::Dummy({0, s(6)});
    ImGui::TextDisabled("Showing %d mods", static_cast<int>(mods.size()));
    ImGui::Separator();
    ImGui::Dummy({0, s(2)});
    ImGui::BeginChild("fabric_mod_rows", {0, 0}, ImGuiChildFlags_None);
    float cardW = ImGui::GetContentRegionAvail().x - s(1);
    if (mods.empty()) ore::textWrapped(tr("no_mods"), col::TextDim);
    for (auto const& m : mods) {
        bool child = !m->parent.empty()
            && std::any_of(mods.begin(), mods.end(), [&](auto const& parent) {
                return parent->name == m->parent;
            });
        drawModCard(*m, cardW, child);
    }
    ImGui::EndChild();
    ImGui::EndChild();

    // Right detail pane is visually separate but uses the same subdued palette.
    ImVec2 rightMin{bodyMin.x + leftW + gap, bodyMin.y};
    ImVec2 rightMax{bodyMax.x, bodyMax.y};
    ImGui::GetWindowDrawList()->AddRectFilled(rightMin, rightMax, col::rgb(0x1B1F27, 245));
    ImGui::SetCursorScreenPos({rightMin.x + s(15), rightMin.y + s(13)});
    ImGui::BeginChild("fabric_mod_details", {std::max(s(100), rightMax.x - rightMin.x - s(28)),
                     std::max(s(120), h - s(24))}, ImGuiChildFlags_None);
    static std::string lastMod;
    if (lastMod != gState.selected) {
        ImGui::SetScrollY(0);
        lastMod = gState.selected;
    }
    auto selected = ModRegistry::instance().find(gState.selected);
    if (!selected && !mods.empty()) {
        gState.selected = mods.front()->name;
        selected = mods.front();
    }
    if (selected) drawDetails(*selected);
    else ore::textWrapped(tr("no_selection"), col::TextDim);
    ImGui::EndChild();
}
'''

FOOTER = r'''
void drawFooter(ImVec2 min, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float by = min.y + (height - s(34)) * 0.5f;
    if (gState.view == View::List) {
        // Fabric puts Done in a stable centered location; peripheral actions
        // remain accessible without dominating the screen.
        ImGui::SetCursorScreenPos({min.x + width * 0.5f - s(98), by});
        if (ore::button("##fabric_done", tr("done"), ore::Variant::Secondary,
                        {s(196), s(34)})) requestClose("Done");

        ImGui::SetCursorScreenPos({min.x + s(5), by});
        if (ore::button("##fabric_refresh", tr("refresh"), ore::Variant::Dark,
                        {s(120), s(34)}))
            ModRegistry::instance().rescan();

        ImGui::SetCursorScreenPos({min.x + width - s(153), by});
        bool running = UpdateChecker::instance().running();
        if (ore::button("##fabric_updates",
                        running ? tr("checking_updates") : tr("check_updates"),
                        ore::Variant::Dark, {s(150), s(34)}, running))
            UpdateChecker::instance().checkAll();
    } else if (gState.view == View::Config) {
        ImGui::SetCursorScreenPos({min.x + width * 0.5f - s(86), by});
        if (ore::button("##fabric_config_done", tr("done"), ore::Variant::Secondary,
                        {s(172), s(34)})) finishConfig();
    } else if (gState.view == View::Json && !gState.docs.empty()) {
        auto& doc = gState.docs[size_t(std::clamp(gState.docIndex, 0, int(gState.docs.size()) - 1))];
        ImGui::SetCursorScreenPos({min.x + width * 0.5f - s(130), by});
        if (ore::button("##fabric_save", tr("save"), ore::Variant::Primary,
                        {s(125), s(34)}, !doc.dirty())) {
            if (doc.save()) showToast(tr("saved"));
            else showToast(doc.error());
        }
        ImGui::SetCursorScreenPos({min.x + width * 0.5f + s(5), by});
        if (ore::button("##fabric_revert", tr("revert"), ore::Variant::Dark,
                        {s(125), s(34)}, !doc.dirty())) doc.revert();
    }
    // Alerts remain visible when relevant, but remove the old F8/HUD hint
    // (inapplicable to the user's Android touchscreen).
    std::string status;
    if (!gState.toast.empty() && ImGui::GetTime() < gState.toastUntil) status = gState.toast;
    else if (ModRegistry::instance().needsRestart()) status = tr("restart_required");
    if (!status.empty()) {
        fonts::note(status);
        dl->PushClipRect(min, {min.x + width * 0.24f, min.y + height}, true);
        dl->AddText({min.x + s(3), min.y + s(4)}, col::Yellow, status.c_str());
        dl->PopClipRect();
    }
}
'''


def main():
    src = SCREEN.read_text(encoding="utf-8")
    src = replace_section(src, "void drawModCard(ModInfo const& mod, float width, bool child) {",
                          "// ---------------------------------------------------------------- details", CARD)
    src = replace_section(src, "void drawDetails(ModInfo const& mod) {",
                          "// ---------------------------------------------------------------- option cards", DETAILS)
    src = replace_section(src, "void drawHeader(ImVec2 min, float width, float height, char const* titleText, bool showBack,",
                          "void drawListView(ImVec2 bodyMin, ImVec2 bodyMax) {", HEADER)
    src = replace_section(src, "void drawListView(ImVec2 bodyMin, ImVec2 bodyMax) {",
                          "void drawFooter(ImVec2 min, float width, float height) {", LIST_VIEW)
    src = replace_section(src, "void drawFooter(ImVec2 min, float width, float height) {",
                          "void drawConfirm() {", FOOTER)

    # Keep background subdued but opaque enough to read over bright game scenery.
    src = replace_once(src, "dl->AddRectFilled(min, max, col::rgb(0x15171C, 188));",
                       "dl->AddRectFilled(min, max, col::rgb(0x262A31, 245));",
                       "shell fill")
    src = replace_once(src, "dl->AddRectFilled({0, 0}, display, col::rgb(0x11131A, 120));",
                       "dl->AddRectFilled({0, 0}, display, col::rgb(0x11131A, 145));",
                       "world backdrop")
    # Title moved to center of left pane and no wide opaque top bar.
    src = replace_once(src, "float headerH = s(46), footerH = s(50), pad = s(12);",
                       "float headerH = s(44), footerH = s(49), pad = s(13);",
                       "layout metrics")
    # Footer separator retained but less prominent.
    SCREEN.write_text(src, encoding="utf-8")

    fonts = SRC / "Fonts.cpp"
    text = fonts.read_text(encoding="utf-8")
    text = replace_once(text, "constexpr float kBodySize  = 18.0f;",
                        "constexpr float kBodySize  = 16.0f;", "body font size")
    text = replace_once(text, "constexpr float kTitleSize = 26.0f;",
                        "constexpr float kTitleSize = 21.0f;", "title font size")
    fonts.write_text(text, encoding="utf-8")
    print("Applied Fabric Mod Menu 21-inspired composition, typography and functional controls.")


if __name__ == "__main__":
    main()
