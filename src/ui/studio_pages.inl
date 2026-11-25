// ---------------------------------------------------------------- Studio: the pages that were on the classic panel
// Voice, Room, EQ, Clip, Plugins, Record and System, drawn in the new look. Each control calls what the classic panel's
// control calls (the same atomics, the same functions), so both looks always do the same thing. Included by studio.inl.

// ---------------------------------------------------------------- pieces

// A knob of the classic panel (K_...) as a column: its value, range and default are the classic ones.
static void stKnobColumn(HDC dc, const RECT& c, const wchar_t* label, int k, int lo, int hi, std::function<std::wstring(int)> show, bool on = true) {
    stColumn(dc, c, label, lo, hi, knobDefault(k), [k] { return knobValue(k); }, [k](int v) { knobSet(k, v); }, std::move(show),
             [k] { knobSettle(k); }, on);
}

// A horizontal slider: label on the left, the value on the right.
static void stHSlider(HDC dc, const RECT& r, const wchar_t* label, int lo, int hi, int def, std::function<int()> get, std::function<void(int)> set,
                      std::function<std::wstring(int)> show, std::function<void()> settle, bool on = true) {
    const int lw = label[0] ? 96 : 4, vw = label[0] ? 86 : 50;  // no label: the track starts at once
    if (label[0]) stText(dc, RECT{r.left, r.top, r.left + 90, r.bottom}, label, S_DIM, sfBody);
    const RECT track = {r.left + lw, (r.top + r.bottom) / 2 - 1, r.right - vw, (r.top + r.bottom) / 2 + 1};
    const int v = get();
    const float f = hi > lo ? std::max(0.0f, std::min(1.0f, (float)(v - lo) / (hi - lo))) : 0.0f;
    const int tx = track.left + (int)lroundf((track.right - track.left) * f);
    stFill(dc, track, S_TRACK);
    stFill(dc, RECT{track.left, track.top, tx, track.bottom}, on ? S_TEAL : S_LINE);
    {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush thumb(gpColor(on ? S_TEAL : S_DIM));
        gr.FillEllipse(&thumb, tx - 7.0f, (float)track.top - 6.0f, 14.0f, 14.0f);
    }
    const RECT box = {r.right - vw + 8, r.top + 2, r.right, r.bottom - 2};
    stFill(dc, box, S_VALUE);
    stText(dc, box, show(v).c_str(), on ? S_TEXT : S_DIM, sfNum, DT_CENTER);
    StSlide s{RECT{track.left - 8, r.top, track.right + 8, r.bottom}, lo, hi, def, false, track, std::move(get), std::move(set), std::move(settle)};
    s.horiz = true;
    stSlides.push_back(std::move(s));
}

// A check box with its words; the whole row clicks.
static void stToggle(HDC dc, int x, int y, int w, const wchar_t* text, bool on, std::function<void()> flip, COLORREF c = S_TEAL) {
    const RECT box = {x, y + 3, x + 16, y + 19};
    stCheck(dc, box, on, c);
    stText(dc, RECT{x + 24, y, x + w, y + 22}, text, on ? S_TEXT : S_DIM, sfBody);
    stArea(RECT{x, y, x + w, y + 22}, std::move(flip));
}

// A button that opens a list (a picker): shows the current choice; the list opens under it.
static void stPicker(HDC dc, HWND h, const RECT& r, const std::wstring& now, std::function<std::vector<std::wstring>()> items, std::function<void(int)> picked) {
    const bool hot = stHot(r);
    stFill(dc, r, hot ? S_VALUE : S_TRACK);
    stFrame(dc, r, hot ? S_TEAL : S_LINE);
    stText(dc, RECT{r.left + 10, r.top, r.right - 24, r.bottom}, now.c_str(), S_TEXT, sfBody);
    stText(dc, RECT{r.right - 22, r.top, r.right - 6, r.bottom}, L"\x25BE", S_DIM, sfBody, DT_CENTER);
    stArea(r, [h, r, items, picked] {
        const std::vector<std::wstring> list = items();
        if (!list.empty()) showMenuList(h, r, list, picked);
    });
}

// What a classic drop-down holds, and choosing an item there as if picked by hand (its owner reacts the same way).
static std::vector<std::wstring> stComboItems(HWND combo, bool listBox = false) {
    std::vector<std::wstring> v;
    if (!combo) return v;
    const int n = (int)SendMessageW(combo, listBox ? LB_GETCOUNT : CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; ++i) {
        wchar_t t[512] = {0};
        if ((int)SendMessageW(combo, listBox ? LB_GETTEXTLEN : CB_GETLBTEXTLEN, i, 0) < 511) SendMessageW(combo, listBox ? LB_GETTEXT : CB_GETLBTEXT, i, (LPARAM)t);
        v.push_back(t);
    }
    return v;
}
static std::wstring stComboNow(HWND combo, const wchar_t* none = L"") {
    if (!combo) return none;
    const int i = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    const std::vector<std::wstring> items = stComboItems(combo);
    return i >= 0 && i < (int)items.size() ? items[i] : none;
}
static void stComboPick(HWND combo, int id, int i, bool listBox = false) {
    if (!combo || !hwndMain) return;
    SendMessageW(combo, listBox ? LB_SETCURSEL : CB_SETCURSEL, (WPARAM)i, 0);
    SendMessageW(hwndMain, WM_COMMAND, MAKEWPARAM(id, listBox ? LBN_SELCHANGE : CBN_SELCHANGE), (LPARAM)combo);
}

static std::wstring stPct(int v) { return std::to_wstring(v) + L"%"; }

// ---------------------------------------------------------------- Voice

static int stVoiceSet = 0;  // the mask list shown: 0 X-Fi, 1 Pixl masks

