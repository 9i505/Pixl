#pragma once
// Test sound for the mic chain: WAV in and out, made-up noises (fans, air conditioning, hum, coil whine, keys, mouse, bangs,
// other talkers), whispering made from normal speech, room echo, and a top octave for the TTS voices (they stop near
// 8 kHz, real voices do not). Every generator is seeded, so each run hears exactly the same sound.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "../../src/convolution/fft.hpp"

namespace kit {
using Sig = std::vector<float>;
const double PI = 3.14159265358979323846;
const int FS = 48000;

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { for (int i = 0; i < 4; ++i) next(); }
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double uni() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
    double range(double a, double b) { return a + (b - a) * uni(); }
    double gauss() { const double u1 = uni() + 1e-12, u2 = uni(); return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * PI * u2); }
};

// ---------- WAV ----------
inline bool readWav(const std::wstring& path, Sig& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    std::vector<uint8_t> b;
    fseek(f, 0, SEEK_END); const long len = ftell(f); fseek(f, 0, SEEK_SET);
    b.resize(len); fread(b.data(), 1, len, f); fclose(f);
    if (len < 44 || memcmp(b.data(), "RIFF", 4) || memcmp(b.data() + 8, "WAVE", 4)) return false;
    int fmt = 1, ch = 1, bits = 16;
    size_t p = 12;
    while (p + 8 <= b.size()) {
        const uint32_t sz = *(const uint32_t*)(b.data() + p + 4);
        if (!memcmp(b.data() + p, "fmt ", 4)) {
            fmt = *(const uint16_t*)(b.data() + p + 8); ch = *(const uint16_t*)(b.data() + p + 10);
            bits = *(const uint16_t*)(b.data() + p + 22);
            if (fmt == 0xFFFE) fmt = *(const uint16_t*)(b.data() + p + 32);
        } else if (!memcmp(b.data() + p, "data", 4)) {
            const size_t n = std::min<size_t>(sz, b.size() - p - 8) / (bits / 8) / ch;
            out.assign(n, 0.f);
            const uint8_t* d = b.data() + p + 8;
            for (size_t i = 0; i < n; ++i) {
                double acc = 0.0;
                for (int c = 0; c < ch; ++c) {
                    const size_t at = (i * ch + c) * (bits / 8);
                    if (fmt == 3 && bits == 32) acc += *(const float*)(d + at);
                    else if (bits == 16) acc += *(const int16_t*)(d + at) / 32768.0;
                    else if (bits == 24) acc += (double)((int32_t)((d[at] << 8) | (d[at + 1] << 16) | (d[at + 2] << 24)) >> 8) / 8388608.0;
                    else if (bits == 32) acc += *(const int32_t*)(d + at) / 2147483648.0;
                }
                out[i] = (float)(acc / ch);
            }
            return true;
        }
        p += 8 + sz + (sz & 1);
    }
    return false;
}

inline bool writeWav(const std::wstring& path, const Sig& x) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    const uint32_t n = (uint32_t)x.size(), bytes = n * 2;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(FS); u32(FS * 2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(bytes);
    std::vector<int16_t> pcm(n);
    for (uint32_t i = 0; i < n; ++i) pcm[i] = (int16_t)std::lround(std::max(-1.0f, std::min(0.99997f, x[i])) * 32767.0);
    fwrite(pcm.data(), 2, n, f);
    fclose(f);
    return true;
}

