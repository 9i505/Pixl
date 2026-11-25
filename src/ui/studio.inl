// ---------------------------------------------------------------- Studio: the new look
// One compact window laid out like a hardware mixer's software: profiles at the top left, the "device" (channel faders,
// meters, knobs and pads) beside them, and tabs below (Mic, Mixer, Sampler, Routing, Effects, System) whose modules are
// columns of thin sliders with their values under them. Grey with teal outlines.
// Only one look shows at a time: the classic window stays behind this one, hidden, and keeps running Pixl; what is not
// redrawn here yet (Effects) switches to the classic window, which has a "New look" button to come back.
// Included by app.cpp after the Mixer window, so everything it uses is already there.

static const int ST_W = 1100, ST_H = 700, ST_TITLE = 30;
static const COLORREF S_BG = RGB(43, 46, 48), S_TOP = RGB(31, 33, 35), S_PANEL = RGB(36, 38, 40), S_MOD = RGB(55, 59, 61);
static const COLORREF S_SUB = RGB(47, 51, 53), S_VALUE = RGB(62, 67, 69), S_TRACK = RGB(26, 28, 29), S_LINE = RGB(70, 75, 78);
static const COLORREF S_TEAL = RGB(79, 179, 191), S_TEXT = RGB(216, 220, 222), S_DIM = RGB(142, 150, 154), S_INK = RGB(16, 34, 37);
static const COLORREF S_RED = RGB(229, 83, 75), S_BODY = RGB(26, 28, 30), S_CAP = RGB(196, 200, 202);
// The channels: the mic, then the three program strips (Music, Chat, Game).
static const COLORREF S_CHANNEL[4] = {RGB(79, 179, 191), RGB(84, 140, 240), RGB(168, 112, 235), RGB(60, 190, 150)};
static const wchar_t* const S_CHANNEL_NAME[4] = {L"MIC", L"MUSIC", L"CHAT", L"GAME"};

enum StudioTab { STAB_MIC, STAB_MIXER, STAB_ROUTING, STAB_VOICE, STAB_ROOM, STAB_EQ, STAB_CLIP, STAB_SAMPLER, STAB_RECORD, STAB_SYSTEM, STAB_COUNT };
static const wchar_t* const ST_TABS[STAB_COUNT] = {L"MIC", L"MIXER", L"ROUTING", L"VOICE", L"ROOM", L"EQ", L"CLIP", L"SAMPLER", L"RECORD", L"SYSTEM"};
static const int ST_TAB_W = 108;
static int studioPage = STAB_MIC;  // the open tab (PIXL_STUDIO=<n> in the preview)
static const int SP_COUNT = STAB_COUNT;
static int studioList = 0;         // the profile list: 0 mixer profiles, 1 mic profiles

static HFONT sfCaps = nullptr, sfBody = nullptr, sfBold = nullptr, sfSmall = nullptr, sfNum = nullptr, sfBrand = nullptr;
static void studioFonts() {
    if (sfCaps) return;
    const wchar_t* face = uiFace.c_str();
    sfCaps = faceFont(-11, FW_BOLD, face);
    sfBody = faceFont(-12, FW_NORMAL, face);
    sfBold = faceFont(-12, FW_SEMIBOLD, face);
    sfSmall = faceFont(-10, FW_NORMAL, face);
    sfNum = faceFont(-12, FW_SEMIBOLD, face);
    sfBrand = faceFont(-13, FW_BOLD, face);
}
static void studioFontsDrop() {
    for (HFONT* f : {&sfCaps, &sfBody, &sfBold, &sfSmall, &sfNum, &sfBrand})
        if (*f) { DeleteObject(*f); *f = nullptr; }
}

// ---------------------------------------------------------------- profiles
// Mixer profiles: the program strips' faders, mutes, solos and routing plus the mic level. Mic profiles: the whole mic
// chain and level, which can be saved to a .pixlmic file and loaded by someone else.
static const wchar_t* ST_KEY = L"Software\\Pixl\\Studio";
static const wchar_t* const PROF_KEYS[2] = {L"Software\\Pixl\\Studio\\MixerProfiles", L"Software\\Pixl\\Studio\\MicProfiles"};
static const wchar_t* const PROF_NOW[2] = {L"Profile", L"MicProfile"};
static std::vector<std::wstring> studioProfiles[2];
static std::wstring studioProfile[2];

static void profRead(int kind) {
    studioProfiles[kind].clear();
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, PROF_KEYS[kind], 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        wchar_t name[128];
        for (DWORD i = 0;; ++i) {
            DWORD n = 128;
            if (RegEnumKeyExW(hk, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            studioProfiles[kind].push_back(name);
        }
        RegCloseKey(hk);
    }
    wchar_t cur[128];
    DWORD cb = sizeof cur;
    studioProfile[kind] = RegGetValueW(HKEY_CURRENT_USER, ST_KEY, PROF_NOW[kind], RRF_RT_REG_SZ, nullptr, cur, &cb) == ERROR_SUCCESS ? cur : L"";
}

static void profRemember(int kind, const std::wstring& name) {
    studioProfile[kind] = name;
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, ST_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) != ERROR_SUCCESS) return;
    RegSetValueExW(hk, PROF_NOW[kind], 0, REG_SZ, (const BYTE*)name.c_str(), (DWORD)((name.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hk);
}

// What a profile holds, as name = value pairs (also the .pixlmic file's lines).
static std::vector<std::pair<std::wstring, int>> profValues(int kind) {
    std::vector<std::pair<std::wstring, int>> v;
    if (kind == 0) {
        std::lock_guard<std::mutex> lk(gMix.mx);
        for (int i = 0; i < mixer::STRIPS; ++i) {
            const mixer::Strip& s = gMix.strips[i];
            const std::wstring at = L"S" + std::to_wstring(i);
            v.push_back({at + L"Db10", s.db10});
            v.push_back({at + L"Flags", (s.mute ? 1 : 0) | (s.solo ? 2 : 0) | (s.audio ? 4 : 0) | (s.b1 ? 8 : 0)});
        }
    } else {
        v = {{L"AiOn", g.aiOn.load()}, {L"AiLevel", g.aiLevel.load()}, {L"BgOn", g.nrBgOn.load()}, {L"Bg", g.nrBg.load()},
             {L"ImpactOn", g.nrImpactOn.load()}, {L"Impact", g.nrImpact.load()}, {L"GateOn", g.micGateOn.load()},
             {L"GateDb10", g.micGateDb10.load()}, {L"CompOn", g.compOn.load()}, {L"CompLevel", g.compLevel.load()},
             {L"Match", g.micMatch.load()}, {L"EqOn", g.micEqOn.load()}, {L"EqLowCut", g.micEqLowCut.load()},
             {L"EqBass", g.micEqBass.load()}, {L"EqMud", g.micEqMud.load()}, {L"EqPresence", g.micEqPresence.load()},
             {L"EqAir", g.micEqAir.load()}, {L"DeEssOn", g.deessOn.load()}, {L"DeEss", g.deess.load()}};
    }
    v.push_back({L"MicLevel", g.micLevel.load()});
    return v;
}

static void profSet(int kind, const std::wstring& n, int x) {
    if (n == L"MicLevel") { g.micLevel = std::max(0, std::min(MIC_MAX, x)); return; }
    if (kind == 0) {
        if (n.size() < 3 || n[0] != L'S') return;
        const int i = n[1] - L'0';
        if (i < 0 || i >= mixer::STRIPS) return;
        std::lock_guard<std::mutex> lk(gMix.mx);
        mixer::Strip& s = gMix.strips[i];
        if (n.compare(2, std::wstring::npos, L"Db10") == 0) s.db10 = std::max(mixer::MIN_DB10, std::min(mixer::MAX_DB10, x));
        else if (n.compare(2, std::wstring::npos, L"Flags") == 0) { s.mute = x & 1; s.solo = (x & 2) != 0; s.audio = (x & 4) != 0; s.b1 = (x & 8) != 0; }
        return;
    }
    auto pct = [&](int v) { return std::max(0, std::min(100, v)); };
    if (n == L"AiOn") g.aiOn = x != 0;
    else if (n == L"AiLevel") g.aiLevel = pct(x);
    else if (n == L"BgOn") g.nrBgOn = x != 0;
    else if (n == L"Bg") g.nrBg = pct(x);
    else if (n == L"ImpactOn") g.nrImpactOn = x != 0;
    else if (n == L"Impact") g.nrImpact = pct(x);
    else if (n == L"GateOn") g.micGateOn = x != 0;
    else if (n == L"GateDb10") g.micGateDb10 = std::max(-800, std::min(0, x));
    else if (n == L"CompOn") g.compOn = x != 0;
    else if (n == L"CompLevel") g.compLevel = pct(x);
    else if (n == L"Match") g.micMatch = x != 0;
    else if (n == L"EqOn") g.micEqOn = x != 0;
    else if (n == L"EqLowCut") g.micEqLowCut = std::max(20, std::min(200, x));
    else if (n == L"EqBass") g.micEqBass = std::max(-90, std::min(90, x));
    else if (n == L"EqMud") g.micEqMud = std::max(-90, std::min(90, x));
    else if (n == L"EqPresence") g.micEqPresence = std::max(-90, std::min(90, x));
    else if (n == L"EqAir") g.micEqAir = std::max(-90, std::min(90, x));
    else if (n == L"DeEssOn") g.deessOn = x != 0;
    else if (n == L"DeEss") g.deess = pct(x);
}

static void profApplied(int kind) {
    if (kind == 0) { gMix.volumeNow(); mixSave(); }
    else g.isoVersion.fetch_add(1);
    settingsSave();
    if (hMixer) InvalidateRect(hMixer, nullptr, FALSE);
    if (hMicWin) InvalidateRect(hMicWin, nullptr, FALSE);
    if (hwndMain) InvalidateRect(hwndMain, nullptr, FALSE);
}

static void profSave(int kind, const std::wstring& name) {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (std::wstring(PROF_KEYS[kind]) + L"\\" + name).c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) != ERROR_SUCCESS) return;
    for (const auto& [n, x] : profValues(kind)) { DWORD d = (DWORD)x; RegSetValueExW(hk, n.c_str(), 0, REG_DWORD, (const BYTE*)&d, sizeof d); }
    RegCloseKey(hk);
    auto& list = studioProfiles[kind];
    if (std::find(list.begin(), list.end(), name) == list.end()) list.push_back(name);
    profRemember(kind, name);
}