static void stTabVoice(HDC dc, HWND h) {
    // Masks
    RECT in = stModule(dc, 40, 452, L"VOICE MASK");
    const int mask = g.voiceMask.load();
    for (int k = 0; k < 2; ++k) {
        const RECT t = {in.left + k * 90, in.top, in.left + (k + 1) * 90 - 4, in.top + 22};
        stButton(dc, t, k ? L"PIXL MASKS" : L"X-FI", stVoiceSet == k, S_TEAL, true);
        stArea(t, [k] { stVoiceSet = k; });
    }
    {
        const RECT off = {in.right - 90, in.top, in.right, in.top + 22};
        const bool neutral = mask == voice::MASK_NEUTRAL && g.pitchHz.load() == 0 && g.voiceMacro.load() == 0 && g.voiceSpeed.load() == 100;
        stButton(dc, off, L"OFF", neutral, S_TEAL, true);
        stArea(off, [] {
            g.voiceMask = voice::MASK_NEUTRAL;
            g.voiceRatio = 1000;
            g.voiceSize = 100;
            g.pitchHz = 0;
            g.voiceMacro = 0;
            g.voiceSpeed = 100;
            showMask();
            settingsSave();
        });
    }
    const int count = stVoiceSet ? voice::NUM_OWN_MASKS : voice::NUM_XFI_MASKS;
    const int cols = 3, cw = (in.right - in.left - 2 * 4) / cols, rh = stVoiceSet ? 25 : 30;
    for (int k = 0; k < count; ++k) {
        const int mi = stVoiceSet ? voice::OWN_ORDER[k] : k;
        const RECT b = {in.left + (k % cols) * (cw + 4), in.top + 28 + (k / cols) * rh, in.left + (k % cols) * (cw + 4) + cw, in.top + 28 + (k / cols) * rh + rh - 3};
        const bool on = mask == mi;
        const bool hot = stHot(b);
        stFill(dc, b, on ? S_TEAL : (hot ? S_VALUE : S_SUB));
        stText(dc, RECT{b.left + 6, b.top, b.right - 4, b.bottom}, voice::MASKS[mi].name, on ? S_INK : S_TEXT, sfBody);
        stArea(b, [mi] { pickMask(mi); settingsSave(); });
    }

    // Voice changer
    in = stModule(dc, 500, 300, L"VOICE CHANGER");
    const int cw2 = (in.right - in.left - 18) / 4;
    auto col = [&](int i) { return RECT{in.left + i * (cw2 + 6), in.top, in.left + i * (cw2 + 6) + cw2, in.bottom}; };
    stKnobColumn(dc, col(0), L"Pitch", K_PITCH, -PITCH_SPAN, PITCH_SPAN, [](int v) { return (v > 0 ? L"+" : L"") + std::to_wstring(v) + L" Hz"; });
    stKnobColumn(dc, col(1), L"Size", K_SIZE, VSIZE_MIN, VSIZE_MAX, stPct);
    stKnobColumn(dc, col(2), L"Voice", K_VOICE, -100, 100, [](int v) { return v == 0 ? std::wstring(L"natural") : (v > 0 ? L"bigger +" : L"smaller ") + std::to_wstring(v); });
    stKnobColumn(dc, col(3), L"Speed", K_VSPEED, SPEED_MIN, SPEED_MAX, [](int v) { return fmtNumber(v / 100.0, 2) + L"x"; });

    // Glitch, where the mask goes, keep pitch
    in = stModule(dc, 808, 252, L"GLITCH \x00B7 MASK ON");
    {
        const RECT hold = {in.left, in.top, in.right, in.top + 44};
        const bool held = g.glitchHold.load() || g.glitchKeyHeld.load();
        stButton(dc, hold, held ? L"REPEATING" : L"HOLD TO REPEAT", held, S_TEAL, true);
        StArea a{hold, nullptr};
        a.down = [] { g.glitchHold = true; };
        a.up = [] { g.glitchHold = false; };
        stAreas.push_back(std::move(a));
        const int st = std::max(0, std::min(voice::NUM_GLITCH_STYLES - 1, g.glitchStyle.load()));
        stPicker(dc, h, RECT{in.left, in.top + 52, in.right, in.top + 78}, voice::GLITCH_LOOKS[st].name,
                 [] { std::vector<std::wstring> v; for (int i = 0; i < voice::NUM_GLITCH_STYLES; ++i) v.push_back(voice::GLITCH_LOOKS[i].name); return v; },
                 [](int i) { stComboPick(hGlitchBox, ID_GLITCHBOX, i); });
        // Its key: click, then press a key or a mouse button (Backspace clears it).
        const int gk = g.glitchKey.load();
        const RECT key = {in.left, in.top + 82, in.right, in.top + 102};
        const std::wstring keyNow = glitchListening ? L"Press a key or a mouse button..." : (!glitchNote.empty() ? glitchNote : (gk ? L"Key: " + keyText((DWORD)gk) : L"Key: none (click to set)"));
        stText(dc, key, keyNow.c_str(), glitchListening ? S_TEAL : (stHot(key) ? S_TEXT : S_DIM), sfSmall);
        stArea(key, [h] { glitchListening = !glitchListening; glitchNote.clear(); SetFocus(h); });
        const int on = g.changeOn.load();
        auto bit = [](int b) { return [b] { g.changeOn = g.changeOn.load() ^ b; settingsSave(); }; };
        stCaps(dc, RECT{in.left, in.top + 108, in.right, in.top + 126}, L"MASK GOES ON", S_DIM, DT_LEFT);
        stToggle(dc, in.left, in.top + 128, in.right - in.left, L"My voice", (on & ON_VOICE) != 0, bit(ON_VOICE));
        stToggle(dc, in.left, in.top + 152, in.right - in.left, L"Sound clip", (on & ON_CLIP) != 0, bit(ON_CLIP));
        stToggle(dc, in.left, in.top + 176, in.right - in.left, L"Program", (on & ON_PROGRAM) != 0, bit(ON_PROGRAM));
        stToggle(dc, in.left, in.top + 206, in.right - in.left, L"Keep pitch when the speed changes", g.voiceKeepPitch.load(),
                 [] { g.voiceKeepPitch = !g.voiceKeepPitch.load(); settingsSave(); });
    }
}

// ---------------------------------------------------------------- Room (environment, 8D, options)

