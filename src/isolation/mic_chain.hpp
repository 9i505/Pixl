#pragma once
// The mic chain, laid out like SteelSeries Sonar's: AI noise cancellation (RNNoise), or classic noise reduction
// (Background and Impact) while the AI is off; then a noise gate and a compressor.
//
// Every branch runs all the time at the same delay (30 ms: 10 ms of buffering, 20 ms inside RNNoise), and switching
// between them crossfades, so turning anything on or off never clicks and never moves the voice in time.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>
#include "../convolution/fft.hpp"
#include "../third_party/rnnoise/include/rnnoise.h"

namespace iso {

struct MicSettings {
    bool aiOn = true;
    int aiLevel = 70;          // Min 0 .. Max 100: how much of the noise the AI may take away
    bool bgOn = false;
    int bg = 50;               // Background: steady noise (fans, hum, hiss), 0..100
    bool impactOn = false;
    int impact = 50;           // Impact: sudden noise (keys, clicks, knocks), 0..100
    bool gateOn = false;
    int gateDb10 = -500;       // threshold in tenths of a dB
    bool gateAuto = false;     // threshold follows the room
    bool compOn = false;
    int compLevel = 50;        // 0..100, shown as 0.00..1.00
    bool matchOn = false;      // Sonar match: the tone, evenness and level SteelSeries Sonar gave this mic
};

// Sonar match: the tone curve, leveller and output level measured from a recording of the same voice through a raw
// Fifine and through SteelSeries Sonar at once (docs/mic.md, "Sonar match"). Shelves, peaks and a steep top cut (Sonar
// stops at 16 kHz), then a slow soft-knee leveller, then the level.
struct ToneMatch {
    struct Params {
        float lsHz = 484.35f, lsDb = 29.78f, hsHz = 6136.51f, hsDb = 26.40f;
        float p1Hz = 343.43f, p1Db = -18.02f, p1Q = 2.99f, p2Hz = 5925.07f, p2Db = 23.35f, p2Q = 0.30f;
        float lpHz = 12251.76f;
        float thr = -15.03f, ratio = 2.4f, gainDb = -30.f;
    };
    struct Bq {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float run(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        // RBJ cookbook: 0 peak, 1 low shelf, 2 high shelf, 3 low pass.
        void design(int kind, double fs, double hz, double gainDb, double q) {
            const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979323846 * hz / fs, cw = std::cos(w), sw = std::sin(w);
            double B0, B1, B2, A0, A1, A2;
            if (kind == 0) {
                const double al = sw / (2.0 * q);
                B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A;
            } else if (kind == 3) {
                const double al = sw / (2.0 * q);
                B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al;
            } else {
                const double s2 = 2.0 * std::sqrt(A) * (sw / 2.0) * std::sqrt(2.0);  // 2 sqrt(A) alpha, shelf slope 1
                if (kind == 1) {
                    B0 = A * ((A + 1) - (A - 1) * cw + s2); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s2);
                    A0 = (A + 1) + (A - 1) * cw + s2; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s2;
                } else {
                    B0 = A * ((A + 1) + (A - 1) * cw + s2); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s2);
                    A0 = (A + 1) - (A - 1) * cw + s2; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s2;
                }
            }
            b0 = (float)(B0 / A0); b1 = (float)(B1 / A0); b2 = (float)(B2 / A0); a1 = (float)(A1 / A0); a2 = (float)(A2 / A0);
        }
    };
    Params p;
    Bq eq[6];
    float env = 0.f, gain = 1.f, fs = 48000.f, lastGr = 0.f;

