#pragma once
// App mixer: groups programs into strips like Voicemeeter. Up to 0 dB a strip sets its programs' Windows volume
// (exact, and no delay). Above it, up to +12 dB, Pixl plays them itself: the program is turned down to 0.1% where it
// plays and its captured sound goes straight back to the same device, louder (about 46 ms later, Windows' capture).
// Strips also mute or solo their programs, add an echo and can send them to the mic (B1).

#include "../playback/program_tap.hpp"
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <map>
#include <functional>
#include <memory>

namespace mixer {

constexpr int STRIPS = 3;
constexpr int OUTS = 3;
constexpr int MAX_APPS = 6;
constexpr int MIN_DB10 = -600;
constexpr int MAX_DB10 = 120;   // +12 dB: Pixl plays the program louder than it plays itself
constexpr const wchar_t* DUCK_KEY = L"Software\\Pixl\\Mixer turned down";  // volumes to give back after a crash

inline float volumeOf(int db10) { return db10 <= MIN_DB10 ? 0.0f : powf(10.0f, db10 / 200.0f); }

// The fader's travel (0 bottom .. 1000 top) against dB, like a desk fader: 0 dB sits at 86%, the 6 dB under it take a
// sixth of the travel, where the ear tells small steps apart, and the quiet end is squeezed. Straight lines between.
struct LawPoint { int pos, db10; };
constexpr LawPoint FADER_LAW[] = {{0, MIN_DB10}, {180, -400}, {360, -250}, {540, -140}, {720, -60}, {860, 0}, {1000, MAX_DB10}};
constexpr int LAW_N = sizeof FADER_LAW / sizeof FADER_LAW[0];

inline int faderDb10(int pos) {
    if (pos <= 0) return MIN_DB10;
    if (pos >= 1000) return MAX_DB10;
    int k = 1;
    while (FADER_LAW[k].pos < pos) ++k;
    const LawPoint a = FADER_LAW[k - 1], b = FADER_LAW[k];
    return a.db10 + (int)lround((double)(b.db10 - a.db10) * (pos - a.pos) / (b.pos - a.pos));
}
inline int faderPos(int db10) {
    if (db10 <= MIN_DB10) return 0;
    if (db10 >= MAX_DB10) return 1000;
    int k = 1;
    while (FADER_LAW[k].db10 < db10) ++k;
    const LawPoint a = FADER_LAW[k - 1], b = FADER_LAW[k];
    return a.pos + (int)lround((double)(b.pos - a.pos) * (db10 - a.db10) / (b.db10 - a.db10));
}

struct Strip {
    std::wstring title;
    std::vector<std::wstring> apps;
    int db10 = 0;  // fader in tenths of a dB, MIN_DB10 (bottom) is silence
    bool mute = false, solo = false, b1 = false, echo = false;
    bool audio = true;  // off: you hear nothing; with B1 the program still reaches the mic
    int out = -1;
};

inline bool sameName(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

struct Mixer {
    std::mutex mx;
    Strip strips[STRIPS];
    std::wstring outs[OUTS];
    std::atomic<float> level[STRIPS];
    std::atomic<bool> b1Live{false};
    std::atomic<bool> echoLive{false};
    std::atomic<bool> echoAny{false};  // some program has Echo on (heard or mic only)
    std::wstring skipName;
    std::wstring duckName;  // the program Pixl turned down to play it itself: its volume is Pixl's
    std::function<std::wstring()> cableFinder;  // id of the VB-CABLE playback device (the mic path)

    Mixer() {
        for (int i = 0; i < STRIPS; ++i) { strips[i].title = L"Audio " + std::to_wstring(i + 1); level[i] = 0.0f; }
    }
    ~Mixer() { stop(); }

    void start(int fs) {
        if (th.joinable()) return;
        rate = fs;
        quit = false;
        wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        th = std::thread([this] { run(); });
    }

    void stop() {
        if (!th.joinable()) return;
        quit = true;
        if (wake) SetEvent(wake);
        th.join();
        if (wake) { CloseHandle(wake); wake = nullptr; }
    }

    void poke() { fullDue = true; if (wake) SetEvent(wake); }
    // Only a fader moved: set the volumes on the sessions already found, at once, without the full scan.
    void volumeNow() { volumeDue = true; if (wake) SetEvent(wake); }

    // Called by the audio thread. dry: Mic programs for the mic. Echo buses: mic: heard and on the
    // mic. mine: only heard. cab: only on the mic (Audio off).
    void pull(float* dryL, float* dryR, float* micL, float* micR, float* mineL, float* mineR, float* cabL, float* cabR, int n) {
        for (float* b : {dryL, dryR, micL, micR, mineL, mineR, cabL, cabR}) std::fill(b, b + n, 0.0f);
        std::unique_lock<std::mutex> lk(tapMx, std::try_to_lock);
        if (!lk.owns_lock()) return;
        tl.resize((size_t)n); tr.resize((size_t)n);
        for (auto& t : taps) {
            const Tap& tp = t.second;
            if (!tp.tap->active()) continue;
            tp.tap->pull(tl.data(), tr.data(), n);  // always drained, so it stays in step
            if (!tp.open) continue;
            const float g = tp.gain;
            if (tp.dry) for (int i = 0; i < n; ++i) { dryL[i] += tl[i] * g; dryR[i] += tr[i] * g; }
            if (tp.echo) {
                float* l = !tp.hear ? cabL : tp.b1 ? micL : mineL;
                float* r = !tp.hear ? cabR : tp.b1 ? micR : mineR;
                for (int i = 0; i < n; ++i) { l[i] += tl[i] * g; r[i] += tr[i] * g; }
            }
        }
    }

    // Which strip holds a program, or -1.
    int stripOf(const std::wstring& name) {
        std::lock_guard<std::mutex> lk(mx);
        return findStrip(name);
    }

    void place(const std::wstring& name, int strip) {
        {
            std::lock_guard<std::mutex> lk(mx);
            for (Strip& s : strips)
                s.apps.erase(std::remove_if(s.apps.begin(), s.apps.end(), [&](const std::wstring& a) { return sameName(a, name); }), s.apps.end());
            if (strip >= 0 && strip < STRIPS && (int)strips[strip].apps.size() < MAX_APPS) strips[strip].apps.push_back(name);
        }
        poke();
    }

    void load(const std::wstring& key) {
        std::lock_guard<std::mutex> lk(mx);
        auto num = [&](const std::wstring& n, int def) {
            DWORD v = 0, cb = sizeof v;
            return RegGetValueW(HKEY_CURRENT_USER, key.c_str(), n.c_str(), RRF_RT_REG_DWORD, nullptr, &v, &cb) == ERROR_SUCCESS ? (int)v : def;
        };
        auto text = [&](const std::wstring& n) {
            wchar_t buf[2048];
            DWORD cb = sizeof buf - sizeof(wchar_t);
            return RegGetValueW(HKEY_CURRENT_USER, key.c_str(), n.c_str(), RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS ? std::wstring(buf) : std::wstring();
        };
        for (int o = 0; o < OUTS; ++o) outs[o] = text(L"Out" + std::to_wstring(o));
        for (int i = 0; i < STRIPS; ++i) {
            Strip& s = strips[i];
            const std::wstring p = L"S" + std::to_wstring(i);
            const std::wstring t = text(p + L"Name");
            if (!t.empty()) s.title = t;
            if (s.title.rfind(L"Strip ", 0) == 0) s.title = L"Audio " + s.title.substr(6);  // old default name
            {
                // Older saves kept a percentage; the fader now works in dB.
                const int pct = std::max(0, std::min(100, num(p + L"Gain", 100)));
                const int fromPct = pct <= 0 ? MIN_DB10 : (int)lroundf(200.0f * log10f(pct / 100.0f));
                s.db10 = std::max(MIN_DB10, std::min(MAX_DB10, num(p + L"GainDb10", fromPct)));
            }
            s.mute = num(p + L"Mute", 0) != 0;
            s.solo = num(p + L"Solo", 0) != 0;
            s.b1 = num(p + L"B1", 0) != 0;
            s.echo = false;  // a program's echo is set on the Plugins panel's Program tab now, not per strip
            s.audio = num(p + L"Audio", 1) != 0;
            s.out = -1;  // no A1-A3 buttons: programs play on their usual device
            s.apps.clear();
            const std::wstring all = text(p + L"Apps");
            size_t from = 0;
            while (from < all.size() && (int)s.apps.size() < MAX_APPS) {
                const size_t bar = all.find(L'|', from);
                const std::wstring a = all.substr(from, bar == std::wstring::npos ? std::wstring::npos : bar - from);
                if (!a.empty()) s.apps.push_back(a);
                if (bar == std::wstring::npos) break;
                from = bar + 1;
            }
        }
    }

    void save(const std::wstring& key) {
        std::lock_guard<std::mutex> lk(mx);
        HKEY hk;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) != ERROR_SUCCESS) return;
        auto putNum = [&](const std::wstring& n, int v) { DWORD d = (DWORD)v; RegSetValueExW(hk, n.c_str(), 0, REG_DWORD, (const BYTE*)&d, sizeof d); };
        auto putText = [&](const std::wstring& n, const std::wstring& v) {
            RegSetValueExW(hk, n.c_str(), 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
        };
        for (int o = 0; o < OUTS; ++o) putText(L"Out" + std::to_wstring(o), outs[o]);
        for (int i = 0; i < STRIPS; ++i) {
            const Strip& s = strips[i];
            const std::wstring p = L"S" + std::to_wstring(i);
            std::wstring all;
            for (const std::wstring& a : s.apps) { if (!all.empty()) all += L"|"; all += a; }
            putText(p + L"Name", s.title);
            putText(p + L"Apps", all);
            putNum(p + L"GainDb10", s.db10);
            putNum(p + L"Mute", s.mute);
            putNum(p + L"Solo", s.solo);
            putNum(p + L"B1", s.b1);
            putNum(p + L"Echo", s.echo);
            putNum(p + L"Audio", s.audio);
            putNum(p + L"Out", s.out);
        }
        RegCloseKey(hk);
    }

private:
    struct Tap {
        std::unique_ptr<clip::ProgramTap> tap;
        bool open = true, b1 = false, echo = false, dry = false, hear = true;
        int strip = -1;
        bool own = false;   // Pixl plays this program itself, on dev
        std::wstring dev;
        float gain = 1.0f;  // the strip's level on what the tap captured (1 when Windows' volume already set it)
    };
    struct Touched {
        float firstVolume = 1.0f;
        std::wstring routedTo;
        bool routed = false;
        std::vector<DWORD> pids;
    };

    std::thread th;
    std::atomic<bool> quit{false}, fullDue{false}, volumeDue{false};
    HANDLE wake = nullptr;
    std::vector<std::pair<int, ISimpleAudioVolume*>> held;  // strip, session volume: from the last scan, for volumeNow
    int rate = 48000;
    std::mutex tapMx;
    std::map<std::wstring, Tap> taps;
    std::map<std::wstring, Touched> touched;
    std::map<std::wstring, std::wstring> lostOn;  // program -> device its copy could not play on: Windows' volume there
    bool above[STRIPS] = {};  // each strip was above 0 dB at the last scan (Pixl plays its programs)
    std::vector<float> tl, tr;

    int findStrip(const std::wstring& name) const {
        for (int i = 0; i < STRIPS; ++i)
            for (const std::wstring& a : strips[i].apps)
                if (sameName(a, name)) return i;
        return -1;
    }

    static std::wstring lower(std::wstring s) {
        for (auto& c : s) c = towlower(c);
        return s;
    }

    struct Session {
        DWORD pid;
        std::wstring name;
        ISimpleAudioVolume* vol;
        IAudioMeterInformation* meter;
        std::wstring dev;  // the output it plays on
        bool active;       // playing right now
    };

    static std::vector<Session> sessions(const clip::ProcessTable& table) {
        std::vector<Session> out;
        IMMDeviceEnumerator* en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)) || !en) return out;
        IMMDeviceCollection* col = nullptr;
        if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
            UINT n = 0;
            col->GetCount(&n);
            for (UINT i = 0; i < n; ++i) {
                IMMDevice* d = nullptr;
                if (FAILED(col->Item(i, &d)) || !d) continue;
                std::wstring devId;
                LPWSTR rawId = nullptr;
                if (SUCCEEDED(d->GetId(&rawId)) && rawId) { devId = rawId; CoTaskMemFree(rawId); }
                IAudioSessionManager2* mgr = nullptr;
                if (SUCCEEDED(d->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr)) && mgr) {
                    IAudioSessionEnumerator* se = nullptr;
                    if (SUCCEEDED(mgr->GetSessionEnumerator(&se)) && se) {
                        int cnt = 0;
                        se->GetCount(&cnt);
                        for (int k = 0; k < cnt; ++k) {
                            IAudioSessionControl* sc = nullptr;
                            if (FAILED(se->GetSession(k, &sc)) || !sc) continue;
                            AudioSessionState st = AudioSessionStateExpired;
                            sc->GetState(&st);
                            IAudioSessionControl2* sc2 = nullptr;
                            if (st != AudioSessionStateExpired && SUCCEEDED(sc->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&sc2)) && sc2) {
                                DWORD pid = 0;
                                sc2->GetProcessId(&pid);
                                const std::wstring name = pid ? table.name(pid) : L"";
                                if (!name.empty()) {
                                    Session s{pid, name, nullptr, nullptr, devId, st == AudioSessionStateActive};
                                    sc->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&s.vol);
                                    sc->QueryInterface(__uuidof(IAudioMeterInformation), (void**)&s.meter);
                                    out.push_back(s);
                                }
                                sc2->Release();
                            }
                            sc->Release();
                        }
                        se->Release();
                    }
                    mgr->Release();
                }
                d->Release();
            }
            col->Release();
        }
        en->Release();
        return out;
    }

    static void route(const std::vector<DWORD>& pids, const std::wstring& deviceId) {
        clip::IAudioPolicyConfig* fac = clip::openPolicy();
        if (!fac) return;
        const HMODULE combase = GetModuleHandleW(L"combase.dll");
        auto mkStr = combase ? (HRESULT (WINAPI *)(const wchar_t*, UINT32, HSTRING*))GetProcAddress(combase, "WindowsCreateString") : nullptr;
        auto delStr = combase ? (HRESULT (WINAPI *)(HSTRING))GetProcAddress(combase, "WindowsDeleteString") : nullptr;
        HSTRING want = clip::makeHs(clip::packRenderId(deviceId), mkStr);
        HSTRING raw = clip::makeHs(deviceId, mkStr);
        static const ERole roles[3] = {eConsole, eMultimedia, eCommunications};
        for (DWORD pid : pids)
            for (ERole r : roles) {
                fac->SetPersistedDefaultAudioEndpoint(pid, eRender, r, want);
                if (raw) fac->SetPersistedDefaultAudioEndpoint(pid, eRender, r, raw);
            }
        if (want && delStr) delStr(want);
        if (raw && delStr) delStr(raw);
        fac->Release();
    }

    void step() {
        Strip snap[STRIPS];
        std::wstring outIds[OUTS], skip, duck;
        {
            std::lock_guard<std::mutex> lk(mx);
            for (int i = 0; i < STRIPS; ++i) snap[i] = strips[i];
            for (int o = 0; o < OUTS; ++o) outIds[o] = outs[o];
            skip = skipName;
            duck = duckName;
        }
        bool anySolo = false;
        for (const Strip& s : snap) anySolo = anySolo || s.solo;
        for (int i = 0; i < STRIPS; ++i) above[i] = snap[i].db10 > 0;
        auto stripFor = [&](const std::wstring& name) {
            for (int i = 0; i < STRIPS; ++i)
                for (const std::wstring& a : snap[i].apps)
                    if (sameName(a, name)) return i;
            return -1;
        };
        auto silent = [&](int i) { return snap[i].mute || (anySolo && !snap[i].solo); };
        // Audio off: you hear nothing. With Mic on the program plays straight into the mic
        // cable instead of your headphones, so the others still hear it.
        auto toCable = [&](int i) { return !snap[i].audio && snap[i].b1 && !silent(i); };
        auto quiet = [&](int i) { return quietIn(snap, anySolo, i); };
        releaseHeld();
        auto needTap = [&](int i) { return i >= 0; };  // every strip program is captured: its meter comes from it
        std::wstring cable;
        for (int i = 0; i < STRIPS; ++i)
            if (toCable(i) && !snap[i].apps.empty() && cableFinder) { cable = cableFinder(); break; }

        const clip::ProcessTable table;
        std::vector<Session> list = sessions(table);
        std::map<std::wstring, std::vector<DWORD>> pidsOf;
        std::map<std::wstring, DWORD> rootOf;
        // Where each strip program plays (a session that is playing wins), and whether Pixl plays it itself there: only
        // above 0 dB, and not when it goes to the mic cable (Audio off), not the program Pixl's effects already play or
        // park, and not on a device its copy could not play on before. Those keep Windows' volume, up to 0 dB.
        std::map<std::wstring, std::wstring> devOf;
        std::map<std::wstring, bool> devLive, ownOf;
        for (const Session& s : list) {
            const std::wstring key = lower(s.name);
            if (stripFor(s.name) < 0 || s.dev.empty()) continue;
            if (!devOf.count(key) || (s.active && !devLive[key])) { devOf[key] = s.dev; devLive[key] = s.active; }
        }
        for (auto& d : devOf) {
            const int i = stripFor(d.first);
            auto lost = lostOn.find(d.first);
            ownOf[d.first] = snap[i].db10 > 0 && snap[i].audio && !(!duck.empty() && sameName(d.first, duck)) && !(!skip.empty() && sameName(d.first, skip)) &&
                             !(lost != lostOn.end() && lost->second == d.second);
        }
        std::map<std::wstring, bool> owned;  // a copy already plays it
        {
            std::lock_guard<std::mutex> lk(tapMx);
            for (auto& t : taps) if (t.second.own && !t.second.tap->playLost.load()) owned[t.first] = true;
        }
        for (Session& s : list) {
            const int i = stripFor(s.name);
            const std::wstring key = lower(s.name);
            const bool ducked = !duck.empty() && sameName(s.name, duck);
            if (i >= 0) {
                if (s.vol && !ducked) {
                    auto seen = touched.find(key);
                    if (seen == touched.end()) {
                        float v = 1.0f;
                        s.vol->GetMasterVolume(&v);
                        seen = touched.emplace(key, Touched{}).first;
                        seen->second.firstVolume = clip::AppTakeover::isDucked(v) ? 1.0f : v;
                    }
                    if (!ownOf[key]) {
                        const float want = quiet(i) ? 0.0f : std::min(1.0f, volumeOf(snap[i].db10));
                        float have = -1.0f;
                        s.vol->GetMasterVolume(&have);
                        if (fabsf(have - want) > 0.0005f) s.vol->SetMasterVolume(want, nullptr);
                        s.vol->AddRef();
                        held.emplace_back(i, s.vol);
                    } else if (!owned.count(key)) {
                        // About to be played by Pixl: at 100% first, so the copy starts from 0 dB, where Windows'
                        // volume left off, and +6 dB is 6 dB over that.
                        float have = -1.0f;
                        s.vol->GetMasterVolume(&have);
                        if (!clip::AppTakeover::isDucked(have) && fabsf(have - 1.0f) > 0.0005f) s.vol->SetMasterVolume(1.0f, nullptr);
                    }
                }
                auto& v = pidsOf[key];
                if (std::find(v.begin(), v.end(), s.pid) == v.end()) v.push_back(s.pid);
                if (!rootOf.count(key)) rootOf[key] = table.root(s.pid);
            } else if (touched.count(key) && !ducked) {
                // Taken out of every strip: give back the volume it had.
                if (s.vol) s.vol->SetMasterVolume(touched[key].firstVolume, nullptr);
            }
            if (s.vol) s.vol->Release();
            if (s.meter) s.meter->Release();
        }

        // Output device per program: the mic cable while Audio is off and Mic is on, else the Windows default.
        for (auto& p : pidsOf) {
            const int i = stripFor(p.first);
            if (i < 0 || (!skip.empty() && sameName(p.first, skip))) continue;
            const std::wstring want = toCable(i) ? cable : L"";
            Touched& t = touched[p.first];
            bool fresh = false;
            for (DWORD pid : p.second)
                if (std::find(t.pids.begin(), t.pids.end(), pid) == t.pids.end()) { t.pids.push_back(pid); fresh = true; }
            if (want.empty() && !t.routed) continue;
            if (t.routed && t.routedTo == want && !fresh) continue;
            route(p.second, want);
            t.routedTo = want;
            t.routed = !want.empty();
        }
        // Programs that left their strip go back to the Windows default device.
        for (auto it = touched.begin(); it != touched.end();) {
            if (stripFor(it->first) >= 0) { ++it; continue; }
            if (it->second.routed) route(it->second.pids, L"");
            it = touched.erase(it);
        }

        // One loopback tap per strip program: it plays the program's copy (when Pixl plays it), feeds the meter, and
        // B1 and Echo when they are on.
        struct Want { int strip; bool own; std::wstring dev; };
        std::map<std::wstring, Want> wantTap;
        for (auto& r : rootOf) {
            const int i = stripFor(r.first);
            if (i < 0 || !needTap(i)) continue;
            const bool own = ownOf[r.first];
            wantTap[r.first] = Want{i, own, own ? devOf[r.first] : std::wstring()};
        }
        auto setFlags = [&](Tap& t, int i) {
            t.open = !quiet(i);
            t.dry = snap[i].b1 && snap[i].audio;  // with Audio off the cable already carries it
            t.b1 = snap[i].b1;
            t.echo = snap[i].echo;
            t.hear = snap[i].audio;
            t.strip = i;
            t.gain = t.own ? (quiet(i) ? 0.0f : volumeOf(snap[i].db10)) : 1.0f;
            if (t.own) t.tap->playGain = t.gain;
        };
        std::vector<std::unique_ptr<clip::ProgramTap>> drop;
        {
            std::lock_guard<std::mutex> lk(tapMx);
            // A tap lives until its program closes or leaves B1/Echo, or Pixl starts or stops playing it, or it moves
            // to another device. A program whose sound session blinks out between two scans keeps its tap, so the
            // echo never drops.
            for (auto it = taps.begin(); it != taps.end();) {
                const int i = stripFor(it->first);
                const auto w = wantTap.find(it->first);
                const bool lost = it->second.own && it->second.tap->playLost.load();
                if (lost) lostOn[it->first] = it->second.dev;
                const bool moved = w != wantTap.end() && (w->second.own != it->second.own || w->second.dev != it->second.dev);
                const bool dead = it->second.tap->finished() || i < 0 || !needTap(i) || moved || lost;
                if (dead) { drop.push_back(std::move(it->second.tap)); it = taps.erase(it); }
                else { setFlags(it->second, i); ++it; }
            }
            for (auto& w : wantTap) {
                if (taps.count(w.first)) continue;
                Tap t;
                t.tap = std::make_unique<clip::ProgramTap>();
                t.own = w.second.own;
                t.dev = w.second.dev;
                if (t.own) {
                    t.tap->playTo = t.dev;
                    t.tap->hijack.duckMemory.key = DUCK_KEY;
                } else {
                    t.tap->handsOff = true;
                }
                setFlags(t, w.second.strip);
                t.tap->start(clip::Program{rootOf[w.first], w.first}, rate, L"", t.own);
                taps.emplace(w.first, std::move(t));
            }
            b1Live = !taps.empty();
            bool echo = false;
            for (auto& t : taps) echo = echo || (t.second.echo && t.second.open && t.second.hear);
            echoLive = echo;
            bool anyEcho = false;
            for (auto& t : taps) anyEcho = anyEcho || (t.second.echo && t.second.open);
            echoAny = anyEcho;
        }
        drop.clear();
        // A copy that just stopped gave its program back its own volume: the strip's level again, at once.
        applyVolumes();
    }

    // Meters come from each program's own captured sound. Windows' session meters can't be
    // trusted here: on some setups several programs report the same level as another one.
    void readMeters() {
        float peak[STRIPS] = {};
        {
            std::lock_guard<std::mutex> lk(tapMx);
            for (auto& t : taps) {
                const int i = t.second.strip;
                if (i < 0 || i >= STRIPS) continue;
                peak[i] = std::max(peak[i], t.second.tap->peakHold.exchange(0.0f) * t.second.gain);
            }
        }
        for (int i = 0; i < STRIPS; ++i) level[i] = std::max(peak[i], level[i].load() * 0.75f);  // fall back smoothly
    }

    static bool quietIn(const Strip* snap, bool anySolo, int i) {
        const bool silent = snap[i].mute || (anySolo && !snap[i].solo);
        return silent || (!snap[i].audio && !snap[i].b1);
    }

    void releaseHeld() {
        for (auto& h : held) h.second->Release();
        held.clear();
    }

    // The fast path while a fader moves: the sessions the last scan found get the new volume straight away.
    void applyVolumes() {
        Strip snap[STRIPS];
        {
            std::lock_guard<std::mutex> lk(mx);
            for (int i = 0; i < STRIPS; ++i) snap[i] = strips[i];
        }
        bool anySolo = false;
        for (const Strip& s : snap) anySolo = anySolo || s.solo;
        // A fader that crossed 0 dB changes who plays the program: that needs the full scan, straight away.
        for (int i = 0; i < STRIPS; ++i)
            if ((snap[i].db10 > 0) != above[i]) { fullDue = true; if (wake) SetEvent(wake); }
        for (auto& h : held) {
            const float want = quietIn(snap, anySolo, h.first) ? 0.0f : std::min(1.0f, volumeOf(snap[h.first].db10));
            float have = -1.0f;
            if (SUCCEEDED(h.second->GetMasterVolume(&have)) && fabsf(have - want) > 0.0005f) h.second->SetMasterVolume(want, nullptr);
        }
        // The programs Pixl plays: their copy's level, picked up by the very next packet.
        std::lock_guard<std::mutex> lk(tapMx);
        for (auto& t : taps) {
            Tap& tp = t.second;
            if (!tp.own || tp.strip < 0 || tp.strip >= STRIPS) continue;
            tp.gain = quietIn(snap, anySolo, tp.strip) ? 0.0f : volumeOf(snap[tp.strip].db10);
            tp.tap->playGain = tp.gain;
        }
    }

    void giveBack() {
        releaseHeld();
        {
            std::lock_guard<std::mutex> lk(tapMx);
            taps.clear();
            b1Live = false;
            echoLive = false;
            echoAny = false;
        }
        std::wstring duck;
        {
            std::lock_guard<std::mutex> lk(mx);
            duck = duckName;
        }
        const clip::ProcessTable table;
        std::vector<Session> list = sessions(table);
        for (Session& s : list) {
            auto t = touched.find(lower(s.name));
            if (t != touched.end() && s.vol && !(!duck.empty() && sameName(s.name, duck))) s.vol->SetMasterVolume(t->second.firstVolume, nullptr);
            if (s.vol) s.vol->Release();
            if (s.meter) s.meter->Release();
        }
        for (auto& t : touched)
            if (t.second.routed) route(t.second.pids, L"");
        touched.clear();
        for (int i = 0; i < STRIPS; ++i) level[i] = 0.0f;
    }

    void run() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            // Programs Pixl left turned down when it did not close cleanly get their own volume back first.
            clip::ParkMemory ducked;
            ducked.key = DUCK_KEY;
            clip::healDucked(ducked, {});
        }
        ULONGLONG nextFull = 0;
        while (!quit.load()) {
            if (GetTickCount64() >= nextFull) {
                step();
                nextFull = GetTickCount64() + 700;
            } else {
                readMeters();
            }
            if (WaitForSingleObject(wake, 60) == WAIT_OBJECT_0) {
                if (fullDue.exchange(false)) nextFull = 0;
                else if (volumeDue.exchange(false)) applyVolumes();
            }
        }
        giveBack();
        if (SUCCEEDED(com)) CoUninitialize();
    }
};

}