static void stTabRoom(HDC dc, HWND h) {
    RECT in = stModule(dc, 40, 452, L"ENVIRONMENT");
    const int cur = g.preset.load();
    const int cols = 3, cw = (in.right - in.left - 8) / cols, rh = 38;
    for (int k = 0; k < NUM_CARD_PRESETS; ++k) {
        const RECT b = {in.left + (k % cols) * (cw + 4), in.top + (k / cols) * rh, in.left + (k % cols) * (cw + 4) + cw, in.top + (k / cols) * rh + rh - 4};
        const bool on = cur == k, hot = stHot(b);
        stFill(dc, b, on ? S_TEAL : (hot ? S_VALUE : S_SUB));
        wchar_t nm[96];
        MultiByteToWideChar(CP_UTF8, 0, CARD_PRESETS[k].name, -1, nm, 96);
        stText(dc, RECT{b.left + 8, b.top, b.right - 4, b.bottom}, nm, on ? S_INK : S_TEXT, sfBold);
        stArea(b, [k] { stComboPick(hPreset, ID_PRESET, k, true); });
    }

    const bool eight = eightShown();
    if (eight) {
        in = stModule(dc, 500, 300, L"8D AUDIO");
        const int cw2 = (in.right - in.left - 24) / 5;
        auto col = [&](int i) { return RECT{in.left + i * (cw2 + 6), in.top, in.left + i * (cw2 + 6) + cw2, in.bottom}; };
        static const wchar_t* const NAMES[5] = {L"Bass", L"Clarity", L"Echo", L"Speed", L"Width"};
        for (int k = 0; k < 5; ++k)
            stKnobColumn(dc, col(k), NAMES[k], K_EIGHT + k, 0, eightMax(k),
                         k == 3 ? std::function<std::wstring(int)>([](int v) { return fmtNumber(spatial::EightD::turnSeconds(v), 1) + L" s"; }) : stPct,
                         k < 3 || g.eightMove.load());
    } else {
        in = stModule(dc, 500, 300, L"EFFECTS AMOUNT");
        stKnobColumn(dc, RECT{in.left + 100, in.top, in.right - 100, in.bottom}, L"Amount", K_AMOUNT, 0, 100,
                     [](int v) { return v <= 0 ? std::wstring(L"off") : std::to_wstring(v); });
    }

    in = stModule(dc, 808, 252, L"OPTIONS");
    const int w = in.right - in.left;
    stToggle(dc, in.left, in.top, w, L"Audio effects on", g.enabled.load(), [] { g.enabled = !g.enabled.load(); });
    stToggle(dc, in.left, in.top + 24, w, L"Wide echo in headphones", g.wide.load(), [] { g.wide = !g.wide.load(); });
    stToggle(dc, in.left, in.top + 48, w, L"CMSS 3D", g.cmss.load(), [] { g.cmss = !g.cmss.load(); });
    if (eight) {
        const int on = g.eightOn.load();
        stCaps(dc, RECT{in.left, in.top + 84, in.right, in.top + 102}, L"8D GOES ROUND", S_DIM, DT_LEFT);
        stToggle(dc, in.left, in.top + 104, w, L"My voice", (on & ON_VOICE) != 0, [] { g.eightOn = g.eightOn.load() ^ ON_VOICE; settingsSave(); });
        stToggle(dc, in.left, in.top + 128, w, L"Programs and clips", (on & ON_PROGRAM) != 0,
                 [] { g.eightOn = g.eightOn.load() ^ (ON_PROGRAM | ON_CLIP); settingsSave(); });
        stToggle(dc, in.left, in.top + 152, w, L"Move (round and round)", g.eightMove.load(), [] { g.eightMove = !g.eightMove.load(); settingsSave(); });
    } else {
        const int eo = g.echoOn.load();
        auto bit = [](int b) { return [b] { g.echoOn = g.echoOn.load() ^ b; settingsSave(); }; };
        stCaps(dc, RECT{in.left, in.top + 84, in.right, in.top + 102}, L"ECHO ON", S_DIM, DT_LEFT);
        stToggle(dc, in.left, in.top + 104, w, L"My voice", (eo & ON_VOICE) != 0, bit(ON_VOICE));
        stToggle(dc, in.left, in.top + 128, w, L"Sound clip", (eo & ON_CLIP) != 0, bit(ON_CLIP));
        stToggle(dc, in.left, in.top + 152, w, L"Program", (eo & ON_PROGRAM) != 0, bit(ON_PROGRAM));
    }
}

// ---------------------------------------------------------------- EQ