    void configure(const Params& q, double rate) {
        p = q; fs = (float)rate;
        eq[0].design(1, rate, p.lsHz, p.lsDb, 0.707);
        eq[1].design(2, rate, p.hsHz, p.hsDb, 0.707);
        eq[2].design(0, rate, p.p1Hz, p.p1Db, p.p1Q);
        eq[3].design(0, rate, p.p2Hz, p.p2Db, p.p2Q);
        eq[4].design(3, rate, std::min(p.lpHz, 0.45f * fs), 0.0, 0.541);   // two together: a 4th-order Butterworth top cut
        eq[5].design(3, rate, std::min(p.lpHz, 0.45f * fs), 0.0, 1.307);
        gain = std::pow(10.f, p.gainDb / 20.f);
    }
    void reset() { for (Bq& b : eq) b.z1 = b.z2 = 0.f; env = 0.f; }
    float run(float x) {
        for (Bq& b : eq) x = b.run(x);
        const float e = x * x;
        env += (e > env ? 1.f / (0.010f * fs) : 1.f / (0.200f * fs)) * (e - env);
        const float over = 10.f * std::log10(env + 1e-20f) - p.thr, knee = 10.f;
        float gr = 0.f;
        if (over > knee / 2.f) gr = over * (1.f - 1.f / p.ratio);
        else if (over > -knee / 2.f) { const float k = over + knee / 2.f; gr = (1.f - 1.f / p.ratio) * k * k / (2.f * knee); }
        lastGr = gr;
        return x * gain * std::pow(10.f, -gr / 20.f);
    }
};

struct MicChain {
    static const int FRAME = 480;             // RNNoise's 10 ms at 48 kHz
    static const int NFFT = 256, HOP = 128;   // the classic noise reduction
    static const int RING = 2048, MASK = RING - 1;
    static const int AHEAD = 144;             // Impact ducks a click from 3 ms before it is heard
    static const int WAVE = 96;               // points of the waveform shown in the window

    MicSettings set;
    double fs = 48000.0;

    // Meters for the window (written by the audio thread).
    std::atomic<float> meterIn{-120.f}, meterOut{-120.f}, meterAiCut{0.f}, meterVoice{0.f}, meterGateDb{-50.f}, meterCompGr{0.f};
    std::atomic<bool> meterGateOpen{false};
    std::atomic<float> wave[WAVE];
    std::atomic<int> waveAt{0};
    int statSeen = 0, statDucked = 0, statSpeech = 0, statLong = 0;   // what Impact decided, for tests

    MicChain() { for (auto& w : wave) w.store(0.f); }
    ~MicChain() { if (den) rnnoise_destroy(den); }
    MicChain(const MicChain&) = delete;
    MicChain& operator=(const MicChain&) = delete;

