// The mic chain's test bench: checks every branch lines up in time, then runs hard cases (TTS speech plus made-up
// noise) through the AI at Min, default and Max, through the classic noise reduction, and through nothing, and
// measures what happened to the voice and to the noise (Hagerman: process speech+noise and speech-noise).
//   mic_test.exe <voices dir> <out dir> [--wavs]
// Build (Developer PowerShell): see docs\mic.md.
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "audio_kit.hpp"
#include "measure.hpp"
#include "../../src/isolation/mic_chain.hpp"
#pragma comment(lib, "gdiplus.lib")

using kit::Sig;
using kit::FS;
static std::wstring gVoices, gOut;
static std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }

struct Case { std::string id, title; Sig s, n; std::vector<int> presses; };

static Sig loadLine(const std::string& voice, const std::string& line, kit::Rng& r) {
    Sig x;
    if (!kit::readWav(gVoices + L"\\" + W(voice + "-" + line) + L".wav", x)) { fprintf(stderr, "missing %s-%s\n", voice.c_str(), line.c_str()); exit(2); }
    Sig y = kit::extendTop(x, r);
    kit::filt(y, kit::Biquad::highpass(60));
    return y;
}
static double activePower(const Sig& s) {
    double e = 0; long n = 0;
    for (auto& g : meas::segments(s)) { e += meas::energy(s, g.a, g.b); n += g.b - g.a; }
    return n ? e / n : kit::power(s);
}
static void setLevel(Sig& s, double dbfs) { kit::scale(s, std::sqrt(std::pow(10.0, dbfs / 10.0) / activePower(s))); }
static void setRms(Sig& n, double dbfs) { kit::scale(n, std::sqrt(std::pow(10.0, dbfs / 10.0) / std::max(kit::power(n), 1e-30))); }
static Sig place(const Sig& s, double lead, double tail) {
    Sig y((size_t)((lead + tail) * FS) + s.size(), 0.f);
    kit::addAt(y, s, (long)(lead * FS));
    return y;
}

static bool buildCase(int id, const std::string& v, Case& c) {
    kit::Rng r(1000 + id * 37 + (v == "zira" ? 7 : v == "mark" ? 13 : 0));
    auto steady = [&](Sig n, double db) { setRms(n, db); return n; };
    switch (id) {
    case 1: { c.id = "1-soft-start"; c.title = "Soft speech after silence, fan 15 dB SNR";
        Sig s = loadLine(v, "onset", r); setLevel(s, -38); c.s = place(s, 1.5, 0.8); c.n = steady(kit::fan((int)c.s.size(), r), -53); break; }
    case 2: { c.id = "2-endings"; c.title = "Quiet consonant endings, air conditioning 10 dB SNR";
        Sig s = loadLine(v, "endings", r); setLevel(s, -28); c.s = place(s, 1.2, 0.8); c.n = steady(kit::airCon((int)c.s.size(), r), -38); break; }
    case 3: { c.id = "3-whisper-fan"; c.title = "Whisper with a fan, 5 dB SNR";
        Sig s = kit::whisperize(loadLine(v, "whisper", r), r); setLevel(s, -42); c.s = place(s, 1.2, 0.8); c.n = steady(kit::fan((int)c.s.size(), r), -47); break; }
    case 4: { c.id = "4-typing"; c.title = "Speech while typing";
        Sig s = loadLine(v, "typing", r); setLevel(s, -28); c.s = place(s, 1.5, 1.0);
        const int n = (int)c.s.size(); kit::Events k = kit::typing(n, r, 0.3, (double)n / FS - 0.3, 0.22);
        c.n = steady(kit::fan(n, r), -58); for (int i = 0; i < n; ++i) c.n[i] += k.s[i]; c.presses = k.presses; break; }
    case 5: { c.id = "5-rapid"; c.title = "Rapid words, fan 12 dB SNR";
        Sig s = loadLine(v, "rapid", r); setLevel(s, -28); c.s = place(s, 1.2, 0.8); c.n = steady(kit::fan((int)c.s.size(), r), -40); break; }
    case 6: { c.id = "6-hum-pc"; c.title = "Hum, coil whine, PC fan, mouse clicks";
        Sig s = loadLine(v, "main", r); setLevel(s, -28); c.s = place(s, 1.5, 1.0); const int n = (int)c.s.size();
        c.n = steady(kit::hum(n, r), -50); Sig w = steady(kit::coilWhine(n, r), -64);
        kit::Events m = kit::mouse(n, r, 0.4, (double)n / FS - 0.4, 0.06); for (int i = 0; i < n; ++i) c.n[i] += w[i] + m.s[i]; c.presses = m.presses; break; }
    case 7: { c.id = "7-clean"; c.title = "Clean speech, no noise";
        Sig s = loadLine(v, "main", r); setLevel(s, -26); c.s = place(s, 1.0, 0.6); c.n.assign(c.s.size(), 0.f); break; }
    default: return false;
    }
    return true;
}