static void stTabEq(HDC dc, HWND h) {
    const bool on = g.eqOn.load();
    RECT in = stModule(dc, 40, 760, L"EQUALIZER", &on, [] { g.eqOn = !g.eqOn.load(); eqSave(); });
    const int n = eqb::EQ_BANDS + 1, cw = (in.right - in.left - 6 * (n - 1)) / n;
    for (int k = 0; k < n; ++k) {
        const RECT c = {in.left + k * (cw + 6), in.top, in.left + k * (cw + 6) + cw, in.bottom};
        if (k < eqb::EQ_BANDS)
            stKnobColumn(dc, c, EQ_BAND_NAME[k], K_EQ + k, -eqb::EQ_RANGE, eqb::EQ_RANGE, [](int v) { return (v > 0 ? L"+" : L"") + std::to_wstring(v) + L" dB"; }, on);
        else
            stColumn(dc, c, L"Level", -eqb::EQ_LEVEL_RANGE, 0, 0, [] { return -g.eqLevel.load(); }, [](int v) { knobSet(K_EQ_LEVEL, -v); },
                     [](int v) { return std::to_wstring(v) + L" dB"; }, [] { knobSettle(K_EQ_LEVEL); }, on);
    }
    in = stModule(dc, 808, 252, L"PRESET");
    const int cur = g.eqPreset.load();
    stPicker(dc, h, RECT{in.left, in.top, in.right, in.top + 28}, eqOnPreset() ? eqPresetName(cur) : L"Custom",
             [] { std::vector<std::wstring> v; for (int i = 0; i < eqPresetCount(); ++i) v.push_back(eqPresetName(i)); return v; },
             [](int i) { if (hEqBox) SendMessageW(hEqBox, CB_SETCURSEL, i, 0); eqGlidePreset(hwndMain, i); });
    int b5 = 0;
    {
        int boost = 0;
        for (int k = 0; k < 2; ++k) boost += g.eqDb[k].load();
        b5 = boost;
    }
    stText(dc, RECT{in.left, in.top + 36, in.right, in.top + 56}, (L"Bass boost " + std::wstring(b5 >= 0 ? L"+" : L"") + std::to_wstring(b5) + L" dB").c_str(), S_DIM, sfBody);
    const RECT def = {in.left, in.top + 64, in.right, in.top + 92};
    stButton(dc, def, L"Default (flat)");
    stArea(def, [] { knobSet(K_EQ_LEVEL, 0); if (hEqBox) SendMessageW(hEqBox, CB_SETCURSEL, 0, 0); eqGlidePreset(hwndMain, 0); eqSave(); });
    const RECT save = {in.left, in.top + 100, in.right, in.top + 128};
    stButton(dc, save, L"Save as a preset");
    stArea(save, [h] {
        const int now = g.eqPreset.load();
        const std::wstring name = askSetupName(h, now >= eqb::NUM_EQ_PRESETS ? eqPresetName(now) : L"");
        if (name.empty()) return;
        EqUserPreset np;
        np.name = name;
        for (int k = 0; k < eqb::EQ_BANDS; ++k) np.band[k] = g.eqDb[k].load();
        eqUserStore(np);
        eqUserLoad();
        int i = eqb::NUM_EQ_PRESETS;
        for (size_t u = 0; u < eqUser.size(); ++u) if (eqUser[u].name == name) i = eqb::NUM_EQ_PRESETS + (int)u;
        g.eqPreset = i;
        fillEqPresets();
        eqSave();
    });
    if (cur >= eqb::NUM_EQ_PRESETS) {
        const RECT drop = {in.left, in.top + 136, in.right, in.top + 164};
        stButton(dc, drop, L"Delete this preset");
        stArea(drop, [] {
            const int now = g.eqPreset.load();
            if (now < eqb::NUM_EQ_PRESETS) return;
            eqUserDrop(eqPresetName(now));
            eqUserLoad();
            g.eqPreset = 0;
            fillEqPresets();
            eqSave();
        });
    }
}

// ---------------------------------------------------------------- Clip

static HWND hStudioUrl = nullptr;  // the link box on the Clip tab (a real edit box, so typing and pasting work)
static const RECT R_ST_URL = {56, 428, 640, 456};

static void stClipStart() {
    wchar_t u[2048] = L"";
    if (hStudioUrl) GetWindowTextW(hStudioUrl, u, 2048);
    std::wstring url = u;
    while (!url.empty() && iswspace(url.back())) url.pop_back();
    size_t s0 = 0;
    while (s0 < url.size() && iswspace(url[s0])) ++s0;
    url = url.substr(s0);
    if (hClipUrl) SetWindowTextW(hClipUrl, url.c_str());
    if (gClip.isPaused()) gClip.resume();
    else if (!url.empty()) { gClip.start(url); settingsSave(); }
}

static void stTabClip(HDC dc, HWND h) {
    RECT in = stModule(dc, 40, 1020, L"SOUND CLIP");
    stFill(dc, R_ST_URL, S_TRACK);
    stFrame(dc, R_ST_URL, S_LINE);
    const bool playing = gClip.active(), paused = gClip.isPaused();
    const RECT open = {652, 428, 752, 456}, play = {760, 428, 860, 456}, stop = {868, 428, 968, 456};
    stButton(dc, open, L"Open file");
    stArea(open, [h] {
        wchar_t path[MAX_PATH] = L"";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof ofn;
        ofn.hwndOwner = h;
        ofn.lpstrFilter = L"Sound files\0*.mp3;*.m4a;*.aac;*.wav;*.wma;*.flac;*.mp4;*.webm;*.opus;*.ogg\0All files\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Choose a sound file";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (GetOpenFileNameW(&ofn) && path[0]) {
            if (hStudioUrl) SetWindowTextW(hStudioUrl, path);
            if (hClipUrl) SetWindowTextW(hClipUrl, path);
            gClip.start(path);
            settingsSave();
        }
    });
    stButton(dc, play, paused ? L"Resume" : L"Play", playing && !paused);
    stArea(play, [] { stClipStart(); });
    const bool pausing = playing && !paused;
    stButton(dc, stop, pausing ? L"Pause" : L"Stop");
    stArea(stop, [] { if (gClip.isPaused()) gClip.stop(); else gClip.pause(); });
    {
        const int st = gClip.state.load();
        const std::wstring m = st == clip::IDLE ? L"Paste a link (YouTube, TikTok...) or open a sound file"
                             : st == clip::DONE ? L"Finished" : gClip.isPaused() ? L"Paused" : gClip.message();
        stText(dc, RECT{56, 460, 1040, 478}, m.c_str(), st == clip::FAILED ? S_RED : S_DIM, sfSmall);
    }
    const double dur = gClip.durationSec.load();
    const bool canSeek = gClip.seekable();
    stHSlider(dc, RECT{56, 488, 820, 514}, L"Position", 0, 1000, 0, [] { return knobValue(K_CLIP_POS); }, [](int v) { knobSet(K_CLIP_POS, v); },
              [dur](int v) { return trimClock(dur * v / 1000.0, 0) + L" / " + trimClock(dur, 0); }, [] { knobSettle(K_CLIP_POS); }, canSeek);
    const RECT back = {840, 488, 930, 514}, fwd = {940, 488, 1030, 514};
    stButton(dc, back, L"- 10 s");
    stArea(back, [] { gClip.skip(-10.0); });
    stButton(dc, fwd, L"+ 10 s");
    stArea(fwd, [] { gClip.skip(10.0); });
    stHSlider(dc, RECT{56, 526, 620, 552}, L"Volume", 0, CLIP_MAX, CLIP_GAIN_DEFAULT, [] { return knobValue(K_CLIP_VOL); },
              [](int v) { knobSet(K_CLIP_VOL, v); }, [](int v) { return std::to_wstring(v); }, [] { knobSettle(K_CLIP_VOL); });
    stHSlider(dc, RECT{56, 564, 620, 590}, L"Speed", SPEED_MIN, SPEED_MAX, 100, [] { return knobValue(K_CLIP_SPEED); },
              [](int v) { knobSet(K_CLIP_SPEED, v); }, [](int v) { return fmtNumber(v / 100.0, 2) + L"x"; }, [] { knobSettle(K_CLIP_SPEED); });
    stToggle(dc, 660, 528, 380, L"Hear it myself", g.clipHear.load(), [] { g.clipHear = !g.clipHear.load(); g.restart = true; settingsSave(); });
    stToggle(dc, 660, 552, 380, L"Send it to the mic (others hear it)", g.clipToMic.load(), [] { g.clipToMic = !g.clipToMic.load(); settingsSave(); });
    stToggle(dc, 660, 576, 380, L"Keep pitch when the speed changes", g.clipKeepPitch.load(), [] { g.clipKeepPitch = !g.clipKeepPitch.load(); settingsSave(); });
    stToggle(dc, 660, 600, 380, L"Follow my voice speed", g.clipFollow.load(), [] { g.clipFollow = !g.clipFollow.load(); settingsSave(); });
    (void)in;
}