    void prepare(double rate) {
        fs = rate;
        if (!den) den = rnnoise_create(nullptr);
        fft.init(NFFT);
        win.assign(NFFT, 0.f);
        for (int i = 0; i < NFFT; ++i) win[i] = (float)std::sqrt(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / NFFT));
        configure(set);
        reset();
    }

    void configure(const MicSettings& s) {
        set = s;
        set.aiLevel = clampi(set.aiLevel, 0, 100);
        set.bg = clampi(set.bg, 0, 100);
        set.impact = clampi(set.impact, 0, 100);
        set.compLevel = clampi(set.compLevel, 0, 100);
        set.gateDb10 = clampi(set.gateDb10, -800, 0);
        // AI: Min still takes 10 dB off the noise, Max all the AI can.
        const double t = set.aiLevel / 100.0;
        aiResidual = set.aiLevel >= 100 ? 0.f : (float)std::pow(10.0, -(10.0 + 40.0 * t * t) / 20.0);
        bgFloor = (float)std::pow(10.0, -(6.0 + 24.0 * set.bg / 100.0) / 20.0);
        const float above = (float)(14.0 - 8.0 * set.impact / 100.0);                  // dB above its surroundings
        impactRatio = (float)std::pow(10.0, above / 10.0);
        impactFloor = (float)std::pow(10.0, -(6.0 + 18.0 * set.impact / 100.0) / 20.0);  // the most it takes off
        const double c = set.compLevel / 100.0;
        compThr = (float)(-10.0 - 26.0 * c);
        compRatio = (float)(1.5 + 4.5 * c);
        compMakeup = (float)(-compThr * (1.0 - 1.0 / compRatio) * 0.5);
        // With Sonar match on, the Compressor drives the match's leveller instead, as measured: Level 0.50 is the 2.4:1
        // Sonar gave, 1.00 is 3.8:1, off is none.
        ToneMatch::Params tp;
        tp.ratio = 1.f + 1.4f * (set.compOn ? set.compLevel / 50.f : 0.f);
        tone.configure(tp, fs);
    }

    void reset() {
        std::fill(in, in + FRAME, 0.f); std::fill(out, out + FRAME, 0.f); std::fill(prev1, prev1 + FRAME, 0.f); std::fill(prev2, prev2 + FRAME, 0.f);
        inPos = 0;
        std::fill(nrIn, nrIn + NFFT, 0.f); std::fill(nrOla, nrOla + NFFT, 0.f);
        std::fill(nrRing, nrRing + RING, 0.f); std::fill(gainRing, gainRing + RING, 1.f);
        count = 0; nrFrames = 0;
        for (int k = 0; k <= NFFT / 2; ++k) { noise[k] = 1e-12f; psmooth[k] = 0.f; gprev[k] = 1.f; }
        if (den) rnnoise_init(den, nullptr);
        wAi = set.aiOn ? 1.f : 0.f;
        wNr = !set.aiOn && (set.bgOn || set.impactOn) ? 1.f : 0.f;
        envFast = envSlow = envMid = fullMid = fullSlow = 1e-10f; impState = 0; holdoff = 0; hintLeft = 0; hpX = hpY = 0.f;
        gateEnv = 1e-10f; gateGain = set.gateOn ? 0.f : 1.f; gateHold = 0; floorDb = -60.f;
        compEnv = 0.f;
        tone.reset();
        wMatch = set.matchOn ? 1.f : 0.f;
        inSm = outSm = -120.f; cutSm = 0.f;
    }

    // The mic level changed by db: move the learned noise with it.
    void shiftLevel(double db) {
        const float p = (float)std::pow(10.0, db / 10.0);
        for (int k = 0; k <= NFFT / 2; ++k) noise[k] *= p;
        floorDb += (float)db;
    }
    // A key or mouse button was pressed: its click is on its way.
    void inputHint() { hintLeft = (int)(0.12 * fs); }

    void process(float* x, int n) {
        for (int i = 0; i < n; ++i) {
            in[inPos] = x[i];
            x[i] = out[inPos];
            if (++inPos == FRAME) { frame(); inPos = 0; }
        }
    }

    static int latencySamples() { return 3 * FRAME; }