// ---------- small tools ----------
inline double dbOf(double p) { return 10.0 * std::log10(p + 1e-30); }
inline double power(const Sig& x, size_t a = 0, size_t b = (size_t)-1) {
    b = std::min(b, x.size());
    if (b <= a) return 0.0;
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += (double)x[i] * x[i];
    return e / (b - a);
}
inline void scale(Sig& x, double g) { for (auto& v : x) v = (float)(v * g); }
inline void addAt(Sig& dst, const Sig& src, long at, double g = 1.0) {
    for (size_t i = 0; i < src.size(); ++i) {
        const long j = at + (long)i;
        if (j >= 0 && j < (long)dst.size()) dst[j] += (float)(src[i] * g);
    }
}
inline Sig delayed(const Sig& x, int d) {
    Sig y(x.size(), 0.f);
    for (size_t i = (size_t)d; i < x.size(); ++i) y[i] = x[i - d];
    return y;
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float run(float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float)y; }
    void set(double B0, double B1, double B2, double A0, double A1, double A2) { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
    static Biquad lowpass(double hz, double q = 0.7071) {
        Biquad f; const double w = 2 * PI * hz / FS, c = std::cos(w), al = std::sin(w) / (2 * q);
        f.set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); return f;
    }
    static Biquad highpass(double hz, double q = 0.7071) {
        Biquad f; const double w = 2 * PI * hz / FS, c = std::cos(w), al = std::sin(w) / (2 * q);
        f.set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); return f;
    }
    static Biquad bandpass(double hz, double q) {
        Biquad f; const double w = 2 * PI * hz / FS, c = std::cos(w), al = std::sin(w) / (2 * q);
        f.set(al, 0, -al, 1 + al, -2 * c, 1 - al); return f;
    }
    static Biquad peak(double hz, double q, double db) {
        Biquad f; const double A = std::pow(10.0, db / 40), w = 2 * PI * hz / FS, c = std::cos(w), al = std::sin(w) / (2 * q);
        f.set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); return f;
    }
    static Biquad highShelf(double hz, double db) {
        Biquad f; const double A = std::pow(10.0, db / 40), w = 2 * PI * hz / FS, c = std::cos(w), s = std::sin(w);
        const double al = s / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
        f.set(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
              (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
        return f;
    }
};
inline void filt(Sig& x, Biquad f) { for (auto& v : x) v = f.run(v); }
inline Sig filtered(const Sig& x, Biquad f) { Sig y = x; filt(y, f); return y; }

// Linear convolution through one big FFT.
inline Sig convolve(const Sig& x, const Sig& h) {
    const size_t need = x.size() + h.size() - 1;
    int n = 1;
    while ((size_t)n < need) n <<= 1;
    conv::FFT fft; fft.init(n);
    std::vector<conv::cf> a(n), b(n);
    for (size_t i = 0; i < x.size(); ++i) a[i] = conv::cf(x[i], 0.f);
    for (size_t i = 0; i < h.size(); ++i) b[i] = conv::cf(h[i], 0.f);
    fft.forward(a.data()); fft.forward(b.data());
    for (int i = 0; i < n; ++i) a[i] *= b[i];
    fft.inverse(a.data());
    Sig y(x.size());
    for (size_t i = 0; i < y.size(); ++i) y[i] = a[i].real();
    return y;
}

// Windowed-sinc resampling (used for STOI at 10 kHz).
inline Sig resample(const Sig& x, double fromHz, double toHz) {
    const double ratio = fromHz / toHz, cutoff = 0.45 * std::min(1.0, toHz / fromHz) * 2.0;
    const int half = (int)std::ceil(24 * ratio);
    const size_t n = (size_t)(x.size() / ratio);
    Sig y(n, 0.f);
    for (size_t i = 0; i < n; ++i) {
        const double t = i * ratio;
        const long c = (long)std::floor(t);
        double acc = 0.0;
        for (long k = c - half; k <= c + half; ++k) {
            if (k < 0 || k >= (long)x.size()) continue;
            const double d = t - k, arg = PI * cutoff * d;
            const double s = std::fabs(arg) < 1e-9 ? 1.0 : std::sin(arg) / arg;
            const double wv = 0.42 + 0.5 * std::cos(PI * d / (half + 1)) + 0.08 * std::cos(2 * PI * d / (half + 1));
            acc += x[k] * cutoff * s * wv;
        }
        y[i] = (float)acc;
    }
    return y;
}

// ---------- room ----------
// A room's impulse response: the direct sound, then an exponentially decaying, slightly duller tail.
inline Sig room(Rng& r, double rt60, double drrDb, double lenSec = 0.9, double lpHz = 6000) {
    Sig h((size_t)(lenSec * FS), 0.f);
    h[0] = 1.f;
    Biquad lp = Biquad::lowpass(lpHz);
    double tail = 0.0;
    const int start = (int)(0.004 * FS);
    for (size_t i = start; i < h.size(); ++i) {
        const double t = (double)i / FS;
        h[i] = lp.run((float)(r.gauss() * std::exp(-6.9 * t / rt60)));
        tail += (double)h[i] * h[i];
    }
    const double g = std::sqrt(std::pow(10.0, -drrDb / 10.0) / std::max(tail, 1e-12));
    for (size_t i = start; i < h.size(); ++i) h[i] = (float)(h[i] * g);
    return h;
}

// ---------- steady noises ----------
// Pink-ish airflow: Kellet's pink filter, then a band from lo to hi, plus a slow swell.
inline Sig airflow(int n, Rng& r, double lo, double hi, double swell, double bladeHz, double bladeDb) {
    Sig x(n);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (int i = 0; i < n; ++i) {
        const double w = r.gauss();
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        x[i] = (float)(b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362);
        b6 = w * 0.115926;
    }
    filt(x, Biquad::highpass(lo)); filt(x, Biquad::highpass(lo));
    filt(x, Biquad::lowpass(hi, 0.6)); filt(x, Biquad::lowpass(hi * 1.6, 0.6));
    const double ph = r.range(0, 2 * PI), rate = r.range(0.15, 0.4);
    for (int i = 0; i < n; ++i) x[i] = (float)(x[i] * (1.0 + swell * std::sin(2 * PI * rate * i / FS + ph)));
    scale(x, 1.0 / std::sqrt(power(x)));
    if (bladeHz > 0) {
        const double a = std::pow(10.0, bladeDb / 20.0);
        double f = bladeHz;
        for (int h = 1; h <= 4; ++h) {
            const double p0 = r.range(0, 2 * PI);
            for (int i = 0; i < n; ++i) x[i] += (float)(a / h * std::sqrt(2.0) * std::sin(2 * PI * f * h * i / FS + p0 + 0.3 * std::sin(2 * PI * 0.2 * i / FS)));
        }
    }
    scale(x, 1.0 / std::sqrt(power(x)));
    return x;
}
inline Sig fan(int n, Rng& r) { return airflow(n, r, 50, 1400, 0.08, 118, -14); }
inline Sig airCon(int n, Rng& r) {
    Sig x = airflow(n, r, 90, 4200, 0.04, 100, -18);
    filt(x, Biquad::peak(650, 0.6, 5));
    scale(x, 1.0 / std::sqrt(power(x)));
    return x;
}
// Mains hum with buzz: 50 Hz harmonics falling slowly, the shape a bad ground or a dimmer gives.
inline Sig hum(int n, Rng& r) {
    Sig x(n, 0.f);
    for (int h = 1; h <= 60; ++h) {
        const double a = (h % 2 ? 1.0 : 0.5) / std::pow((double)h, 0.8), ph = r.range(0, 2 * PI);
        for (int i = 0; i < n; ++i) x[i] += (float)(a * std::sin(2 * PI * 50.0 * h * i / FS + ph));
    }
    scale(x, 1.0 / std::sqrt(power(x)));
    return x;
}
// Coil whine: thin tones near 9 and 12 kHz that come and go with the load.
inline Sig coilWhine(int n, Rng& r) {
    Sig x(n, 0.f);
    const double f1 = r.range(8800, 9600), f2 = r.range(11500, 12300);
    double ph1 = 0, ph2 = 0, gate = 0, target = 1;
    int next = 0;
    for (int i = 0; i < n; ++i) {
        if (i >= next) { target = r.uni() < 0.7 ? 1.0 : 0.15; next = i + (int)(r.range(0.2, 1.0) * FS); }
        gate += 0.002 * (target - gate);
        const double drift = 40 * std::sin(2 * PI * 0.7 * i / FS);
        ph1 += 2 * PI * (f1 + drift) / FS; ph2 += 2 * PI * (f2 - drift) / FS;
        x[i] = (float)(gate * (std::sin(ph1) + 0.5 * std::sin(ph2)));
    }
    scale(x, 1.0 / std::sqrt(power(x)));
    return x;
}

// ---------- transients ----------
struct Events {
    Sig s;
    std::vector<int> presses;               // when a key or a button went down or up (for Clarity's input hint)
    std::vector<std::pair<int, int>> spans; // where each sound is (for measuring)
};
inline void decaying(Sig& x, int at, double amp, double hz, double tauMs, double ph) {
    const int len = (int)(tauMs * 0.001 * FS * 7);
    for (int i = 0; i < len && at + i < (int)x.size(); ++i)
        if (at + i >= 0) x[at + i] += (float)(amp * std::exp(-i / (tauMs * 0.001 * FS)) * std::sin(2 * PI * hz * i / FS + ph));
}
inline void burst(Sig& x, int at, double amp, double tauMs, Rng& r) {
    const int len = (int)(tauMs * 0.001 * FS * 7);
    for (int i = 0; i < len && at + i < (int)x.size(); ++i)
        if (at + i >= 0) x[at + i] += (float)(amp * std::exp(-i / (tauMs * 0.001 * FS)) * r.gauss());
}
// One mechanical key: a sharp broadband tick, two ringing modes and a low thock.
inline void keyClick(Sig& x, int at, double amp, Rng& r, bool up) {
    const double k = up ? 0.45 : 1.0;
    burst(x, at, amp * k, r.range(0.25, 0.6), r);
    decaying(x, at, amp * k * r.range(0.3, 0.6), r.range(1800, 3600), r.range(1.5, 4.0), r.range(0, 2 * PI));
    decaying(x, at, amp * k * r.range(0.2, 0.45), r.range(4200, 7500), r.range(0.8, 2.5), r.range(0, 2 * PI));
    decaying(x, at + (int)(0.0015 * FS), amp * k * r.range(0.3, 0.7), r.range(160, 420), r.range(5.0, 14.0), r.range(0, 2 * PI));
}
// Typing: words of 3 to 8 keys, spaces between, now and then a pause; each key goes down and comes back up.
inline Events typing(int n, Rng& r, double fromSec, double toSec, double amp) {
    Events e; e.s.assign(n, 0.f);
    double t = fromSec;
    while (t < toSec) {
        const int keys = 3 + (int)(r.uni() * 6);
        for (int j = 0; j < keys && t < toSec; ++j) {
            const int at = (int)(t * FS), up = at + (int)(r.range(0.07, 0.13) * FS);
            const double a = amp * std::pow(10.0, r.range(-4, 2) / 20.0);
            keyClick(e.s, at, a, r, false);
            keyClick(e.s, up, a, r, true);
            e.presses.push_back(at); e.presses.push_back(up);
            e.spans.push_back({at, at + (int)(0.03 * FS)}); e.spans.push_back({up, up + (int)(0.03 * FS)});
            t += r.range(0.09, 0.2);
        }
        t += r.uni() < 0.2 ? r.range(0.5, 1.2) : r.range(0.12, 0.3);
    }
    return e;
}
// Mouse buttons: very short and bright.
inline Events mouse(int n, Rng& r, double fromSec, double toSec, double amp) {
    Events e; e.s.assign(n, 0.f);
    double t = fromSec;
    while (t < toSec) {
        const int at = (int)(t * FS), up = at + (int)(r.range(0.08, 0.15) * FS);
        for (int j = 0; j < 2; ++j) {
            const int p = j ? up : at;
            const double a = amp * (j ? 0.6 : 1.0);
            burst(e.s, p, a, 0.25, r);
            decaying(e.s, p, a * 0.5, r.range(5000, 8000), 1.2, r.range(0, 2 * PI));
            decaying(e.s, p, a * 0.3, r.range(2200, 3200), 0.8, r.range(0, 2 * PI));
            e.presses.push_back(p);
            e.spans.push_back({p, p + (int)(0.02 * FS)});
        }
        t += r.range(0.7, 2.0);
    }
    return e;
}
// A door slamming: low thump, a crack, then the room.
inline Sig doorSlam(Rng& r) {
    Sig x((size_t)(0.5 * FS), 0.f);
    decaying(x, 0, 1.0, 72, 70, 0);
    Sig nz((size_t)(0.3 * FS), 0.f);
    burst(nz, 0, 0.8, 35, r);
    filt(nz, Biquad::lowpass(500));
    addAt(x, nz, 0);
    burst(x, 0, 0.9, 3.0, r);
    Sig y = convolve(x, room(r, 0.6, 2.0));
    scale(y, 1.0 / (*std::max_element(y.begin(), y.end(), [](float a, float b) { return std::fabs(a) < std::fabs(b); }) + 1e-9f));
    for (auto& v : y) v = std::fabs(v) > 1.f ? (v > 0 ? 1.f : -1.f) : v;
    return y;
}
inline Sig clap(Rng& r) {
    Sig x((size_t)(0.1 * FS), 0.f);
    burst(x, 0, 1.0, 4.5, r);
    filt(x, Biquad::bandpass(2200, 0.7));
    Sig y = convolve(x, room(r, 0.5, 4.0));
    float pk = 0; for (float v : y) pk = std::max(pk, std::fabs(v));
    scale(y, 1.0 / pk);
    return y;
}
// A spoon dropped on a table: a few bounces, each a click with ringing metal partials.
inline Sig spoonDrop(Rng& r) {
    Sig x((size_t)(0.9 * FS), 0.f);
    const double when[4] = {0, 0.21, 0.33, 0.40}, amp[4] = {1, 0.5, 0.3, 0.15};
    const double part[4] = {2130, 3710, 5940, 8320}, tau[4] = {250, 180, 120, 80}, pa[4] = {0.3, 0.25, 0.2, 0.1};
    for (int b = 0; b < 4; ++b) {
        const int at = (int)(when[b] * FS);
        burst(x, at, amp[b], 0.8, r);
        for (int p = 0; p < 4; ++p) decaying(x, at, amp[b] * pa[p], part[p] * r.range(0.995, 1.005), tau[p] * amp[b], r.range(0, 2 * PI));
    }
    float pk = 0; for (float v : x) pk = std::max(pk, std::fabs(v));
    scale(x, 1.0 / pk);
    return x;
}

// ---------- voices ----------
// The TTS voices stop near 8 kHz. Real voices keep going: s and f reach well past 10 kHz, vowels fall off steeply.
// This adds the missing top from the 4 to 7.5 kHz band of each moment, flat-ish for hiss sounds, steep for vowels.
inline Sig extendTop(const Sig& x, Rng& r) {
    const int N = 1024, HOPN = 256, HH = N / 2 + 1;
    conv::FFT fft; fft.init(N);
    std::vector<float> win(N);
    for (int i = 0; i < N; ++i) win[i] = (float)std::sqrt(0.5 - 0.5 * std::cos(2 * PI * i / N));
    Sig y(x.size() + N, 0.f);
    std::vector<conv::cf> sp(N);
    const double bin = (double)FS / N;
    const int k4 = (int)(2000 / bin), k8 = (int)(4000 / bin), kTop = (int)(7500 / bin), kFrom = (int)(7000 / bin), kEnd = (int)(16500 / bin);
    for (size_t st = 0; st + N <= x.size(); st += HOPN) {
        for (int i = 0; i < N; ++i) sp[i] = conv::cf(x[st + i] * win[i], 0.f);
        fft.forward(sp.data());
        double mid = 0, hi = 0;
        for (int k = k4; k < k8; ++k) mid += std::norm(sp[k]);
        for (int k = k8; k < kTop; ++k) hi += std::norm(sp[k]);
        mid /= (k8 - k4); hi /= (kTop - k8);
        const double hissy = hi / (hi + mid + 1e-30);
        const double slope = -20.0 + 17.0 * std::min(1.0, hissy / 0.6);  // dB per octave above 7.5 kHz
        for (int k = kFrom; k < HH; ++k) {
            const double f = k * bin;
            double target = 0.0;
            if (k < kEnd) {
                const double oct = std::log2(f / 7500.0);
                const double db = slope * std::max(0.0, oct) - (f > 12000 ? 12.0 * std::log2(f / 12000.0) : 0.0);
                target = std::sqrt(hi * std::pow(10.0, db / 10.0));
            }
            const double mix = k < kTop ? (double)(k - kFrom) / (kTop - kFrom) : 1.0;
            const double ph = r.range(0, 2 * PI);
            sp[k] = (float)(1 - mix) * sp[k] + (float)mix * std::polar((float)target, (float)ph);
            if (k > 0 && k < N / 2) sp[N - k] = std::conj(sp[k]);
        }
        fft.inverse(sp.data());
        for (int i = 0; i < N; ++i) y[st + i] += sp[i].real() * win[i] * 0.5f;
    }
    y.resize(x.size());
    return y;
}

// Whispering from normal speech: each 20 ms keeps its LPC spectral envelope but the voice source becomes noise, and
// the low end goes, as in a real whisper.
inline Sig whisperize(const Sig& x, Rng& r) {
    const int N = 960, HOPN = 480, P = 40, PRE = 240;
    std::vector<double> win(N);
    for (int i = 0; i < N; ++i) win[i] = 0.5 - 0.5 * std::cos(2 * PI * i / N);
    Sig y(x.size() + N, 0.f);
    std::vector<double> fr(N), ac(P + 1), a(P + 1), tmp(P + 1), out(N + PRE);
    for (size_t st = 0; st + N <= x.size(); st += HOPN) {
        double e0 = 0;
        for (int i = 0; i < N; ++i) { fr[i] = x[st + i] * win[i]; e0 += fr[i] * fr[i]; }
        if (e0 < 1e-12) continue;
        for (int l = 0; l <= P; ++l) {
            double s = 0; for (int i = l; i < N; ++i) s += fr[i] * fr[i - l];
            ac[l] = s * std::exp(-0.5 * std::pow(2 * PI * 60.0 * l / FS, 2));
        }
        ac[0] *= 1.0001;
        std::fill(a.begin(), a.end(), 0.0); a[0] = 1;
        double err = ac[0];
        for (int i = 1; i <= P; ++i) {
            double acc = ac[i];
            for (int j = 1; j < i; ++j) acc += a[j] * ac[i - j];
            const double k = -acc / err;
            tmp = a;
            for (int j = 1; j < i; ++j) a[j] = tmp[j] + k * tmp[i - j];
            a[i] = k;
            err *= (1 - k * k);
            if (err <= 0) break;
        }
        for (int i = 0; i < N + PRE; ++i) {
            double v = r.gauss();
            for (int j = 1; j <= P; ++j) if (i - j >= 0) v -= a[j] * out[i - j];
            out[i] = v;
        }
        double e1 = 0;
        for (int i = 0; i < N; ++i) e1 += out[PRE + i] * out[PRE + i] * win[i] * win[i];
        const double g = std::sqrt(e0 / std::max(e1, 1e-20));
        for (int i = 0; i < N; ++i) y[st + i] += (float)(out[PRE + i] * g * win[i]);
    }
    y.resize(x.size());
    filt(y, Biquad::highpass(380)); filt(y, Biquad::highpass(380));
    return y;
}

}