// ---------------------------------------------------------------- Plugins

static void stPlugAddMenu(HWND h, const RECT& at) {
    plugScanStart();
    const std::vector<plugins::Found> found = plugScanner.found();
    const bool looking = !plugScanner.searched();
    const auto& own = pfx::catalog();
    std::vector<std::wstring> items;
    for (const auto& e : own) items.push_back(e.name);
    for (const auto& f : found) items.push_back(f.name);
    items.push_back(looking ? L"Looking for VST plugins..." : found.empty() ? L"Add VST plugins from a folder..." : L"Choose another plugin folder...");
    showMenuList(h, at, items, [h, found, looking](int k) {
        const auto& own = pfx::catalog();
        if (k < (int)own.size()) { plugAdd(hwndMain, plugins::Found{own[k].name, std::wstring(pfx::PREFIX) + own[k].id, L"Pixl"}); return; }
        k -= (int)own.size();
        if (k < (int)found.size()) { plugAdd(hwndMain, found[k]); return; }
        if (!looking) plugPickFolder(h);
    });
}

static void stPlugMenu(HWND h, const RECT& at, int i) {
    plugins::Chain& c = plugChain(plugTab);
    vst2::Plugin* p = c.at(i);
    if (!p) return;
    std::vector<std::wstring> items = {p->editorOpen() ? L"Close its window" : L"Open its window", c.bypassed(i) ? L"Turn on" : L"Turn off (bypass)",
                                       L"Move left", L"Move right", L"Remove"};
    const int tab = plugTab;
    showMenuList(h, at, items, [i, tab](int k) {
        plugins::Chain& c = plugChain(tab);
        vst2::Plugin* p = c.at(i);
        if (!p) return;
        switch (k) {
        case 0: plugToggleWindow(p); break;
        case 1: c.setBypass(i, !c.bypassed(i)); break;
        case 2: c.move(i, i - 1); break;
        case 3: c.move(i, i + 1); break;
        case 4: {
            plugCloseWindow(p);
            auto gone = c.remove(i);
            gone.reset();
            break;
        }
        }
        plugSave();
    });
}

// The plugins, under the Mixer's channels: My voice or the program's, their settings on one line, then the effects in
// the order the sound goes through them (click opens one's window, right-click for more).
static void stPlugPanel(HDC dc, HWND h, const RECT& box) {
    stFill(dc, box, S_MOD);
    const int y = box.top + 6, x0 = box.left + 8, x1 = box.right - 8;
    stCaps(dc, RECT{x0, y, x0 + 90, y + 26}, L"PLUGINS", S_TEXT, DT_LEFT);
    for (int k = 0; k < 2; ++k) {
        const RECT t = {x0 + 90 + k * 116, y, x0 + 90 + k * 116 + 110, y + 26};
        stButton(dc, t, k ? L"PROGRAM" : L"MY VOICE", plugTab == (k ? PLUG_PROGRAM : PLUG_VOICE), S_TEAL, true);
        stArea(t, [k] { plugTab = k ? PLUG_PROGRAM : PLUG_VOICE; plugNote.clear(); applyLayout(); plugSave(); });
    }
    const int lx = x0 + 340;
    const int y2 = y + 32;
    if (plugTab == PLUG_PROGRAM) {
        stText(dc, RECT{lx, y, lx + 70, y + 26}, L"Program", S_DIM, sfBody);
        stPicker(dc, h, RECT{lx + 70, y, lx + 320, y + 26}, gApp.chosen() ? gApp.name : L"(none)",
                 [] { fillPrograms(); std::vector<std::wstring> v = {L"(none)"}; for (const auto& p : uiApps) v.push_back(p.name); return v; },
                 [](int i) { stComboPick(hApp, ID_APP, i); });
        stHSlider(dc, RECT{lx + 340, y, x1, y + 26}, L"Level", 0, APP_MAX, APP_GAIN_DEFAULT, [] { return knobValue(K_APP); },
                  [](int v) { knobSet(K_APP, v); }, [](int v) { return std::to_wstring(v); }, [] { knobSettle(K_APP); });
        stToggle(dc, x0, y2, 170, L"Only I hear it", g.appOnly.load(), [] { g.appOnly = !g.appOnly.load(); appRepark(); settingsSave(); });
        const bool card = g.appHear.load();
        stToggle(dc, x0 + 180, y2, 220, L"Hear it through the card", card,
                 [] { if (!g.appHear.load()) { g.appHear = true; g.restart = true; appRepark(); settingsSave(); } });
        stToggle(dc, x0 + 410, y2, 200, L"Hear it from the app", !card,
                 [] { if (g.appHear.load()) { g.appHear = false; g.restart = true; appRepark(); settingsSave(); } });
    } else {
        stToggle(dc, lx, y + 2, 240, L"Effects on my voice", gPlugVoice.on.load(), [] { gPlugVoice.on = !gPlugVoice.on.load(); plugSave(); });
        const RECT folder = {x1 - 180, y, x1, y + 26};
        stButton(dc, folder, L"VST plugin folder...");
        stArea(folder, [h] { plugPickFolder(h); });
    }
    const std::wstring hint = !plugNote.empty() ? plugNote : L"Click an effect to open it  \x00B7  right-click for more";
    stText(dc, RECT{x1 - 380, y2, x1, y2 + 22}, hint.c_str(), plugNote.empty() ? S_DIM : S_RED, sfSmall, DT_RIGHT);
    plugins::Chain& c = plugChain(plugTab);
    const int ey = y2 + 28, eh = box.bottom - 6 - ey, tw = 118;
    int x = x0;
    for (int i = 0; i < c.size() && i < plugins::Chain::kMaxSlots; ++i) {
        vst2::Plugin* p = c.at(i);
        if (!p) continue;
        const RECT t = {x, ey, x + tw, ey + eh};
        const bool off = c.bypassed(i), open = p->editorOpen(), hot = stHot(t);
        stFill(dc, t, open ? mixc(S_SUB, S_TEAL, 0.25) : (hot ? S_VALUE : S_SUB));
        stFrame(dc, t, open || hot ? S_TEAL : S_LINE);
        stText(dc, RECT{t.left + 8, t.top + 4, t.right - 6, t.top + 26}, p->title().c_str(), off ? S_DIM : S_TEXT, sfBold);
        stCaps(dc, RECT{t.left + 8, t.top + 26, t.right - 6, t.bottom - 2}, off ? L"OFF" : (p->builtin() ? L"PIXL" : L"VST"), off ? S_RED : S_DIM, DT_LEFT);
        StArea a{t, [p] { plugToggleWindow(p); }};
        a.right = [h, t, i] { stPlugMenu(h, t, i); };
        stAreas.push_back(std::move(a));
        x += tw + 8;
        if (i < c.size() - 1) stText(dc, RECT{x - 8, ey, x, ey + eh}, L"\x203A", S_DIM, sfBrand, DT_CENTER);
    }
    if (c.size() < plugins::Chain::kMaxSlots && x + 100 <= x1) {
        const RECT add = {x, ey, x + 100, ey + eh};
        stButton(dc, add, L"+ Add");
        stArea(add, [h, add] { stPlugAddMenu(h, add); });
    }
    if (c.size() == 0) stText(dc, RECT{x + 112, ey, x1, ey + eh}, L"Pixl's own (Music Enhancer, EQ, Reverb...) or a VST plugin", S_DIM, sfSmall);
}