static void profApply(int kind, const std::wstring& name) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (std::wstring(PROF_KEYS[kind]) + L"\\" + name).c_str(), 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS) return;
    for (const auto& [n, now] : profValues(kind)) {
        DWORD d = 0, sz = sizeof d, type = 0;
        if (RegQueryValueExW(hk, n.c_str(), nullptr, &type, (BYTE*)&d, &sz) == ERROR_SUCCESS && type == REG_DWORD) profSet(kind, n, (int)d);
    }
    RegCloseKey(hk);
    profApplied(kind);
    profRemember(kind, name);
}

static void profDelete(int kind, const std::wstring& name) {
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(PROF_KEYS[kind]) + L"\\" + name).c_str());
    auto& list = studioProfiles[kind];
    list.erase(std::remove(list.begin(), list.end(), name), list.end());
    profRemember(kind, list.empty() ? L"" : list.front());
}

static void profEnsure() {
    for (int kind = 0; kind < 2; ++kind) {
        profRead(kind);
        if (studioProfiles[kind].empty()) profSave(kind, kind ? L"My mic" : L"Default");
        else if (studioProfile[kind].empty()) profRemember(kind, studioProfiles[kind].front());
    }
}

// A .pixlmic file: "Pixl mic profile" on the first line, then name=value lines. Plain text, so it can be sent anywhere.
static bool micExport(HWND h, const std::wstring& name) {
    wchar_t path[MAX_PATH] = {0};
    wcsncpy_s(path, (name + L".pixlmic").c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = h;
    ofn.lpstrFilter = L"Pixl mic profile (*.pixlmic)\0*.pixlmic\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"pixlmic";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"w, ccs=UTF-8") || !f) return false;
    fwprintf(f, L"Pixl mic profile\n");
    for (const auto& [n, x] : profValues(1)) fwprintf(f, L"%s=%d\n", n.c_str(), x);
    fclose(f);
    return true;
}

static bool micImport(HWND h) {
    wchar_t path[MAX_PATH] = {0};
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = h;
    ofn.lpstrFilter = L"Pixl mic profile (*.pixlmic)\0*.pixlmic\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"r, ccs=UTF-8") || !f) return false;
    wchar_t line[256];
    bool ours = false;
    std::vector<std::pair<std::wstring, int>> got;
    while (fgetws(line, 256, f)) {
        std::wstring s = line;
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
        if (!ours) { ours = s == L"Pixl mic profile"; if (!ours) break; continue; }
        const size_t eq = s.find(L'=');
        if (eq != std::wstring::npos) got.push_back({s.substr(0, eq), _wtoi(s.c_str() + eq + 1)});
    }
    fclose(f);
    if (!ours) { MessageBoxW(h, L"That file isn't a Pixl mic profile.", L"Pixl", MB_OK | MB_ICONINFORMATION); return false; }
    for (const auto& [n, x] : got) profSet(1, n, x);
    profApplied(1);
    profSave(1, cleanName(fileTitle(path)));
    return true;
}

// ---------------------------------------------------------------- what is on screen and what it does
// Painting lists every button and slider with its place; the mouse finds them there (the same frame it sees).

struct StArea {
    RECT r;
    std::function<void()> click;
    std::function<void()> down, up;  // held buttons (the glitch) and drags (the recorder's wave): on press and release
    std::function<void()> right;     // a right-click
};
struct StSlide {
    RECT r;               // where it is grabbed
    int lo, hi, def;      // range and the value a double-click gives back
    bool relative;        // a knob: drags by distance from where it was grabbed; a fader: the value under the mouse
    RECT track;           // a fader's travel (top = hi; for a horizontal one, right = hi)
    std::function<int()> get;
    std::function<void(int)> set;
    std::function<void()> settle;  // after a drag: save
    bool horiz = false;
};
static std::vector<StArea> stAreas;
static std::vector<StSlide> stSlides;
static int studioHot = -1, studioDrag = -1, stDragY = 0, stDragFrom = 0;
static bool studioMouse = false;
static POINT stMouse = {-1, -1};

static bool stHot(const RECT& r) { return PtInRect(&r, stMouse) != 0; }
static void stArea(const RECT& r, std::function<void()> click) { stAreas.push_back({r, std::move(click)}); }

// ---------------------------------------------------------------- drawing pieces

static void stFill(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
static void stFrame(HDC dc, const RECT& r, COLORREF c) {
    stFill(dc, RECT{r.left, r.top, r.right, r.top + 1}, c);
    stFill(dc, RECT{r.left, r.bottom - 1, r.right, r.bottom}, c);
    stFill(dc, RECT{r.left, r.top, r.left + 1, r.bottom}, c);
    stFill(dc, RECT{r.right - 1, r.top, r.right, r.bottom}, c);
}
static void stRound(HDC dc, const RECT& r, float rad, COLORREF fill, COLORREF edge, bool withEdge = true) {
    Gdiplus::Graphics gr(dc);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath p;
    roundPath(p, r.left + 0.5f, r.top + 0.5f, (float)(r.right - r.left - 1), (float)(r.bottom - r.top - 1), rad);
    Gdiplus::SolidBrush br(gpColor(fill));
    gr.FillPath(&br, &p);
    if (withEdge) {
        Gdiplus::Pen pen(gpColor(edge), 1.0f);
        gr.DrawPath(&pen, &p);
    }
}
static void stText(HDC dc, RECT r, const wchar_t* s, COLORREF c, HFONT f, UINT align = DT_LEFT) {
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ of = SelectObject(dc, f);
    DrawTextW(dc, s, -1, &r, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, of);
}
// Small capitals, spaced out: the labels of tabs and modules.
static void stCaps(HDC dc, RECT r, const wchar_t* s, COLORREF c, UINT align = DT_CENTER) {
    const int was = SetTextCharacterExtra(dc, 1);
    stText(dc, r, s, c, sfCaps, align);
    SetTextCharacterExtra(dc, was);
}
// A flat button: lit is on, hot under the mouse.
static void stButton(HDC dc, const RECT& r, const wchar_t* text, bool lit = false, COLORREF litColor = S_TEAL, bool caps = false) {
    const bool hot = stHot(r);
    stFill(dc, r, lit ? litColor : (hot ? S_VALUE : S_SUB));
    stFrame(dc, r, lit ? litColor : (hot ? S_TEAL : S_LINE));
    if (caps) stCaps(dc, r, text, lit ? S_INK : S_TEXT);
    else stText(dc, r, text, lit ? S_INK : S_TEXT, sfBold, DT_CENTER);
}
static void stIconButton(HDC dc, const RECT& r, int icon, bool enabled = true) {
    const bool hot = enabled && stHot(r);
    stFill(dc, r, hot ? S_VALUE : S_SUB);
    stFrame(dc, r, hot ? S_TEAL : S_LINE);
    drawIcon(dc, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, icon, enabled ? S_TEXT : S_LINE, hot ? S_VALUE : S_SUB);
}
static void stCheck(HDC dc, const RECT& r, bool on, COLORREF c = S_TEAL) {
    const bool hot = stHot(r);
    stFill(dc, r, on ? c : S_TRACK);
    stFrame(dc, r, on ? c : (hot ? S_TEAL : S_LINE));
    if (on) {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(gpColor(S_INK), 2.0f);
        const float x = (float)r.left, y = (float)r.top, w = (float)(r.right - r.left);
        const Gdiplus::PointF p[3] = {{x + w * 0.25f, y + w * 0.52f}, {x + w * 0.43f, y + w * 0.7f}, {x + w * 0.76f, y + w * 0.32f}};
        gr.DrawLines(&pen, p, 3);
    }
}

// A module: a box with its name on top, and an on/off box at its right when it can be turned off; returns where its
// columns go.
static RECT stModuleAt(HDC dc, const RECT& r, const wchar_t* name, const bool* on = nullptr, std::function<void()> toggle = {}) {
    const int x = r.left, w = r.right - r.left, y = r.top;
    stFill(dc, r, S_MOD);
    stCaps(dc, RECT{x + (on ? 4 : 0), y + 4, x + w - (on ? 26 : 0), y + 24}, name, S_TEXT);
    if (on) {
        const RECT sw = {x + w - 22, y + 8, x + w - 8, y + 22};
        stCheck(dc, sw, *on);
        stArea(RECT{x + w - 26, y + 4, x + w - 4, y + 26}, std::move(toggle));
    }
    return RECT{x + 6, y + 28, x + w - 6, r.bottom - 6};
}
// A module the height of the page.
static RECT stModule(HDC dc, int x, int w, const wchar_t* name, const bool* on = nullptr, std::function<void()> toggle = {}) {
    return stModuleAt(dc, RECT{x, 392, x + w, 664}, name, on, std::move(toggle));
}
// The arrow between two modules of a chain.
static void stArrow(HDC dc, int x) {
    stFill(dc, RECT{x, 392, x + 16, 664}, S_SUB);
    stText(dc, RECT{x, 392, x + 16, 664}, L"\x203A", S_DIM, sfBrand, DT_CENTER);
}

// One column of a module: a label, a thin vertical slider with a round thumb, the value in a box under it.
static void stColumn(HDC dc, const RECT& c, const wchar_t* label, int lo, int hi, int def, std::function<int()> get,
                     std::function<void(int)> set, std::function<std::wstring(int)> show, std::function<void()> settle, bool on = true,
                     const bool* own = nullptr, std::function<void()> flip = {}) {
    stFill(dc, c, S_SUB);
    if (own) {
        // A column that can be turned off by itself: its box before its name.
        const RECT box = {c.left + 6, c.top + 6, c.left + 20, c.top + 20};
        stCheck(dc, box, *own);
        stText(dc, RECT{c.left + 24, c.top + 4, c.right, c.top + 22}, label, S_DIM, sfSmall);
        stArea(RECT{c.left, c.top + 2, c.right, c.top + 24}, std::move(flip));
    } else {
        stText(dc, RECT{c.left, c.top + 4, c.right, c.top + 22}, label, S_DIM, sfSmall, DT_CENTER);
    }
    const RECT track = {(c.left + c.right) / 2 - 1, c.top + 32, (c.left + c.right) / 2 + 1, c.bottom - 40};
    const int v = get();
    const float f = hi > lo ? (float)(v - lo) / (hi - lo) : 0.0f;
    const int ty = track.bottom - (int)lroundf((track.bottom - track.top) * std::max(0.0f, std::min(1.0f, f)));
    stFill(dc, track, S_TRACK);
    stFill(dc, RECT{track.left, ty, track.right, track.bottom}, on ? S_TEAL : S_LINE);
    {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush thumb(gpColor(on ? S_TEAL : S_DIM));
        gr.FillEllipse(&thumb, (float)(track.left - 6), ty - 7.0f, 14.0f, 14.0f);
    }
    const RECT box = {c.left + 6, c.bottom - 30, c.right - 6, c.bottom - 8};
    stFill(dc, box, S_VALUE);
    stText(dc, box, show(v).c_str(), on ? S_TEXT : S_DIM, sfNum, DT_CENTER);
    RECT grab = {c.left, c.top + 24, c.right, c.bottom - 34};
    stSlides.push_back({grab, lo, hi, def, false, RECT{track.left, track.top, track.right, track.bottom}, std::move(get), std::move(set), std::move(settle)});
}

// ---------------------------------------------------------------- the strips (the device and the Mixer tab)

static int stDb10Of(int i) {
    if (i == 0) {
        const int lvl = g.micLevel.load();
        return lvl <= 0 ? mixer::MIN_DB10 : std::max(mixer::MIN_DB10, std::min(mixer::MAX_DB10, (int)lround(200.0 * log10(lvl / 100.0))));
    }
    std::lock_guard<std::mutex> lk(gMix.mx);
    return gMix.strips[i - 1].db10;
}
static void stSetDb10(int i, int db10) {
    db10 = std::max(mixer::MIN_DB10, std::min(mixer::MAX_DB10, db10));
    if (i == 0) {
        g.micLevel = db10 <= mixer::MIN_DB10 ? 0 : std::max(0, std::min(MIC_MAX, (int)lround(100.0 * mixer::volumeOf(db10))));
        if (hwndMain) InvalidateRect(hwndMain, nullptr, FALSE);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(gMix.mx);
        if (gMix.strips[i - 1].db10 == db10) return;
        gMix.strips[i - 1].db10 = db10;
    }
    gMix.volumeNow();
    if (hMixer) InvalidateRect(hMixer, nullptr, FALSE);
}
static void stSettle(int i) {
    if (i == 0) settingsSave();
    else mixSave();
}
static std::wstring stDbText(int db10) {
    wchar_t t[24];
    if (db10 <= mixer::MIN_DB10) return L"-inf";
    swprintf_s(t, db10 > 0 ? L"+%.1f" : L"%.1f", db10 / 10.0);
    return t;
}
static void stStripToggle(int s, int what) {
    {
        std::lock_guard<std::mutex> lk(gMix.mx);
        mixer::Strip& st = gMix.strips[s];
        if (what == 0) st.audio = !st.audio;
        else if (what == 1) st.b1 = !st.b1;
        else if (what == 2) st.mute = !st.mute;
        else st.solo = !st.solo;
    }
    mixSave();
    if (hMixer) InvalidateRect(hMixer, nullptr, FALSE);
}

// The level of channel i in dBFS (mic: after its chain; a program: after its fader), -120 when silent.
static float stLevelDb(int i, const mixer::Strip* s, bool anySolo) {
    if (i == 0) return g.micOut.load();
    if (s->mute || (anySolo && !s->solo) || (!s->audio && !s->b1)) return -120.0f;
    const float pk = gMix.level[i - 1].load();
    return pk > 0.0f ? 20.0f * log10f(pk) : -120.0f;
}

// ---------------------------------------------------------------- top: profiles and the device

static void stProfiles(HDC dc, HWND h) {
    const RECT p = {10, 40, 300, 330};
    stFill(dc, p, S_PANEL);
    static const wchar_t* const TABS[2] = {L"PROFILES", L"MIC PROFILES"};
    for (int k = 0; k < 2; ++k) {
        const RECT t = {p.left + k * 145, p.top, p.left + (k + 1) * 145, p.top + 28};
        const bool on = studioList == k;
        stFill(dc, t, on ? S_PANEL : S_TOP);
        stCaps(dc, t, TABS[k], on ? S_TEXT : S_DIM);
        if (on) stFill(dc, RECT{t.left, t.top, t.right, t.top + 2}, S_TEAL);
        stArea(t, [k] { studioList = k; });
    }
    stFrame(dc, p, S_TEAL);
    const int kind = studioList;
    // Profiles are Pixl's setups (every effect setting, the same files the classic look saves) plus the mixer's faders
    // and routing kept under the same name; mic profiles are the mic chain alone.
    const std::vector<std::wstring> list = kind ? studioProfiles[1] : setupList();
    const std::wstring now = kind ? studioProfile[1] : curSetup;
    for (size_t k = 0; k < list.size() && k < 9; ++k) {
        const RECT row = {p.left + 10, p.top + 38 + (int)k * 26, p.right - 10, p.top + 62 + (int)k * 26};
        const bool on = list[k] == now;
        if (on) stFill(dc, row, S_SUB);
        else if (stHot(row)) stFill(dc, row, RGB(40, 43, 45));
        if (on) stFill(dc, RECT{row.left, row.top, row.left + 2, row.bottom}, S_TEAL);
        stText(dc, RECT{row.left + 12, row.top, row.right - 6, row.bottom}, list[k].c_str(), on ? S_TEAL : S_TEXT, sfBody);
        const std::wstring name = list[k];
        if (kind) stArea(row, [name] { profApply(1, name); });
        else stArea(row, [name] {
            settingsFileLoad(setupPath(name));
            curSetup = name;
            rememberSetup(name);
            g.restart = true;
            appRepark();
            if (std::find(studioProfiles[0].begin(), studioProfiles[0].end(), name) != studioProfiles[0].end()) profApply(0, name);
            showMask();
            if (hwndMain) InvalidateRect(hwndMain, nullptr, FALSE);
        });
    }
    if (list.empty()) stText(dc, RECT{p.left + 16, p.top + 40, p.right - 10, p.top + 64}, kind ? L"No mic profiles yet" : L"No profiles yet: press +", S_DIM, sfBody);
    // Save, new, delete; and for mic profiles, export and import.
    const int y = p.bottom - 36;
    const RECT b[5] = {{p.left + 10, y, p.left + 60, y + 26}, {p.left + 65, y, p.left + 115, y + 26}, {p.left + 120, y, p.left + 170, y + 26},
                       {p.left + 175, y, p.left + 225, y + 26}, {p.left + 230, y, p.left + 280, y + 26}};
    stIconButton(dc, b[0], ICO_SAVE);
    stButton(dc, b[1], L"+");
    stIconButton(dc, b[2], ICO_TRASH, kind ? list.size() > 1 : !curSetup.empty());
    if (kind) {
        stArea(b[0], [] { if (!studioProfile[1].empty()) profSave(1, studioProfile[1]); });
        stArea(b[1], [h] {
            const std::wstring name = cleanName(askText(h, L"New mic profile", L"").c_str());
            if (!name.empty()) profSave(1, name);
        });
        stArea(b[2], [h] {
            if (studioProfiles[1].size() < 2 || studioProfile[1].empty()) return;
            const std::wstring ask = L"Delete \"" + studioProfile[1] + L"\"?";
            if (MessageBoxW(h, ask.c_str(), L"Pixl", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
            profDelete(1, studioProfile[1]);
            if (!studioProfile[1].empty()) profApply(1, studioProfile[1]);
        });
    } else {
        stArea(b[0], [h] {
            if (curSetup.empty()) { saveSetupAs(h); if (!curSetup.empty()) profSave(0, curSetup); return; }
            if (settingsFileSave(setupPath(curSetup))) {
                for (const std::wstring& d : otherSetupDirs) CopyFileW(setupPath(curSetup).c_str(), (d + L"\\" + curSetup + L".ini").c_str(), FALSE);
                savedAt = GetTickCount64();
            }
            profSave(0, curSetup);
        });
        stArea(b[1], [h] { saveSetupAs(h); if (!curSetup.empty()) profSave(0, curSetup); });
        stArea(b[2], [h] {
            if (curSetup.empty()) return;
            const std::wstring ask = L"Delete \"" + curSetup + L"\"?";
            if (MessageBoxW(h, ask.c_str(), L"Pixl", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
            DeleteFileW(setupPath(curSetup).c_str());
            for (const std::wstring& d : otherSetupDirs) DeleteFileW((d + L"\\" + curSetup + L".ini").c_str());
            if (std::find(studioProfiles[0].begin(), studioProfiles[0].end(), curSetup) != studioProfiles[0].end()) profDelete(0, curSetup);
            curSetup.clear();
            rememberSetup(L"");
        });
    }
    stIconButton(dc, b[3], ICO_SEND, kind == 1);
    stIconButton(dc, b[4], ICO_OPEN, kind == 1);
    if (kind == 1) {
        stArea(b[3], [h] { micExport(h, studioProfile[1]); });
        stArea(b[4], [h] { micImport(h); });
    }
}

// The device: the four channels spread across it, each with its name, its meter (two columns of lights on the fader's
// own scale), its fader, its level, and its mute (the mic: hear myself).
static void stDevice(HDC dc) {
    const RECT body = {312, 40, 1090, 330};
    stRound(dc, body, 14.0f, S_BODY, RGB(58, 62, 64));
    mixer::Strip strips[mixer::STRIPS];
    {
        std::lock_guard<std::mutex> lk(gMix.mx);
        for (int i = 0; i < mixer::STRIPS; ++i) strips[i] = gMix.strips[i];
    }
    bool anySolo = false;
    for (const mixer::Strip& s : strips) anySolo = anySolo || s.solo;
    for (int i = 0; i < 4; ++i) {
        const int x = 312 + i * 194 + 97 - 45;   // each channel in the middle of its quarter
        const COLORREF c = S_CHANNEL[i];
        const mixer::Strip* s = i ? &strips[i - 1] : nullptr;
        std::wstring name = i == 0 ? L"MIC" : (s->title.empty() ? S_CHANNEL_NAME[i] : s->title);
        CharUpperW(name.data());
        stCaps(dc, RECT{x - 20, 50, x + 110, 66}, name.c_str(), c);
        // The meter: two columns of lights on the fader's own scale.
        const float db = stLevelDb(i, s, anySolo);
        const RECT meter = {x + 14, 76, x + 30, 266};
        const float lv = db <= -99.0f ? 0.0f : mixer::faderPos((int)lroundf(std::max(-100.0f, db) * 10.0f)) / 1000.0f;
        const int lit = (int)lroundf(24 * lv);
        for (int k = 0; k < 24; ++k) {
            const int yb = meter.bottom - k * 8;
            const COLORREF on = k >= 21 ? S_RED : (k >= 17 ? RGB(232, 190, 76) : c);
            const COLORREF col = k < lit ? on : mixc(S_BODY, c, 0.14);
            stFill(dc, RECT{meter.left, yb - 6, meter.left + 7, yb}, col);
            stFill(dc, RECT{meter.left + 9, yb - 6, meter.right, yb}, col);
        }
        // The fader: a slot and a cap, on the mixer's law.
        const RECT slot = {x + 56, 76, x + 62, 266};
        stFill(dc, slot, RGB(8, 9, 10));
        const int db10 = stDb10Of(i);
        const int pos = mixer::faderPos(db10);
        const int top = slot.top + 10, bot = slot.bottom - 10;
        const int cy = bot - (bot - top) * pos / 1000;
        const int y0 = bot - (bot - top) * mixer::faderPos(0) / 1000;
        stFill(dc, RECT{slot.left - 8, y0, slot.left - 3, y0 + 1}, S_DIM);
        {
            const bool off = s && (s->mute || (anySolo && !s->solo));
            const RECT cap = {x + 44, cy - 12, x + 74, cy + 12};
            stRound(dc, cap, 3.0f, off ? RGB(110, 114, 116) : S_CAP, RGB(90, 94, 96));
            for (int k = -1; k <= 1; ++k) stFill(dc, RECT{cap.left + 5, cy + k * 5, cap.right - 5, cy + k * 5 + 1}, RGB(120, 124, 126));
        }
        stText(dc, RECT{x + 34, 268, x + 90, 286}, stDbText(db10).c_str(), db10 > 0 ? S_TEAL : S_TEXT, sfSmall, DT_CENTER);
        stSlides.push_back({RECT{x + 38, 70, x + 82, 272}, 0, 1000, mixer::faderPos(0), false, RECT{slot.left, top, slot.right, bot},
                            [i] { return mixer::faderPos(stDb10Of(i)); }, [i](int p) { stSetDb10(i, mixer::faderDb10(p)); },
                            [i] { stSettle(i); }});
        // Under it: mute (a program) or hear myself (the mic).
        const RECT btn = {x + 8, 292, x + 84, 318};
        if (i == 0) {
            stButton(dc, btn, L"HEAR", g.monitor.load(), S_TEAL, true);
            stArea(btn, [] { g.monitor = !g.monitor.load(); settingsSoon(hStudio); if (hwndMain) InvalidateRect(hwndMain, nullptr, FALSE); });
        } else {
            stButton(dc, btn, L"MUTE", s->mute, S_RED, true);
            stArea(btn, [i] { stStripToggle(i - 1, 2); });
        }
    }
}

// ---------------------------------------------------------------- the tabs

#include "studio_pages.inl"

static std::wstring stDbTenths(int v) {
    wchar_t t[24];
    swprintf_s(t, v > 0 ? L"+%.1f" : L"%.1f", v / 10.0);
    return t;
}

static void stTabMic(HDC dc, HWND h) {
    // Isolation | Noise > Gate > EQ > Comp > Sonar > De-esser, in the order the voice goes through them (1054 wide).
    // The mic's level and meter are its fader at the top.
    int x = (ST_W - 1054) / 2;
    auto pct = [](int v) { return std::to_wstring(v) + L"%"; };
    auto mic = [h] { micChanged(h); };
    auto flip = [h](std::atomic<bool>& f) { return [h, &f] { f = !f.load(); micChanged(h); }; };
    {
        // Noise isolation on or off and its kind (AI is the chain to the right); Hear myself; Raw mic.
        const bool isoOn = g.isolate.load();
        RECT in = stModule(dc, x, 130, L"ISOLATION", &isoOn, [] { g.isolate = !g.isolate.load(); settingsSave(); g.reopenMic = true; });
        stFill(dc, in, S_SUB);
        const RECT prev = {in.left + 4, in.top + 14, in.left + 28, in.top + 40}, next = {in.right - 28, in.top + 14, in.right - 4, in.top + 40};
        stIconButton(dc, prev, ICO_PREV);
        stIconButton(dc, next, ICO_NEXT);
        stText(dc, RECT{prev.right, prev.top, next.left, prev.bottom}, isoModeName(g.isoMode.load()), isoOn ? S_TEXT : S_DIM, sfBold, DT_CENTER);
        auto step = [](int d) {
            return [d] {
                int at = 0;
                for (int k = 0; k < ISO_MODES; ++k) if (ISO_ORDER[k] == g.isoMode.load()) at = k;
                at = (at + d + ISO_MODES) % ISO_MODES;
                g.isoMode = ISO_ORDER[at];
                g.isoVersion.fetch_add(1);
                settingsSave();
            };
        };
        stArea(prev, step(-1));
        stArea(next, step(1));
        const int m = g.isoMode.load();
        if (m != ISO_AI) {
            // The other kinds have one strength.
            stHSlider(dc, RECT{in.left + 4, in.top + 50, in.right - 4, in.top + 76}, L"", 0, 100, knobDefault(K_ISO), [] { return knobValue(K_ISO); },
                      [](int v) { knobSet(K_ISO, v); }, [](int v) { return std::to_wstring(v); }, [] { knobSettle(K_ISO); }, isoOn);
        } else {
            stText(dc, RECT{in.left + 4, in.top + 50, in.right - 4, in.top + 76}, isoOn ? L"The chain \x203A" : L"Off: raw voice", S_DIM, sfSmall, DT_CENTER);
        }
        stToggle(dc, in.left + 6, in.top + 104, in.right - in.left - 6, L"Hear myself", g.monitor.load(),
                 [] { g.monitor = !g.monitor.load(); settingsSoon(hStudio); });
        stToggle(dc, in.left + 6, in.top + 130, in.right - in.left - 6, L"Raw mic", g.rawMic.load(), [] { g.rawMic = !g.rawMic.load(); g.reopenMic = true; });
    }
    x += 138;
    const bool ai = g.isolate.load() && g.isoMode.load() == ISO_AI;
    const bool aiOn = g.aiOn.load(), gateOn = g.micGateOn.load(), compOn = g.compOn.load(), matchOn = g.micMatch.load();
    const bool bgOn = g.nrBgOn.load(), impactOn = g.nrImpactOn.load(), eqOn = g.micEqOn.load(), deessOn = g.deessOn.load();
    RECT in = stModule(dc, x, 208, L"NOISE", &aiOn, flip(g.aiOn));
    const int w3 = (in.right - in.left - 12) / 3;
    stColumn(dc, RECT{in.left, in.top, in.left + w3, in.bottom}, L"AI", 0, 100, MIC_DEFAULTS.aiLevel,
             [] { return g.aiLevel.load(); }, [](int v) { g.aiLevel = v; g.isoVersion.fetch_add(1); }, pct, mic, ai && aiOn);
    stColumn(dc, RECT{in.left + w3 + 6, in.top, in.left + 2 * w3 + 6, in.bottom}, L"Backgr.", 0, 100, MIC_DEFAULTS.bg,
             [] { return g.nrBg.load(); }, [](int v) { g.nrBg = v; g.isoVersion.fetch_add(1); }, pct, mic, ai && bgOn, &bgOn, flip(g.nrBgOn));
    stColumn(dc, RECT{in.left + 2 * w3 + 12, in.top, in.right, in.bottom}, L"Impact", 0, 100, MIC_DEFAULTS.impact,
             [] { return g.nrImpact.load(); }, [](int v) { g.nrImpact = v; g.isoVersion.fetch_add(1); }, pct, mic, ai && impactOn, &impactOn, flip(g.nrImpactOn));
    x += 208;
    stArrow(dc, x); x += 16;
    in = stModule(dc, x, 80, L"GATE", &gateOn, flip(g.micGateOn));
    stColumn(dc, in, L"Opens at", -800, 0, MIC_DEFAULTS.gateDb10, [] { return g.micGateDb10.load(); },
             [](int v) { g.micGateDb10 = v; g.isoVersion.fetch_add(1); }, [](int v) { return fmtNumber(v / 10.0, 0) + L" dB"; }, mic, ai && gateOn);
    x += 80;
    stArrow(dc, x); x += 16;
    {
        // Mic EQ: a low cut, then body, boxiness, presence and air.
        in = stModule(dc, x, 300, L"MIC EQ", &eqOn, flip(g.micEqOn));
        const int cw = (in.right - in.left - 16) / 5;
        auto col = [&](int k) { return RECT{in.left + k * (cw + 4), in.top, in.left + k * (cw + 4) + cw, in.bottom}; };
        stColumn(dc, col(0), L"Low cut", 20, 200, MIC_DEFAULTS.eqLowCut, [] { return g.micEqLowCut.load(); },
                 [](int v) { g.micEqLowCut = v; g.isoVersion.fetch_add(1); }, [](int v) { return v <= 20 ? std::wstring(L"Off") : std::to_wstring(v) + L" Hz"; },
                 mic, ai && eqOn);
        struct Band { const wchar_t* name; std::atomic<int>* v; int def; };
        const Band bands[4] = {{L"Bass", &g.micEqBass, MIC_DEFAULTS.eqBass}, {L"Mud", &g.micEqMud, MIC_DEFAULTS.eqMud},
                               {L"Presence", &g.micEqPresence, MIC_DEFAULTS.eqPresence}, {L"Air", &g.micEqAir, MIC_DEFAULTS.eqAir}};
        for (int k = 0; k < 4; ++k) {
            std::atomic<int>* v = bands[k].v;
            stColumn(dc, col(k + 1), bands[k].name, -90, 90, bands[k].def, [v] { return v->load(); },
                     [v](int n) { *v = n; g.isoVersion.fetch_add(1); }, stDbTenths, mic, ai && eqOn);
        }
    }
    x += 300;
    stArrow(dc, x); x += 16;
    in = stModule(dc, x, 80, L"COMP", &compOn, flip(g.compOn));
    stColumn(dc, in, L"Amount", 0, 100, MIC_DEFAULTS.compLevel, [] { return g.compLevel.load(); },
             [](int v) { g.compLevel = v; g.isoVersion.fetch_add(1); }, pct, mic, ai && compOn);
    x += 80;
    stArrow(dc, x); x += 16;
    {
        const RECT r = stModule(dc, x, 88, L"SONAR", nullptr);
        stFill(dc, r, S_SUB);
        const RECT b = {r.left + 8, r.top + 80, r.right - 8, r.top + 112};
        stButton(dc, b, matchOn ? L"ON" : L"OFF", matchOn, S_TEAL, true);
        stArea(b, flip(g.micMatch));
        stText(dc, RECT{r.left, r.top + 122, r.right, r.top + 140}, L"Tone and level", S_DIM, sfSmall, DT_CENTER);
        stText(dc, RECT{r.left, r.top + 138, r.right, r.top + 156}, L"like SteelSeries", S_DIM, sfSmall, DT_CENTER);
        stText(dc, RECT{r.left, r.top + 154, r.right, r.top + 172}, L"Sonar", S_DIM, sfSmall, DT_CENTER);
    }
    x += 88;
    stArrow(dc, x); x += 16;
    in = stModule(dc, x, 80, L"DE-ESS", &deessOn, flip(g.deessOn));
    stColumn(dc, in, L"Amount", 0, 100, MIC_DEFAULTS.deess, [] { return g.deess.load(); },
             [](int v) { g.deess = v; g.isoVersion.fetch_add(1); }, pct, mic, ai && deessOn);
}

static void stTabMixer(HDC dc, HWND h) {
    // The three program channels (what plays in each, add one, where it goes), and under them the plugins.
    mixer::Strip strips[mixer::STRIPS];
    {
        std::lock_guard<std::mutex> lk(gMix.mx);
        for (int i = 0; i < mixer::STRIPS; ++i) strips[i] = gMix.strips[i];
    }
    for (int s = 0; s < mixer::STRIPS; ++s) {
        const int x = 40 + s * 344;
        const mixer::Strip& st = strips[s];
        const RECT in = stModuleAt(dc, RECT{x, 392, x + 330, 532}, st.title.empty() ? S_CHANNEL_NAME[s + 1] : st.title.c_str(), nullptr);
        stFill(dc, RECT{x, 392, x + 330, 394}, S_CHANNEL[s + 1]);
        // The programs in it, two to a row, then "+ Add".
        const int colW = (in.right - in.left - 6) / 2;
        auto slot = [&](int k) {
            const int cx = in.left + (k % 2) * (colW + 6), cy = in.top + (k / 2) * 24;
            return RECT{cx, cy, cx + colW, cy + 22};
        };
        const int shown = std::min((int)st.apps.size(), 5);
        for (int k = 0; k < shown; ++k) {
            const RECT row = slot(k);
            const bool hot = stHot(row);
            stFill(dc, row, hot ? S_VALUE : S_SUB);
            int tx = row.left + 6;
            if (HICON ic = mixIcon(st.apps[k])) { DrawIconEx(dc, row.left + 4, row.top + 3, ic, 16, 16, 0, nullptr, DI_NORMAL); tx = row.left + 26; }
            stText(dc, RECT{tx, row.top, row.right - (hot ? 22 : 6), row.bottom}, st.apps[k].c_str(), S_TEXT, sfBody);
            if (!hot) continue;
            // Its x takes it out of this channel.
            const RECT x2 = {row.right - 22, row.top, row.right, row.bottom};
            drawIcon(dc, row.right - 11.0f, (row.top + row.bottom) / 2.0f, ICO_CLOSE, stHot(x2) ? S_RED : S_DIM, S_VALUE);
            const std::wstring app = st.apps[k];
            stArea(x2, [s, app] {
                {
                    std::lock_guard<std::mutex> lk(gMix.mx);
                    auto& apps = gMix.strips[s].apps;
                    apps.erase(std::remove_if(apps.begin(), apps.end(), [&](const std::wstring& a) { return mixer::sameName(a, app); }), apps.end());
                }
                mixSave();
                if (hMixer) InvalidateRect(hMixer, nullptr, FALSE);
            });
        }
        {
            const RECT add = slot(shown);
            const std::wstring more = (int)st.apps.size() > shown ? L"+ Add (" + std::to_wstring(st.apps.size() - shown) + L" more)" : L"+ Add a program";
            const bool hot = stHot(add);
            stFill(dc, add, hot ? S_VALUE : S_MOD);
            stFrame(dc, add, hot ? S_TEAL : S_LINE);
            stText(dc, add, more.c_str(), hot ? S_TEXT : S_DIM, sfBody, DT_CENTER);
            stArea(add, [h, s, add] { mixAddProgramAt(h, s, add); });
        }
        const int bw = (in.right - in.left - 18) / 4, by = in.top + 3 * 24 + 4;
        const wchar_t* names[4] = {L"YOU", L"DISCORD", L"MUTE", L"SOLO"};
        const bool on[4] = {st.audio, st.b1, st.mute, st.solo};
        const COLORREF col[4] = {S_CHANNEL[s + 1], S_CHANNEL[s + 1], S_RED, RGB(232, 190, 76)};
        for (int k = 0; k < 4; ++k) {
            const RECT b = {in.left + k * (bw + 6), by, in.left + k * (bw + 6) + bw, by + 26};
            stButton(dc, b, names[k], on[k], col[k], true);
            stArea(b, [s, k] { stStripToggle(s, k); });
        }
    }
    stPlugPanel(dc, h, RECT{40, 540, 1062, 664});
}

static void stTabSampler(HDC dc, HWND h) {
    const unsigned playing = gBoard.playing();
    for (int k = 0; k < 12 && k < BOARD_PADS; ++k) {
        const RECT r = {40 + (k % 6) * 172, 396 + (k / 6) * 112, 40 + (k % 6) * 172 + 160, 396 + (k / 6) * 112 + 100};
        const bool on = ((playing >> k) & 1u) != 0, hot = stHot(r);
        const BoardPad& bp = boardPads[k];
        stFill(dc, r, on ? mixc(S_MOD, S_TEAL, 0.4) : (hot ? S_VALUE : S_MOD));
        stFrame(dc, r, on || hot ? S_TEAL : S_LINE);
        const std::wstring nm = bp.path.empty() ? L"Empty" : (bp.name.empty() ? fileTitle(bp.path) : bp.name);
        stText(dc, RECT{r.left + 10, r.top + 22, r.right - 10, r.top + 50}, nm.c_str(), bp.path.empty() ? S_DIM : S_TEXT, sfBold, DT_CENTER);
        stCaps(dc, RECT{r.left, r.top + 56, r.right, r.top + 76}, (L"PAD " + std::to_wstring(k + 1)).c_str(), S_DIM);
        stArea(r, [k] { boardPlay(k); });
    }
    const RECT edit = {40, 624, 240, 656};
    stButton(dc, edit, L"Add, trim and set keys");
    stArea(edit, [h] { openBoard(h); });
}

static void stTabRouting(HDC dc) {
    const RECT t = {40, 392, 1060, 664};
    stFill(dc, t, S_MOD);
    static const wchar_t* const COLS[2] = {L"HEADPHONES", L"DISCORD MIC"};
    const int rowH = 46, x0 = 260, colW = 220;
    for (int c = 0; c < 2; ++c) stCaps(dc, RECT{x0 + c * colW, 404, x0 + (c + 1) * colW, 430}, COLS[c], S_DIM);
    mixer::Strip strips[mixer::STRIPS];
    {
        std::lock_guard<std::mutex> lk(gMix.mx);
        for (int i = 0; i < mixer::STRIPS; ++i) strips[i] = gMix.strips[i];
    }
    for (int r = 0; r < 4; ++r) {
        const int y = 440 + r * rowH;
        stFill(dc, RECT{t.left + 12, y, t.right - 12, y + rowH - 6}, r % 2 ? S_MOD : S_SUB);
        stFill(dc, RECT{t.left + 12, y, t.left + 15, y + rowH - 6}, S_CHANNEL[r]);
        const std::wstring name = r == 0 ? L"MIC" : (strips[r - 1].title.empty() ? S_CHANNEL_NAME[r] : strips[r - 1].title);
        stCaps(dc, RECT{t.left + 28, y, x0, y + rowH - 6}, name.c_str(), S_TEXT, DT_LEFT);
        for (int c = 0; c < 2; ++c) {
            const RECT box = {x0 + c * colW + colW / 2 - 11, y + 9, x0 + c * colW + colW / 2 + 11, y + 31};
            if (r == 0) {
                // The mic always goes to Discord; in your headphones it is Hear myself.
                const bool on = c == 0 ? g.monitor.load() : true;
                stCheck(dc, box, on, S_CHANNEL[0]);
                if (c == 0) stArea(box, [] { g.monitor = !g.monitor.load(); settingsSoon(hStudio); if (hwndMain) InvalidateRect(hwndMain, nullptr, FALSE); });
            } else {
                const mixer::Strip& s = strips[r - 1];
                stCheck(dc, box, c == 0 ? s.audio : s.b1, S_CHANNEL[r]);
                stArea(box, [r, c] { stStripToggle(r - 1, c); });
            }
        }
    }
    stText(dc, RECT{t.left + 16, t.bottom - 40, t.right - 16, t.bottom - 12},
           L"Discord mic is what others hear: pick What U Hear (VB-Audio Virtual Cable) as the microphone in Discord and games.", S_DIM, sfSmall);
}

// ---------------------------------------------------------------- the window

static HFONT sfEdit = nullptr;  // the link box's own font (a real edit box: drawn by Windows at screen scale)

// The link box shows on the Clip tab only.
static void stPlaceUrl() {
    if (!hStudioUrl) return;
    const bool show = studioPage == STAB_CLIP;
    if (show) SetWindowPos(hStudioUrl, nullptr, sc(R_ST_URL.left + 8), sc(R_ST_URL.top + 6), sc(R_ST_URL.right - R_ST_URL.left - 16),
                           sc(R_ST_URL.bottom - R_ST_URL.top - 10), SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(hStudioUrl, show ? SW_SHOWNA : SW_HIDE);
}

static void paintStudioIn(HDC dc, const RECT& rc, HWND h) {
    studioFonts();
    stAreas.clear();
    stSlides.clear();
    stFill(dc, rc, S_BG);
    // Title strip: the name in the middle, an update when there is one, minimise and close; drag it to move the window.
    stFill(dc, RECT{0, 0, rc.right, ST_TITLE}, S_TOP);
    const int was = SetTextCharacterExtra(dc, 4);
    stText(dc, RECT{0, 0, rc.right, ST_TITLE}, L"PIXL", S_TEXT, sfBrand, DT_CENTER);
    SetTextCharacterExtra(dc, was);
    if (updState.load() == 1 || updState.load() == 2) {
        const RECT up = {10, 4, 200, ST_TITLE - 4};
        const std::wstring t = updState.load() == 2 ? L"Downloading update..." : L"Update to " + updLatest.version.text();
        stButton(dc, up, t.c_str(), true);
        if (updState.load() == 1) stArea(up, [] { updStart(hwndMain); });
    }
    const RECT rMin = {ST_W - 80, 0, ST_W - 40, ST_TITLE}, rClose = {ST_W - 40, 0, ST_W, ST_TITLE};
    if (stHot(rClose)) stFill(dc, rClose, RGB(200, 58, 72));
    else if (stHot(rMin)) stFill(dc, rMin, S_VALUE);
    drawIcon(dc, (rMin.left + rMin.right) / 2.0f, ST_TITLE / 2.0f, ICO_MIN, S_TEXT, stHot(rMin) ? S_VALUE : S_TOP);
    drawIcon(dc, (rClose.left + rClose.right) / 2.0f, ST_TITLE / 2.0f, ICO_CLOSE, S_TEXT, stHot(rClose) ? RGB(200, 58, 72) : S_TOP);
    stArea(rMin, [h] { ShowWindow(h, SW_MINIMIZE); });
    stArea(rClose, [h] { PostMessageW(h, WM_CLOSE, 0, 0); });

    stProfiles(dc, h);
    stDevice(dc);

    // The tab row, joined to the page under it.
    const int ty = 344, th = 30;
    stFill(dc, RECT{0, ty + th - 1, rc.right, ty + th}, S_TEAL);
    for (int k = 0; k < STAB_COUNT; ++k) {
        const RECT t = {10 + k * ST_TAB_W, ty, 10 + (k + 1) * ST_TAB_W, ty + th};
        const bool on = studioPage == k;
        if (on) {
            stFill(dc, t, S_BG);
            stFill(dc, RECT{t.left, t.top, t.right, t.top + 1}, S_TEAL);
            stFill(dc, RECT{t.left, t.top, t.left + 1, t.bottom}, S_TEAL);
            stFill(dc, RECT{t.right - 1, t.top, t.right, t.bottom}, S_TEAL);
        } else if (stHot(t)) {
            stFill(dc, RECT{t.left, t.top, t.right, t.bottom - 1}, RGB(48, 51, 53));
        }
        stCaps(dc, t, ST_TABS[k], on ? S_TEXT : S_DIM);
        stArea(t, [k] { studioPage = k; stPlaceUrl(); });
    }
    switch (studioPage) {
    case STAB_MIC: stTabMic(dc, h); break;
    case STAB_MIXER: stTabMixer(dc, h); break;
    case STAB_ROUTING: stTabRouting(dc); break;
    case STAB_VOICE: stTabVoice(dc, h); break;
    case STAB_ROOM: stTabRoom(dc, h); break;
    case STAB_EQ: stTabEq(dc, h); break;
    case STAB_CLIP: stTabClip(dc, h); break;
    case STAB_SAMPLER: stTabSampler(dc, h); break;
    case STAB_RECORD: stTabRecord(dc, h); break;
    default: stTabSystem(dc, h); break;
    }
}
static void paintStudio(HDC dc, const RECT& rc) { paintStudioIn(dc, rc, hStudio); }

static int stSlideAt(POINT p) {
    for (int k = (int)stSlides.size() - 1; k >= 0; --k) if (PtInRect(&stSlides[k].r, p)) return k;
    return -1;
}
static int stAreaAt(POINT p) {
    for (int k = (int)stAreas.size() - 1; k >= 0; --k) if (PtInRect(&stAreas[k].r, p)) return k;
    return -1;
}

static void stSlideTo(HWND h, int k, POINT p, bool start) {
    StSlide& s = stSlides[k];
    int v;
    if (s.relative) {
        if (start) { stDragY = p.y; stDragFrom = s.get(); return; }
        v = stDragFrom + (int)lroundf((stDragY - p.y) * (s.hi - s.lo) / 160.0f);
    } else if (s.horiz) {
        v = s.lo + (int)lroundf((float)(p.x - s.track.left) / std::max(1L, s.track.right - s.track.left) * (s.hi - s.lo));
    } else {
        v = s.lo + (int)lroundf((float)(s.track.bottom - p.y) / std::max(1L, s.track.bottom - s.track.top) * (s.hi - s.lo));
    }
    s.set(std::max(s.lo, std::min(s.hi, v)));
    InvalidateRect(h, nullptr, FALSE);
}

static std::function<void()> stHeldUp;  // the release of a held button (the glitch)

static LRESULT CALLBACK studioProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
        printBox(h, (HDC)w, paintStudio);
        return 0;
    case WM_PAINT:
        paintBox(h, paintStudio);
        return 0;
    case WM_TIMER:
        if (!IsIconic(h) && IsWindowVisible(h)) InvalidateRect(h, nullptr, FALSE);  // meters, pads, the clip and the recorder
        return 0;
    case WM_CTLCOLOREDIT: {
        static HBRUSH well = nullptr;
        if (!well) well = CreateSolidBrush(S_TRACK);
        SetTextColor((HDC)w, S_TEXT);
        SetBkColor((HDC)w, S_TRACK);
        return (LRESULT)well;
    }
    case WM_COMMAND:
        if ((HWND)l == hStudioUrl && HIWORD(w) == EN_CHANGE && hClipUrl) {
            wchar_t u[2048] = L"";
            GetWindowTextW(hStudioUrl, u, 2048);
            SetWindowTextW(hClipUrl, u);
        }
        return 0;
    case WM_MOUSEMOVE: {
        boxTrackLeave(h, studioMouse);
        stMouse = {(short)LOWORD(l), (short)HIWORD(l)};
        if (stWaveDrag) { stWaveMove(stMouse); InvalidateRect(h, nullptr, FALSE); return 0; }
        if (studioDrag >= 0 && studioDrag < (int)stSlides.size()) { stSlideTo(h, studioDrag, stMouse, false); return 0; }
        const int hot = stAreaAt(stMouse) * 2 + 1 + (stSlideAt(stMouse) + 1) * 4096;
        if (hot != studioHot) { studioHot = hot; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        studioMouse = false;
        if (!stWaveDrag && studioDrag < 0) stMouse = {-1, -1};
        studioHot = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const POINT p = {(short)LOWORD(l), (short)HIWORD(l)};
        stMouse = p;
        if (keyListen >= 0) { keyListenEnd(); keyNote.clear(); }
        const int sl = stSlideAt(p);
        if (sl >= 0) {
            if (m == WM_LBUTTONDBLCLK) {
                stSlides[sl].set(stSlides[sl].def);
                stSlides[sl].settle();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            studioDrag = sl;
            SetCapture(h);
            stSlideTo(h, sl, p, true);
            return 0;
        }
        const int a = stAreaAt(p);
        if (a >= 0) {
            // The paint that follows rebuilds the list, so the actions are taken out first.
            const StArea area = stAreas[a];
            if (area.down) {
                area.down();
                if (area.up) { stHeldUp = area.up; SetCapture(h); }
            }
            if (area.click) area.click();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (p.y < ST_TITLE) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_RBUTTONUP: {
        const POINT p = {(short)LOWORD(l), (short)HIWORD(l)};
        const int a = stAreaAt(p);
        if (a >= 0 && stAreas[a].right) {
            const std::function<void()> act = stAreas[a].right;
            act();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        const POINT p = {(short)LOWORD(l), (short)HIWORD(l)};
        if (stWaveDrag) { stWaveUp(p); InvalidateRect(h, nullptr, FALSE); return 0; }
        if (stHeldUp) {
            const std::function<void()> up = stHeldUp;
            stHeldUp = nullptr;
            ReleaseCapture();
            up();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (studioDrag >= 0) {
            const int k = studioDrag;
            studioDrag = -1;
            ReleaseCapture();
            if (k < (int)stSlides.size()) stSlides[k].settle();
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (stHeldUp) { const std::function<void()> up = stHeldUp; stHeldUp = nullptr; up(); }
        if (stWaveDrag) { stWaveDrag = false; take.w.drag = WD_NONE; }
        if (studioDrag >= 0) {
            const int k = studioDrag;
            studioDrag = -1;
            if (k < (int)stSlides.size()) stSlides[k].settle();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        const POINT p = wheelPoint(h, l);
        const double notches = GET_WHEEL_DELTA_WPARAM(w) / (double)WHEEL_DELTA;
        if (studioPage == STAB_RECORD) {
            const RECT wr = stWaveRect();
            if (PtInRect(&wr, p) && takeWheel(hwndMain, stToWave(p), notches)) { InvalidateRect(h, nullptr, FALSE); return 0; }
        }
        const int sl = stSlideAt(p);
        if (sl < 0) return 0;
        StSlide& s = stSlides[sl];
        const int step = std::max(1, (s.hi - s.lo) / ((GetKeyState(VK_SHIFT) & 0x8000) ? 500 : 100));
        s.set(std::max(s.lo, std::min(s.hi, s.get() + (notches > 0 ? step : -step))));
        s.settle();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
        // A hotkey being set on the System tab takes the keys (the same handling as the classic Settings box).
        if (keyListen >= 0) {
            MSG msg = {h, m, w, l};
            if (keyCapture(h, msg)) return 0;
        }
        break;
    case WM_CLOSE:
        // Closing the new look closes Pixl, as the classic window does.
        DestroyWindow(h);
        if (hwndMain) PostMessageW(hwndMain, WM_CLOSE, 0, 0);
        return 0;
    case WM_DPICHANGED: {
        // Dragged onto a screen with another scale: Pixl takes that screen's scale (as the classic window does when it
        // moves), so the page fills the window instead of staying the old size in its corner.
        uiDpi = HIWORD(w);
        const RECT* at = (const RECT*)l;
        const RECT wa = workAreaOf(MonitorFromRect(at, MONITOR_DEFAULTTONEAREST));
        float s = uiDpi / 96.0f * uiZoom / 100.0f;
        s = std::min(s, std::min((wa.right - wa.left - 8) / (float)ST_W, (wa.bottom - wa.top - 8) / (float)ST_H));
        s = std::max(ZOOM_MIN / 100.0f, std::min(ZOOM_MAX / 100.0f, s));
        if (hwndMain) applyScale(hwndMain, s, nullptr, false);
        else uiScale = s;
        const int nw = sc(ST_W), nh = sc(ST_H);
        SetWindowPos(h, nullptr, (at->left + at->right - nw) / 2, at->top, nw, nh, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hStudioUrl) {
            HFONT was = sfEdit;
            sfEdit = faceFont(-sc(13), FW_NORMAL, uiFace.c_str());
            SendMessageW(hStudioUrl, WM_SETFONT, (WPARAM)sfEdit, TRUE);
            if (was) DeleteObject(was);
            stPlaceUrl();
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        if (keyListen >= 0) keyListenEnd();
        hStudio = nullptr;
        hStudioUrl = nullptr;
        studioMouse = false;
        studioHot = -1;
        studioDrag = -1;
        stWaveDrag = false;
        stHeldUp = nullptr;
        stAreas.clear();
        stSlides.clear();
        studioFontsDrop();
        if (sfEdit) { DeleteObject(sfEdit); sfEdit = nullptr; }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// The new look's colours for every box Pixl opens beside it (effect windows, the soundboard editor, lists, prompts).
static void studioRecolour() {
    if (!uiStudio) return;
    C_GOLD = S_TEAL; C_TEXT = S_TEXT; C_DIM = S_DIM; C_PANEL = S_MOD; C_BACK = S_BG; C_EDGE = S_LINE;
    C_NAVY = S_VALUE; C_OFF = RGB(96, 101, 104); C_KNOB = RGB(40, 90, 96);
    lookBrushes();
}

// Opens the new look centred on the screen the classic window is on.
static void studioOpen(int show) {
    if (hStudio) {
        if (IsIconic(hStudio)) ShowWindow(hStudio, SW_RESTORE);
        ShowWindow(hStudio, SW_SHOW);
        SetForegroundWindow(hStudio);
        return;
    }
    profEnsure();
    {
        // The program strips are Music, Chat and Game (they kept the old names "Audio 1, 2, 3" before).
        bool renamed = false;
        {
            std::lock_guard<std::mutex> lk(gMix.mx);
            for (int i = 0; i < mixer::STRIPS && i < 3; ++i)
                if (gMix.strips[i].title.empty() || gMix.strips[i].title == L"Audio " + std::to_wstring(i + 1)) {
                    static const wchar_t* const NAMES[3] = {L"Music", L"Chat", L"Game"};
                    gMix.strips[i].title = NAMES[i];
                    renamed = true;
                }
        }
        if (renamed) mixSave();
    }
    applyLook();  // the boxes take the new look's colours (studioRecolour)
    RECT o = {0, 0, 0, 0};
    if (hwndMain) GetWindowRect(hwndMain, &o);
    const RECT wa = workAreaOf(MonitorFromRect(&o, MONITOR_DEFAULTTOPRIMARY));
    const int w = sc(ST_W), ht = sc(ST_H);
    const int x = std::max((int)wa.left, (int)(wa.left + (wa.right - wa.left - w) / 2));
    const int y = std::max((int)wa.top, (int)(wa.top + (wa.bottom - wa.top - ht) / 2));
    hStudio = CreateWindowExW(WS_EX_APPWINDOW, L"PixlStudio", L"Pixl", WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX | WS_SYSMENU,
                              x, y, w, ht, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hStudio) return;
    if (hwndMain) {
        SendMessageW(hStudio, WM_SETICON, ICON_BIG, SendMessageW(hwndMain, WM_GETICON, ICON_BIG, 0));
        SendMessageW(hStudio, WM_SETICON, ICON_SMALL, SendMessageW(hwndMain, WM_GETICON, ICON_SMALL, 0));
    }
    // The sound clip's link box: what the classic one holds, kept in step with it.
    hStudioUrl = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, hStudio, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (hStudioUrl) {
        sfEdit = faceFont(-sc(13), FW_NORMAL, uiFace.c_str());
        SendMessageW(hStudioUrl, WM_SETFONT, (WPARAM)sfEdit, TRUE);
        SendMessageW(hStudioUrl, EM_SETLIMITTEXT, 2000, 0);
        SendMessageW(hStudioUrl, EM_SETCUEBANNER, TRUE, (LPARAM)L"Paste the sound link");
        noTheme(hStudioUrl);
        wchar_t u[2048] = L"";
        if (hClipUrl) GetWindowTextW(hClipUrl, u, 2048);
        SetWindowTextW(hStudioUrl, u);
        stPlaceUrl();
    }
    SetTimer(hStudio, 1, 60, nullptr);
    if (uiTop) applyTopmost(hStudio);
    ShowWindow(hStudio, show == SW_HIDE || show == SW_SHOWMINNOACTIVE || show == SW_MINIMIZE || show == SW_SHOWMINIMIZED ? show : SW_SHOW);
    if (IsWindowVisible(hStudio) && !IsIconic(hStudio)) SetForegroundWindow(hStudio);
}

// Back from the classic panel to the new look.
static void studioBack() {
    studioMainWanted = false;
    if (hwndMain) ShowWindow(hwndMain, SW_HIDE);
    studioOpen(SW_SHOW);
}

// Switches between the new look and the classic one, and remembers the choice.
static void studioUse(bool on) {
    uiStudio = on;
    settingsSave();
    if (on) {
        studioBack();
    } else {
        studioMainWanted = false;
        if (hStudio) DestroyWindow(hStudio);
        applyLook();  // the classic colours again
        if (hwndMain) {
            refreshLook(hwndMain);
            if (IsIconic(hwndMain)) ShowWindow(hwndMain, SW_RESTORE);
            windowOpen(hwndMain, SW_SHOW);
            SetForegroundWindow(hwndMain);
        }
    }
}