struct Sys { std::string name; iso::MicSettings ms; };

static Sig run(const Sig& x, const std::vector<int>& presses, const iso::MicSettings& ms) {
    auto c = std::make_unique<iso::MicChain>();
    c->set = ms; c->prepare(FS);
    Sig y = x;
    size_t hp = 0;
    for (size_t p = 0; p < y.size(); p += 480) {
        const int len = (int)std::min<size_t>(480, y.size() - p);
        while (hp < presses.size() && presses[hp] < (int)p + len + 720) { c->inputHint(); ++hp; }
        c->process(&y[p], len);
    }
    return y;
}

static int lagOf(const Sig& a, const Sig& b) {
    int best = 0; double bv = -1e30;
    for (int l = 0; l < 3000; ++l) {
        double s = 0; for (size_t i = 20000; i + l < b.size() && i < 60000; ++i) s += (double)a[i] * b[i + l];
        if (s > bv) { bv = s; best = l; }
    }
    return best;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { puts("mic_test <voices dir> <out dir> [--wavs]"); return 1; }
    gVoices = argv[1]; gOut = argv[2];
    CreateDirectoryW(gOut.c_str(), nullptr);
    const bool wavs = argc > 3 && std::wstring(argv[3]) == L"--wavs";
    meas::Gdi gdi;
    // Timing: every branch must come out exactly MicChain::latencySamples() late.
    {
        kit::Rng r(4);
        Case c; buildCase(5, "david", c);
        Sig x(c.s.size()); for (size_t i = 0; i < x.size(); ++i) x[i] = c.s[i] + 0.1f * c.n[i];
        iso::MicSettings off; off.aiOn = false;
        iso::MicSettings nr = off; nr.bgOn = true;
        iso::MicSettings ai;
        const Sig y0 = run(x, {}, off), y1 = run(x, {}, nr), y2 = run(x, {}, ai);
        double err = 0, sig = 0;
        const int L = iso::MicChain::latencySamples();
        for (size_t i = L; i < x.size(); ++i) { const double d = y0[i] - x[i - L]; err += d * d; sig += (double)x[i - L] * x[i - L]; }
        printf("timing: off %d samples (error %.1f dB), noise reduction %d, AI %d; want %d\n", lagOf(x, y0), kit::dbOf(err) - kit::dbOf(sig), lagOf(x, y1), lagOf(x, y2), L);
    }
    // Impact on typing alone (no speech): how many clicks it saw, ducked, took for speech, or found too long.
    {
        Case c; buildCase(4, "david", c);
        iso::MicSettings s; s.aiOn = false; s.impactOn = true;
        auto ch = std::make_unique<iso::MicChain>(); ch->set = s; ch->prepare(FS);
        Sig y = c.n;
        size_t hp = 0;
        for (size_t p = 0; p < y.size(); p += 480) {
            const int len = (int)std::min<size_t>(480, y.size() - p);
            while (hp < c.presses.size() && c.presses[hp] < (int)p + len + 720) { ch->inputHint(); ++hp; }
            ch->process(&y[p], len);
        }
        const int L = iso::MicChain::latencySamples();
        double ei = 0, eo = 0; for (size_t i = L; i < y.size(); ++i) { ei += (double)c.n[i - L] * c.n[i - L]; eo += (double)y[i] * y[i]; }
        printf("impact on typing alone: %zu presses; seen %d, ducked %d, taken for speech %d, too long %d; %.1f dB down\n", c.presses.size(), ch->statSeen, ch->statDucked,
               ch->statSpeech, ch->statLong, kit::dbOf(ei) - kit::dbOf(eo));
    }
    std::vector<Sys> systems;
    { iso::MicSettings s; s.aiLevel = 100; systems.push_back({"AI Max", s}); }
    { iso::MicSettings s; systems.push_back({"AI 70 (default)", s}); }
    { iso::MicSettings s; s.aiLevel = 0; systems.push_back({"AI Min", s}); }
    { iso::MicSettings s; s.aiOn = false; s.bgOn = true; s.impactOn = true; systems.push_back({"Noise reduction 50/50", s}); }
    { iso::MicSettings s; s.aiOn = false; systems.push_back({"Off", s}); }
    FILE* md = _wfopen((gOut + L"\\results.md").c_str(), L"w");
    const char* head = "| system | start loss (audible) avg / worst | end loss avg / worst | noise down in pauses | under speech | STOI in > out | cut |\n|---|---|---|---|---|---|---|";
    for (int id = 1; id <= 7; ++id) {
        std::string title;
        std::vector<std::string> rows;
        for (const auto& sys : systems) {
            double aOn = 0, aEnd = 0, wOn = 0, wEnd = 0, nrP = 0, nrS = 0, stIn = 0, stOut = 0; int nOn = 0, nEnd = 0, cut = 0, segs = 0, runs = 0;
            for (const char* v : {"david", "zira", "mark"}) {
                Case c; if (!buildCase(id, v, c)) continue;
                title = c.id + ": " + c.title;
                const size_t n = c.s.size();
                Sig xp(n), xm(n);
                for (size_t i = 0; i < n; ++i) { xp[i] = c.s[i] + c.n[i]; xm[i] = c.s[i] - c.n[i]; }
                const Sig y1 = run(xp, c.presses, sys.ms), y2 = run(xm, c.presses, sys.ms);
                Sig so(n), no(n);
                for (size_t i = 0; i < n; ++i) { so[i] = 0.5f * (y1[i] + y2[i]); no[i] = 0.5f * (y1[i] - y2[i]); }
                const int L = iso::MicChain::latencySamples();
                const Sig sd = kit::delayed(c.s, L), nd = kit::delayed(c.n, L), xd = kit::delayed(xp, L);
                const auto sg = meas::segments(sd);
                std::vector<char> pause(n, 1);
                for (auto& g : sg) for (int i = std::max(0, g.a - FS / 10); i < std::min((int)n, g.b + FS / 10); ++i) pause[i] = 0;
                for (int i = 0; i < std::min((int)n, L + FS / 2); ++i) pause[i] = 0;
                double np = 0, nop = 0, ns = 0, nos = 0;
                for (size_t i = 0; i < n; ++i) {
                    if (pause[i]) { np += (double)nd[i] * nd[i]; nop += (double)no[i] * no[i]; }
                    bool in = false; for (auto& g : sg) if ((int)i >= g.a && (int)i < g.b) { in = true; break; }
                    if (in) { ns += (double)nd[i] * nd[i]; nos += (double)no[i] * no[i]; }
                }
                if (np > 0) nrP += kit::dbOf(np) - kit::dbOf(nop);
                if (ns > 0) nrS += kit::dbOf(ns) - kit::dbOf(nos);
                for (auto& g : sg) {
                    double la = 0, le = 0;
                    const bool ha = meas::audibleLoss(sd, nd, so, g.a, g.a + FS * 30 / 1000, la), he = meas::audibleLoss(sd, nd, so, g.b - FS * 80 / 1000, g.b, le);
                    if (ha) { aOn += la; ++nOn; wOn = std::max(wOn, la); }
                    if (he) { aEnd += le; ++nEnd; wEnd = std::max(wEnd, le); }
                    if ((ha && la > 6) || (he && le > 6)) ++cut;
                }
                segs += (int)sg.size();
                stIn += meas::stoi(sd, xd); stOut += meas::stoi(sd, y1); ++runs;
                if (wavs || (std::string(v) == "david" && (sys.name == "AI 70 (default)" || sys.name == "Off"))) {
                    std::string f = sys.name; for (auto& ch : f) if (ch == ' ' || ch == '/' || ch == '(' || ch == ')') ch = '-';
                    kit::writeWav(gOut + L"\\" + W(c.id + "-" + v + "-" + f) + L".wav", y1);
                }
            }
            char b[400];
            const bool noisy = id != 7;
            snprintf(b, sizeof b, "| %s | %.1f / %.1f dB | %.1f / %.1f dB | %s | %s | %.2f > %.2f | %d of %d |", sys.name.c_str(), aOn / std::max(1, nOn), wOn, aEnd / std::max(1, nEnd), wEnd,
                     noisy ? (std::to_string((int)lround(nrP / runs)) + " dB").c_str() : "-", noisy ? (std::to_string((int)lround(nrS / runs)) + " dB").c_str() : "-", stIn / runs, stOut / runs, cut, segs);
            rows.push_back(b);
        }
        printf("\n%s\n%s\n", title.c_str(), head);
        if (md) fprintf(md, "\n### %s\n\n%s\n", title.c_str(), head);
        for (auto& r : rows) { printf("%s\n", r.c_str()); if (md) fprintf(md, "%s\n", r.c_str()); }
    }
    // CPU: everything on (AI, gate, compressor) over 60 s of sound.
    {
        Case c; buildCase(5, "david", c);
        Sig x(c.s.size()); for (size_t i = 0; i < x.size(); ++i) x[i] = c.s[i] + c.n[i];
        iso::MicSettings s; s.gateOn = true; s.compOn = true;
        auto ch = std::make_unique<iso::MicChain>(); ch->set = s; ch->prepare(FS);
        LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
        double secs = 0;
        while (secs < 60) { Sig y = x; for (size_t p = 0; p < y.size(); p += 480) ch->process(&y[p], (int)std::min<size_t>(480, y.size() - p)); secs += (double)x.size() / FS; }
        QueryPerformanceCounter(&b);
        const double cpu = (double)(b.QuadPart - a.QuadPart) / f.QuadPart / secs;
        printf("\nCPU: %.2f%% of one core\n", cpu * 100);
        if (md) fprintf(md, "\nCPU: %.2f%% of one core\n", cpu * 100);
    }
    if (md) fclose(md);
    return 0;
}