// ---------------------------------------------------------------- Record

// The recorder's wave is the classic one, drawn and clicked through a map: same width, three times as tall.
static const int ST_WAVE_X = 60, ST_WAVE_Y = 470, ST_WAVE_K = 3;
static RECT stWaveRect() {
    return {ST_WAVE_X, ST_WAVE_Y, ST_WAVE_X + (R_REC_WAVE.right - R_REC_WAVE.left), ST_WAVE_Y + (R_REC_BAR.bottom - R_REC_WAVE.top) * ST_WAVE_K};
}
static POINT stToWave(POINT p) {
    return {R_REC_WAVE.left + (p.x - ST_WAVE_X), R_REC_WAVE.top + (p.y - ST_WAVE_Y) / ST_WAVE_K};
}
static bool stWaveDrag = false;

// A recorder button: the classic one pressed (its rect's middle), so it does exactly what it does there.
static void stTakeButton(HDC dc, const RECT& r, const RECT& classic, const wchar_t* text, int icon, bool enabled, bool lit = false, COLORREF litColor = S_TEAL) {
    const bool hot = enabled && stHot(r);
    stFill(dc, r, lit ? litColor : (hot ? S_VALUE : S_SUB));
    stFrame(dc, r, lit ? litColor : (hot ? S_TEAL : S_LINE));
    const COLORREF ink = !enabled ? S_LINE : (lit ? S_INK : S_TEXT);
    if (icon != ICO_NONE && text) {
        drawIcon(dc, r.left + 16.0f, (r.top + r.bottom) / 2.0f, icon, ink, lit ? litColor : (hot ? S_VALUE : S_SUB));
        stText(dc, RECT{r.left + 30, r.top, r.right - 6, r.bottom}, text, ink, sfBold);
    } else if (icon != ICO_NONE) {
        drawIcon(dc, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, icon, ink, lit ? litColor : (hot ? S_VALUE : S_SUB));
    } else {
        stText(dc, r, text, ink, sfBold, DT_CENTER);
    }
    if (enabled) stArea(r, [classic] { if (hwndMain) takeClick(hwndMain, POINT{(classic.left + classic.right) / 2, (classic.top + classic.bottom) / 2}); });
}

