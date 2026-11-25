// The music effects' test bench (Music Enhancer, 8-Band EQ, Crossfeed, Volume Leveller): each is fed tones and
// noise and measured: what it does to each frequency, to each side, to loud and quiet songs, and that nothing
// ever passes its ceiling. Prints the numbers and FAIL lines; exits 1 when something is off.
//   cl /nologo /O2 /EHsc /std:c++17 tests\music\music_test.cpp && music_test.exe
#define NOMINMAX
#include <cstdio>
#include <random>
#include "../../src/plugins/pixl_effects.hpp"

using pfx::FS;
using pfx::PI;
typedef std::vector<float> Sig;
static int fails = 0;
static void check(bool ok, const char* what) { if (!ok) { ++fails; printf("  FAIL %s\n", what); } }
static double db(double g) { return 20.0 * std::log10(std::max(g, 1e-12)); }
static double rms(const Sig& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double)x[i] * x[i]; return std::sqrt(e / (b - a)); }
static double peak(const Sig& x) { float m = 0; for (float v : x) m = std::max(m, std::fabs(v)); return m; }
static Sig tone(double hz, double amp, double seconds) { Sig x((size_t)(seconds * FS)); for (size_t i = 0; i < x.size(); ++i) x[i] = (float)(amp * std::sin(2 * PI * hz * i / FS)); return x; }
// Noise shaped like music: the same energy in each octave (pink) up to 2 kHz, 10 dB less above it, as the long-term
// spectrum of recorded music runs.
static Sig pink(double rmsDb, double seconds, unsigned seed) {
    std::mt19937 rng(seed); std::normal_distribution<float> d(0, 1);
    Sig x((size_t)(seconds * FS)); double b[7] = {};
    pfx::BQ top; pfx::design(top, pfx::BQ_HSHELF, 2000.0, 0.707, -10.0);
    for (auto& v : x) {
        const double w = d(rng);
        b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759; b[2] = 0.969 * b[2] + w * 0.153852;
        b[3] = 0.8665 * b[3] + w * 0.3104856; b[4] = 0.55 * b[4] + w * 0.5329522; b[5] = -0.7616 * b[5] - w * 0.016898;
        v = top.run((float)(b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362)); b[6] = w * 0.115926;
    }
    const double k = std::pow(10.0, rmsDb / 20.0) / rms(x, 0, x.size());
    for (auto& v : x) v = (float)(v * k);
    return x;
}
static void run(pfx::Effect& e, Sig& l, Sig& r) {
    for (size_t i = 0; i < l.size(); i += 480) e.process(&l[i], &r[i], (int)std::min<size_t>(480, l.size() - i));
}
// What a steady tone comes out as, in dB against what went in (the last half second of two).
static double gainAt(const wchar_t* id, double hz, std::initializer_list<std::pair<int, float>> set, double amp = 0.05, bool leftOnly = false, bool readRight = false) {
    auto e = pfx::make(std::wstring(L"pixl:") + id);
    for (auto& s : set) e->setValue(s.first, s.second);
    Sig l = tone(hz, amp, 2.0), r = leftOnly ? Sig(l.size(), 0.f) : l;
    const double in = rms(l, l.size() * 3 / 4, l.size());
    run(*e, l, r);
    const Sig& out = readRight ? r : l;
    return db(rms(out, out.size() * 3 / 4, out.size()) / in);
}