private:
    typedef std::complex<float> cf;
    DenoiseState* den = nullptr;
    conv::FFT fft;
    std::vector<float> win;
    float in[FRAME] = {}, out[FRAME] = {}, prev1[FRAME] = {}, prev2[FRAME] = {};
    float nrIn[NFFT] = {}, nrOla[NFFT] = {}, nrRing[RING] = {}, gainRing[RING] = {};
    float noise[NFFT / 2 + 1] = {}, psmooth[NFFT / 2 + 1] = {}, gprev[NFFT / 2 + 1] = {};
    cf buf[NFFT];
    long long count = 0;
    int inPos = 0, nrFrames = 0, hintLeft = 0, gateHold = 0;
    float aiResidual = 0.f, bgFloor = 0.1f, impactRatio = 10.f, impactFloor = 0.1f;
    float compThr = -20.f, compRatio = 3.f, compMakeup = 0.f;
    ToneMatch tone;
    float wMatch = 0.f;
    float wAi = 1.f, wNr = 0.f, envFast = 1e-10f, envSlow = 1e-10f, hpX = 0.f, hpY = 0.f;
    long long evStart = 0;
    float evPeak = 0.f, evBase = 0.f, envMid = 1e-10f, voiceNear = 0.f, fullMid = 1e-10f, fullSlow = 1e-10f, fullBase = 0.f;
    int impState = 0, holdoff = 0;
    bool evHinted = false;
    float gateEnv = 1e-10f, gateGain = 1.f, floorDb = -60.f, compEnv = 0.f;
    float inSm = -120.f, outSm = -120.f, cutSm = 0.f;

    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float db(float p) { return 10.f * std::log10(p + 1e-20f); }
    static float toward(float v, float target, float step) {
        return std::fabs(target - v) <= step ? target : v + (target > v ? step : -step);
    }

    // One 10 ms frame. RNNoise hands back the frame from two frames ago cleaned, so the dry signal for its residual,
    // and the dry path itself, is that frame too; the classic reduction runs two frames late to match.
    void frame() {
        float scaled[FRAME], d[FRAME];
        for (int i = 0; i < FRAME; ++i) scaled[i] = in[i] * 32768.f;
        const float vad = den ? rnnoise_process_frame(den, d, scaled) : 0.f;

        const bool ai = set.aiOn, nr = !set.aiOn && (set.bgOn || set.impactOn);
        const float step = 1.f / (0.02f * (float)fs);
        float pIn = 0.f, pAi = 0.f, pOut = 0.f, peak = 0.f;
        for (int i = 0; i < FRAME; ++i) {
            const float dry = prev2[i];
            const float dn = d[i] / 32768.f;
            const float aiOut = dn + aiResidual * (dry - dn);
            const float nrOut = nrSample(in[i]);
            pIn += dry * dry;
            pAi += aiOut * aiOut;
            wAi = toward(wAi, ai ? 1.f : 0.f, step);
            wNr = toward(wNr, nr ? 1.f : 0.f, step);
            const float wDry = std::max(0.f, 1.f - wAi - wNr);
            float y = wAi * aiOut + wNr * nrOut + wDry * dry;
            y = gate(y);
            y = compress(y);
            // Sonar match: always running, so switching it crossfades.
            wMatch = toward(wMatch, set.matchOn ? 1.f : 0.f, step);
            y = wMatch * tone.run(y) + (1.f - wMatch) * y;
            if (y > 1.f) y = 1.f; else if (y < -1.f) y = -1.f;
            out[i] = y;
            pOut += y * y;
            peak = std::max(peak, std::fabs(y));
        }
        std::memcpy(prev2, prev1, sizeof prev2);
        std::memcpy(prev1, in, sizeof prev1);
        // The voice detector's view of the last few frames, for Impact (it must not take a "t" out of a word).
        voiceNear = std::max(vad, voiceNear * 0.6f);

        const float inDb = db(pIn / FRAME), outDb = db(pOut / FRAME);
        inSm += 0.3f * (inDb - inSm);
        outSm += 0.3f * (outDb - outSm);
        cutSm += 0.1f * ((ai ? std::max(0.f, inDb - db(pAi / FRAME)) : 0.f) - cutSm);
        meterIn.store(inSm, std::memory_order_relaxed);
        meterOut.store(outSm, std::memory_order_relaxed);
        meterAiCut.store(cutSm, std::memory_order_relaxed);
        meterVoice.store(vad, std::memory_order_relaxed);
        meterGateDb.store(set.gateAuto ? autoThreshold() : set.gateDb10 / 10.f, std::memory_order_relaxed);
        meterGateOpen.store(!set.gateOn || gateGain > 0.5f, std::memory_order_relaxed);
        meterCompGr.store(!set.compOn ? 0.f : set.matchOn ? tone.lastGr : lastGr, std::memory_order_relaxed);
        const int w = waveAt.load(std::memory_order_relaxed);
        wave[w].store(peak, std::memory_order_relaxed);
        waveAt.store((w + 1) % WAVE, std::memory_order_relaxed);
    }

    // Classic noise reduction: one sample in, the sample from two frames ago out. Background is a Wiener filter over a
    // noise floor that follows the quietest the room has been; Impact ducks what jumps far above the last 50 ms.
    float nrSample(float x) {
        nrIn[count % NFFT] = x;
        // Impact is judged on the input as it arrives, and its gain used two frames later (then set back to 1).
        impactTrack(x);
        ++count;
        if (count >= NFFT && count % HOP == 0) nrHop();
        const long long t = count - 1 - 2 * FRAME;
        if (t < 0) return 0.f;
        const float g = gainRing[t & MASK];
        gainRing[t & MASK] = 1.f;
        return nrRing[t & MASK] * (set.impactOn ? g : 1.f);
    }
    void nrHop() {
        const long long start = count - NFFT;
        for (int i = 0; i < NFFT; ++i) buf[i] = cf(nrIn[(start + i) % NFFT] * win[i], 0.f);
        fft.forward(buf);
        ++nrFrames;
        const float up = 1.0007f;  // the floor creeps up about 2.5 dB a second, and drops at once
        for (int k = 0; k <= NFFT / 2; ++k) {
            const float p = std::norm(buf[k]);
            psmooth[k] = 0.7f * psmooth[k] + 0.3f * p;
            if (nrFrames <= 40) noise[k] = nrFrames == 1 ? psmooth[k] : std::min(noise[k] * 1.01f, psmooth[k]);
            else noise[k] = psmooth[k] < noise[k] ? psmooth[k] : noise[k] * up;
            float g = 1.f;
            if (set.bgOn) {
                const float gamma = p / (noise[k] * 1.6f + 1e-20f);
                const float xi = 0.98f * gprev[k] * gprev[k] * std::max(gamma, 1.f) + 0.02f * std::max(gamma - 1.f, 0.f);
                g = std::max(xi / (1.f + xi), bgFloor);
                g = 0.6f * g + 0.4f * gprev[k];
            }
            gprev[k] = g;
            buf[k] *= g;
            if (k > 0 && k < NFFT / 2) buf[NFFT - k] = std::conj(buf[k]);
        }
        fft.inverse(buf);
        for (int i = 0; i < NFFT; ++i) nrOla[i] += buf[i].real() * win[i];
        for (int i = 0; i < HOP; ++i) nrRing[(start + i) & MASK] = nrOla[i];
        std::memmove(nrOla, nrOla + HOP, (NFFT - HOP) * sizeof(float));
        std::fill(nrOla + (NFFT - HOP), nrOla + NFFT, 0.f);
    }
    // Impact: a sharp jump above the last 50 ms (mostly above 2 kHz) is a click if, 20 ms later, it has already
    // fallen 10 dB from its peak; a word starting does not fall like that. The voice is played two frames late, so
    // the click can be ducked from just before it, after the fact, and then until its ring and thock have died back
    // to within 6 dB of where the room was (at most 150 ms).
    enum { IMP_IDLE, IMP_CHECK, IMP_DUCK };
    void impactTrack(float x) {
        const float hp = 0.86f * (hpY + x - hpX);   // mostly above 2 kHz, where clicks live and vowels do not
        hpX = x; hpY = hp;
        const float e = hp * hp;
        envFast += 0.04f * (e - envFast);                       // about 0.5 ms: steadier than single samples of noise
        envMid += (e > envMid ? 0.04f : 0.007f) * (e - envMid);  // about 3 ms: the click with its ring
        fullMid += (x * x > fullMid ? 0.04f : 0.007f) * (x * x - fullMid);  // the whole sound, for when the thock is gone
        float ratio = impactRatio;
        if (hintLeft > 0) { --hintLeft; ratio *= 0.7f; }   // a key was pressed: be readier
        ratio = std::max(ratio, 10.f);                       // never less than a 10 dB jump
        if (holdoff > 0) --holdoff;
        const long long ramp = 48;
        switch (impState) {
        case IMP_IDLE:
            if (holdoff == 0 && envFast > ratio * envSlow && envFast > 1e-7f) {
                impState = IMP_CHECK; evStart = count; evPeak = envMid; evBase = envSlow; fullBase = fullSlow; evHinted = hintLeft > 0; ++statSeen;
            }
            break;
        case IMP_CHECK:
            // A far bigger jump while checking is the real event: start the check again from it.
            if (count - evStart > (long long)(0.005 * fs) && envFast > ratio * evPeak) { evStart = count; evPeak = envMid; evBase = std::min(evBase, envSlow); evHinted = evHinted || hintLeft > 0; break; }
            evPeak = std::max(evPeak, envMid);
            if (count - evStart < (long long)(0.02 * fs)) break;
            if (envMid > 0.1f * evPeak) { ++statLong; impState = IMP_IDLE; holdoff = (int)(0.005 * fs); break; }  // goes on: a word
            // A key or mouse button Windows saw pressed is always taken out. Anything else only when the voice
            // detector hears nobody talking: inside speech a short burst is more likely a "t" or a "k".
            if (!evHinted && voiceNear > 0.5f) { ++statSpeech; impState = IMP_IDLE; holdoff = (int)(0.04 * fs); break; }
            ++statDucked;
            if (voiceNear > 0.5f) {
                // A key pressed while talking: the voice goes on under it, so only the click's first 8 ms go down,
                // and by 6 dB at most.
                const float g = std::max(impactFloor, 0.5f);
                const long long a = evStart - AHEAD, b = evStart + (long long)(0.008 * fs);
                for (long long j = std::max(a - ramp, count - 2 * FRAME); j <= b + ramp; ++j) {
                    const float k = j < a ? (float)(a - j) / ramp : (j > b ? (float)(j - b) / ramp : 0.f);
                    gainRing[j & MASK] = std::min(gainRing[j & MASK], g + (1.f - g) * std::min(k, 1.f));
                }
                impState = IMP_IDLE; holdoff = (int)(0.01 * fs);
                envSlow = evBase; fullSlow = fullBase;
                break;
            }
            for (long long j = std::max(evStart - AHEAD - ramp, count - 2 * FRAME); j <= count; ++j) {
                const float k = j < evStart - AHEAD ? (float)(evStart - AHEAD - j) / ramp : 0.f;
                gainRing[j & MASK] = std::min(gainRing[j & MASK], impactFloor + (1.f - impactFloor) * k);
            }
            impState = IMP_DUCK;
            break;
        case IMP_DUCK:
            if (fullMid < 4.f * fullBase || count - evStart > (long long)(0.15 * fs)) {
                for (long long j = 0; j < ramp; ++j)   // let go over 1 ms
                    gainRing[(count + j) & MASK] = std::min(gainRing[(count + j) & MASK], impactFloor + (1.f - impactFloor) * (float)j / ramp);
                impState = IMP_IDLE; holdoff = (int)(0.01 * fs);
                envSlow = evBase; fullSlow = fullBase;
            } else {
                gainRing[count & MASK] = std::min(gainRing[count & MASK], impactFloor);
            }
            break;
        }
    }

    float autoThreshold() const { return std::min(-20.f, std::max(-70.f, floorDb + 10.f)); }
    // Noise gate, timed like Sonar's (measured): the level is a 2 ms power average, so a lone noise peak does not open
    // it; it stays 80 ms after the last moment above the threshold, then closes over 50 ms to 60 dB down.
    float gate(float y) {
        const float e = y * y;
        gateEnv += (e > gateEnv ? 1.f / (0.002f * (float)fs) : 1.f / (0.02f * (float)fs)) * (e - gateEnv);
        const float lv = db(gateEnv);
        floorDb = lv < floorDb ? lv : floorDb + 3.f / (float)fs;   // the room's floor: the quietest, rising slowly
        if (!set.gateOn) { gateGain = 1.f; return y; }
        const float thr = set.gateAuto ? autoThreshold() : set.gateDb10 / 10.f;
        if (lv > thr) gateHold = (int)(0.08 * fs);
        else if (gateHold > 0) --gateHold;
        const float want = gateHold > 0 ? 1.f : 0.001f;
        gateGain += (want > gateGain ? 0.05f : 1.f / (0.05f * (float)fs)) * (want - gateGain);
        return y * gateGain;
    }
    // Compressor: soft knee, 5 ms attack, 150 ms release, half of what it takes given back as makeup.
    float lastGr = 0.f;
    float compress(float y) {
        if (!set.compOn || set.matchOn) return y;
        const float e = y * y;
        compEnv += (e > compEnv ? 1.f / (0.005f * (float)fs) : 1.f / (0.15f * (float)fs)) * (e - compEnv);
        const float over = db(compEnv) - compThr, knee = 6.f;
        float gr = 0.f;
        if (over > knee / 2.f) gr = over * (1.f - 1.f / compRatio);
        else if (over > -knee / 2.f) { const float k = over + knee / 2.f; gr = (1.f - 1.f / compRatio) * k * k / (2.f * knee); }
        lastGr = gr;
        return y * std::pow(10.f, (compMakeup - gr) / 20.f);
    }
};

}