static void stTabRecord(HDC dc, HWND h) {
    RECT in = stModule(dc, 40, 1020, L"RECORDER");
    const bool rec = recording(), ready = takeReady(), hear = recHear();
    const bool ear = take.playing == TAKE_EAR, mic = take.playing == TAKE_MIC;
    int x = in.left + 14;
    const int y = in.top + 4, bh = 32;
    auto at = [&](int w) { const RECT r = {x, y, x + w, y + bh}; x += w + 6; return r; };
    stTakeButton(dc, at(40), R_REC_FROM_VOICE, nullptr, ICO_MIC, !rec, recVoice());
    stTakeButton(dc, at(40), R_REC_FROM_HEAR, nullptr, ICO_SPEAKER, !rec, hear);
    stTakeButton(dc, at(110), R_REC_GO, rec ? L"Stop" : L"Record", rec ? ICO_STOP : ICO_RECORD, true, rec, S_RED);
    {
        const RECT t = at(70);
        const double secs = rec ? recTake.size() / 2.0 / FS : take.w.length();
        stText(dc, t, trimClock(secs, 1).c_str(), rec ? S_RED : (ready ? S_TEXT : S_DIM), sfNum, DT_CENTER);
    }
    x += 10;
    stTakeButton(dc, at(28), R_REC_LAST_PREV, nullptr, ICO_PREV, !rec && keepIndex() > 0);
    stTakeButton(dc, at(150), R_REC_LAST, (keepSec ? L"Last " + keepText(keepSec) : std::wstring(L"Last: off")).c_str(), ICO_BACK, !rec && keepSec > 0);
    stTakeButton(dc, at(28), R_REC_LAST_NEXT, nullptr, ICO_NEXT, !rec && keepIndex() < NUM_KEEP_STEPS - 1);
    stTakeButton(dc, at(40), R_REC_LAST_QUIET, nullptr, ICO_HUSH, true, lastQuiet);
    x = in.right - 5 * 46;
    stTakeButton(dc, at(40), R_REC_FX, nullptr, ICO_MASK, ready, take.working);
    stTakeButton(dc, at(40), R_REC_LISTEN, nullptr, ear ? ICO_STOP : ICO_HEADPHONES, ready, ear);
    stTakeButton(dc, at(40), R_REC_SEND, nullptr, mic ? ICO_STOP : ICO_SEND, ready, mic);
    stTakeButton(dc, at(40), R_REC_SAVE, nullptr, ICO_SAVE, ready && !takeSaving);
    stTakeButton(dc, at(40), R_REC_DROP, nullptr, ICO_TRASH, ready && !takeSaving);

    // The wave: drawn by the classic code through a map onto this rect.
    const RECT wr = stWaveRect();
    stFill(dc, RECT{wr.left - 6, wr.top - 6, wr.right + 6, wr.bottom + 6}, S_TRACK);
    {
        XFORM was;
        GetWorldTransform(dc, &was);
        XFORM map = {1.0f, 0.0f, 0.0f, (float)ST_WAVE_K, (float)(ST_WAVE_X - R_REC_WAVE.left), (float)(ST_WAVE_Y - R_REC_WAVE.top * ST_WAVE_K)};
        ModifyWorldTransform(dc, &map, MWT_LEFTMULTIPLY);
        panel(dc, R_REC_WAVE, C_BACK, C_EDGE);
        if (rec) {
            drawLiveWave(dc);
        } else if (take.w.ready()) {
            const std::vector<size_t> joins = take.edit.joins();
            WaveMarks marks;
            marks.joins = &joins;
            marks.fadeIn = take.fadeIn;
            marks.fadeOut = take.fadeOut;
            marks.fadeTo = take.w.length();
            if (take.playing != TAKE_QUIET)
                marks.playAt = take.playFrom + (take.playing == TAKE_EAR ? gTakeEar : gTakeMic).position(0) / (double)FS;
            drawWave(dc, take.w, marks);
            if (take.w.zoomed()) drawWaveBar(dc, take.w);
        }
        SetWorldTransform(dc, &was);
    }
    if (!rec && !take.w.ready())
        stText(dc, wr, recBoth() ? L"Press Record to record your voice and what you hear, or Last to grab what just played"
                                 : (hear ? L"Press Record to record what you hear, or Last to grab what just played"
                                         : L"Press Record and speak, or Last to grab what you just said"), S_DIM, sfBody, DT_CENTER);
    {
        // Drag on the wave to pick a part, click to play from there.
        StArea a{wr, nullptr};
        a.down = [h] {
            if (!takeReady()) return;
            if (waveDown(take.w, stToWave(stMouse))) { stWaveDrag = true; SetCapture(h); }
        };
        stAreas.push_back(std::move(a));
    }
    // Editing.
    x = in.left + 14;
    const int ey = wr.bottom + 16;
    auto at2 = [&](int w) { const RECT r = {x, ey, x + w, ey + 30}; x += w + 6; return r; };
    stTakeButton(dc, at2(100), R_REC_CUT, L"Cut out", ICO_CUT, ready);
    stTakeButton(dc, at2(100), R_REC_KEEP, L"Keep only", ICO_NONE, ready);
    stTakeButton(dc, at2(36), R_REC_UNDO, nullptr, ICO_UNDO, (ready && take.edit.canUndo()) || takeRestorable());
    stTakeButton(dc, at2(36), R_REC_REDO, nullptr, ICO_REDO, ready && take.edit.canRedo());
    stTakeButton(dc, at2(36), R_REC_QUIET, nullptr, ICO_HUSH, ready);
    x += 30;
    stText(dc, RECT{x, ey, x + 60, ey + 30}, L"Fade in", ready ? S_DIM : S_LINE, sfBody);
    x += 64;
    stTakeButton(dc, at2(28), R_REC_FIN_PREV, nullptr, ICO_PREV, ready);
    stTakeButton(dc, at2(60), R_REC_FIN, fadeText(take.fadeIn).c_str(), ICO_NONE, ready, ready && take.fadeIn > 0.0);
    stTakeButton(dc, at2(28), R_REC_FIN_NEXT, nullptr, ICO_NEXT, ready);
    x += 20;
    stText(dc, RECT{x, ey, x + 66, ey + 30}, L"Fade out", ready ? S_DIM : S_LINE, sfBody);
    x += 70;
    stTakeButton(dc, at2(28), R_REC_FOUT_PREV, nullptr, ICO_PREV, ready);
    stTakeButton(dc, at2(60), R_REC_FOUT, fadeText(take.fadeOut).c_str(), ICO_NONE, ready, ready && take.fadeOut > 0.0);
    stTakeButton(dc, at2(28), R_REC_FOUT_NEXT, nullptr, ICO_NEXT, ready);
    if (takeNoteShown()) stText(dc, RECT{in.left + 14, ey + 40, in.right - 14, ey + 60}, take.note.c_str(), recording() ? S_RED : S_TEXT, sfBody);
    else stText(dc, RECT{in.left + 14, ey + 40, in.right - 14, ey + 60}, (L"Saved recordings go to " + clipsFolder()).c_str(), S_DIM, sfSmall);
}

// The wave's drag and its end, from the window's mouse handling.
static void stWaveMove(POINT p) {
    if (!stWaveDrag) return;
    if (waveMove(take.w, stToWave(p), nullptr)) take.note.clear();
}
static void stWaveUp(POINT p) {
    if (!stWaveDrag) return;
    stWaveDrag = false;
    ReleaseCapture();
    const int x = stToWave(p).x;
    const bool click = waveUp(take.w, x);
    if (click) takePlayAt(hwndMain, take.w.timeAt(x));
    else takeSay(take.w.whole() ? L"" : L"Picked " + trimClock(take.w.to - take.w.from) + L". Cut out removes it, Keep only keeps just that part");
}