int main() {
    printf("8-Band EQ\n");
    static const double HZ[8] = {60, 150, 400, 1000, 2500, 6000, 10000, 16000};
    double worst = 0;
    for (double f : {30.0, 100.0, 1000.0, 5000.0, 15000.0}) worst = std::max(worst, std::fabs(gainAt(L"eq8", f, {})));
    printf("  everything at 0 dB: within %.3f dB of the sound as it came\n", worst);
    check(worst < 0.01, "flat when every band is at 0");
    for (int b = 0; b < 8; ++b) {
        const double up = gainAt(L"eq8", b == 0 ? 30 : b == 7 ? 19000 : HZ[b], {{b, 6.f}}), away = gainAt(L"eq8", b < 4 ? 12000 : 40, {{b, 6.f}});
        printf("  %5.0f Hz at +6 dB: %+.2f dB there, %+.2f dB far from it\n", HZ[b], up, away);
        check(std::fabs(up - 6.0) < (b == 0 || b == 7 ? 0.8 : 0.1) && std::fabs(away) < 0.1, "a band moves its own frequencies only");
    }

    printf("Music Enhancer (as added: Bass +2, Clean 40%%, Vocals +1, Air +2, Smooth 50%%)\n");
    for (double f : {40.0, 90.0, 300.0, 1000.0, 2800.0, 16000.0}) printf("  %5.0f Hz: %+.2f dB\n", f, gainAt(L"enhancer", f, {{4, 0.f}}));
    check(std::fabs(gainAt(L"enhancer", 40, {{4, 0.f}}) - 2.0) < 0.4, "Bass +2 dB under 90 Hz");
    check(std::fabs(gainAt(L"enhancer", 300, {{4, 0.f}}) + 1.6) < 0.5, "Clean 40%: 1.6 dB less at 300 Hz");
    check(std::fabs(gainAt(L"enhancer", 16000, {{4, 0.f}}) - 2.0) < 0.5, "Air +2 dB over 11 kHz");
    worst = 0;
    for (double f : {40.0, 300.0, 1000.0, 2800.0, 4500.0, 16000.0}) worst = std::max(worst, std::fabs(gainAt(L"enhancer", f, {{0, 0.f}, {1, 0.f}, {2, 0.f}, {3, 0.f}, {4, 0.f}})));
    printf("  every knob at nothing: within %.3f dB\n", worst);
    check(worst < 0.01, "nothing changed with every knob at nothing");
    {
        // Smooth: music with its usual top is left alone; the same music with a harsh 4.5 kHz band is turned down there.
        auto measure = [](double harshDb, float smooth) {
            auto e = pfx::make(L"pixl:enhancer");
            for (int i = 0; i < 4; ++i) e->setValue(i, 0.f);
            e->setValue(4, smooth);
            Sig l = pink(-20, 4.0, 7), h = tone(4500, std::pow(10.0, harshDb / 20.0), 4.0);
            for (size_t i = 0; i < l.size(); ++i) l[i] += h[i];
            Sig r = l, in = l;
            run(*e, l, r);
            // The tone's level out against in, by correlation with the tone itself (72 samples late: the ceiling).
            double so = 0, si = 0;
            for (size_t i = l.size() / 2; i < l.size(); ++i) { so += l[i] * std::sin(2 * PI * 4500 * (i - 71.0) / FS); si += in[i] * std::sin(2 * PI * 4500 * (double)i / FS); }
            double co = 0, ci = 0;
            for (size_t i = l.size() / 2; i < l.size(); ++i) { co += l[i] * std::cos(2 * PI * 4500 * (i - 71.0) / FS); ci += in[i] * std::cos(2 * PI * 4500 * (double)i / FS); }
            return db(std::hypot(so, co) / std::hypot(si, ci));
        };
        const double calm = measure(-60, 100), harsh50 = measure(-22, 50), harsh100 = measure(-22, 100), off = measure(-22, 0);
        printf("  Smooth on calm music: %+.2f dB at 4.5 kHz; on a harsh band: %+.2f dB at 50%%, %+.2f dB at 100%%, %+.2f dB at 0%%\n", calm, harsh50, harsh100, off);
        check(std::fabs(off) < 0.05, "Smooth 0% leaves the band alone");
        check(harsh100 < -4.0 && harsh100 > -6.5 && harsh50 < -1.5 && harsh50 > harsh100, "Smooth turns a harsh band down, more at 100%");
        check(calm > -1.5, "Smooth leaves calm music nearly alone");
    }
    {
        auto e = pfx::make(L"pixl:enhancer");
        e->setValue(0, 9.f); e->setValue(2, 6.f); e->setValue(3, 9.f); e->setValue(5, 6.f);
        Sig l = pink(-8, 5.0, 3), r = pink(-8, 5.0, 4);
        run(*e, l, r);
        printf("  every boost at most on loud music: peak %.3f (ceiling 0.966)\n", std::max(peak(l), peak(r)));
        check(std::max(peak(l), peak(r)) <= 0.9661, "never over its ceiling");
    }

    printf("Crossfeed (as added: 70%%, 700 Hz)\n");
    const double midLow = gainAt(L"crossfeed", 100, {}), midHigh = gainAt(L"crossfeed", 8000, {});
    const double sameLow = gainAt(L"crossfeed", 100, {}, 0.05, true), otherLow = gainAt(L"crossfeed", 100, {}, 0.05, true, true);
    const double sameHigh = gainAt(L"crossfeed", 8000, {}, 0.05, true), otherHigh = gainAt(L"crossfeed", 8000, {}, 0.05, true, true);
    printf("  a sound in the middle: %+.2f dB at 100 Hz, %+.2f dB at 8 kHz\n", midLow, midHigh);
    printf("  a sound on the left only, 100 Hz: left %+.2f dB, right %+.2f dB; 8 kHz: left %+.2f dB, right %+.2f dB\n", sameLow, otherLow, sameHigh, otherHigh);
    check(std::fabs(midLow) < 0.2 && midHigh > -3.0 && midHigh < 0.0, "the middle keeps its lows, loses under 3 dB of top");
    check(otherLow - sameLow > -6.0 && otherLow - sameLow < -2.0, "lows reach the other ear a few dB down");
    check(otherHigh - sameHigh < -18.0, "highs stay on their side");
    check(std::fabs(gainAt(L"crossfeed", 1000, {{0, 0.f}}, 0.05, true)) < 0.001 && gainAt(L"crossfeed", 1000, {{0, 0.f}}, 0.05, true, true) < -200, "0%: untouched");

    printf("Volume Leveller (as added: -16 LUFS, boost 10, cut 12, Normal)\n");
    auto level = [](double inDb, std::initializer_list<std::pair<int, float>> set, double seconds, double* pk = nullptr) {
        auto e = pfx::make(L"pixl:leveller");
        for (auto& s : set) e->setValue(s.first, s.second);
        Sig l = pink(inDb, seconds, 11), r = pink(inDb, seconds, 12);
        run(*e, l, r);
        if (pk) *pk = std::max(peak(l), peak(r));
        return db(rms(l, l.size() - (size_t)(2 * FS), l.size())) - inDb;
    };
    double pk = 0;
    const double quiet = level(-25, {}, 20), mid = level(-22, {}, 20), loud = level(-10, {}, 20, &pk), veryQuiet = level(-50, {}, 30);
    printf("  a song at -25 dB: %+.1f dB; at -22 dB: %+.1f dB; at -10 dB: %+.1f dB (peak %.3f); at -50 dB: %+.1f dB (boost stops at 10)\n", quiet, mid, loud, pk, veryQuiet);
    check(std::fabs((quiet - 25) - (mid - 22)) < 0.5 && std::fabs((mid - 22) - (loud - 10)) < 0.5, "quiet and loud songs come out at the same loudness");
    check(std::fabs(veryQuiet - 10.0) < 0.3, "never more than Max boost");
    check(pk <= 0.8911, "never over its ceiling");
    {
        // A quiet song, 5 s of silence, the song again: the volume is still up when it returns.
        auto e = pfx::make(L"pixl:leveller");
        Sig l = pink(-34, 15.0, 21), r = pink(-34, 15.0, 22), gapL(5 * 48000, 0.f), gapR = gapL, l2 = pink(-34, 1.0, 23), r2 = pink(-34, 1.0, 24);
        run(*e, l, r);
        const double before = db(rms(l, l.size() - 48000, l.size())) + 34;
        run(*e, gapL, gapR); run(*e, l2, r2);
        const double after = db(rms(l2, 0, 24000)) + 34;
        printf("  volume before a 5 s silence: %+.1f dB; right after it: %+.1f dB\n", before, after);
        check(std::fabs(before - after) < 0.5, "silence holds the volume");
        // A loud song right after: down within a few seconds, never over the ceiling meanwhile.
        Sig l3 = pink(-10, 6.0, 25), r3 = pink(-10, 6.0, 26);
        run(*e, l3, r3);
        const double first = db(rms(l3, 0, 24000)) + 10, later = db(rms(l3, l3.size() - 48000, l3.size())) + 10;
        printf("  a loud song right after the quiet one: %+.1f dB in its first half second (peak %.3f), %+.1f dB after 5 s\n", first, peak(l3), later);
        check(peak(l3) <= 0.8911 && later < -3.0, "a loud song after a quiet one is caught and turned down");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