// ---------------------------------------------------------------- System

static void stTabSystem(HDC dc, HWND h) {
    // General: the look, starting, behaviour, the clips folder.
    RECT in = stModule(dc, 40, 300, L"GENERAL");
    {
        const RECT nw = {in.left, in.top, in.left + 140, in.top + 28}, cl = {in.left + 146, in.top, in.right, in.top + 28};
        stButton(dc, nw, L"NEW LOOK", true, S_TEAL, true);
        stButton(dc, cl, L"CLASSIC LOOK", false, S_TEAL, true);
        stArea(cl, [] { studioUse(false); });
    }
    const int w = in.right - in.left;
    stToggle(dc, in.left, in.top + 40, w, L"Start with Windows", startupOn, [] { if (startupSet(!startupOn)) startupOn = !startupOn; oldStartupTasks(hwndMain, true); });
    stToggle(dc, in.left, in.top + 64, w, L"Animations", uiAnim, [] { uiAnim = !uiAnim; settingsSave(); });
    stToggle(dc, in.left, in.top + 88, w, L"Always on top", uiTop, [] { uiTop = !uiTop; applyTopmost(hwndMain); if (hStudio) applyTopmost(hStudio); settingsSave(); });
    stToggle(dc, in.left, in.top + 112, w, L"Remember where the window was", uiPlace, [] { uiPlace = !uiPlace; settingsSave(); });
    stToggle(dc, in.left, in.top + 136, w, L"Show hints", uiHints, [] { uiHints = !uiHints; if (hTip) SendMessageW(hTip, TTM_ACTIVATE, uiHints ? TRUE : FALSE, 0); settingsSave(); });
    stText(dc, RECT{in.left, in.top + 168, in.right, in.top + 186}, L"Saved clips and recordings", S_DIM, sfSmall);
    const RECT op = {in.left, in.top + 188, in.left + 140, in.top + 214}, ch = {in.left + 146, in.top + 188, in.right, in.top + 214};
    stButton(dc, op, L"Open folder");
    stArea(op, [] { clipsOpen(); });
    stButton(dc, ch, L"Change...");
    stArea(ch, [h] { clipsPick(h); });

    // Devices.
    in = stModule(dc, 348, 340, L"DEVICES");
    // The device boxes draw their own items (no strings in them): the names come from the lists they were filled from.
    auto devNow = [](HWND box, const std::vector<Dev>& devs) {
        const int i = box ? (int)SendMessageW(box, CB_GETCURSEL, 0, 0) : -1;
        return i >= 0 && i < (int)devs.size() ? devs[i].name : std::wstring(L"(none)");
    };
    auto devNames = [](const std::vector<Dev>& devs) { std::vector<std::wstring> v; for (const Dev& d : devs) v.push_back(d.name); return v; };
    stText(dc, RECT{in.left, in.top, in.right, in.top + 18}, L"Microphone", S_DIM, sfSmall);
    stPicker(dc, h, RECT{in.left, in.top + 18, in.right, in.top + 46}, devNow(hMic, uiMics), [devNames] { return devNames(uiMics); },
             [](int i) { stComboPick(hMic, ID_MIC, i); });
    stText(dc, RECT{in.left, in.top + 54, in.right, in.top + 72}, L"Speakers or headphones", S_DIM, sfSmall);
    stPicker(dc, h, RECT{in.left, in.top + 72, in.right, in.top + 100}, devNow(hSpk, uiSpks), [devNames] { return devNames(uiSpks); },
             [](int i) { stComboPick(hSpk, ID_SPK, i); });
    const RECT rf = {in.left, in.top + 108, in.left + 150, in.top + 134};
    stButton(dc, rf, L"Look again");
    stArea(rf, [] { devicesScan(hwndMain, true); });
    stToggle(dc, in.left, in.top + 144, in.right - in.left, L"Raw mic (no auto gain)", g.rawMic.load(), [] { g.rawMic = !g.rawMic.load(); g.reopenMic = true; });
    {
        wchar_t outName[256];
        {
            std::lock_guard<std::mutex> lk(g.devMx);
            wcsncpy_s(outName, g.cableName, 255);
        }
        stText(dc, RECT{in.left, in.top + 176, in.right, in.top + 194}, L"In Discord and games pick this microphone", S_DIM, sfSmall);
        stText(dc, RECT{in.left, in.top + 194, in.right, in.top + 216}, outName[0] ? outName : L"VB-Audio Virtual Cable (not found)", outName[0] ? S_TEXT : S_RED, sfBold);
    }

    // Hotkeys: click one, press the keys (Backspace clears, Esc keeps).
    in = stModule(dc, 696, 364, L"HOTKEYS");
    for (int a = 0; a < NUM_SHORTCUTS; ++a) {
        const int yy = in.top + a * 22;
        const RECT cap = {in.left, yy, in.left + 116, yy + 20};
        const bool listening = keyListen == a, hot = stHot(cap);
        stFill(dc, cap, listening ? S_TEAL : (hot ? S_VALUE : S_TRACK));
        stFrame(dc, cap, listening || hot ? S_TEAL : S_LINE);
        stText(dc, cap, listening ? L"Press keys" : keyLabel(a).c_str(), listening ? S_INK : (shortcutKey[a] ? S_TEXT : S_DIM), sfSmall, DT_CENTER);
        stText(dc, RECT{cap.right + 8, yy, in.right, yy + 20}, SHORTCUTS[a].what, S_DIM, sfSmall);
        stArea(cap, [h, a] { keyListenFlip(h, a); SetFocus(h); });
    }
    if (!keyNote.empty()) stText(dc, RECT{in.left, in.bottom - 18, in.right, in.bottom}, keyNote.c_str(), S_TEXT, sfSmall);

    // About and updates.
    stText(dc, RECT{40, 670, 1060, 690}, (L"Pixl " + update::current().text() + L"  \x00B7  programmed by 9i505  \x00B7  github.com/9i505/Pixl").c_str(), S_DIM, sfSmall);
}
