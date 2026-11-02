#pragma once
// Measuring what an isolator did. Every system is run so that its effect on the speech alone and on the noise alone
// is known (Hagerman's trick: process s+n and s-n, then add and subtract). From those:
// how much of each word's start and end survived, how much of the s/f/th band, how far the noise fell in pauses and
// under speech, STOI, SI-SDR, musical noise (log kurtosis ratio), and spectrograms to look at.
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "audio_kit.hpp"

namespace meas {
using kit::Sig;

struct Seg { int a, b; };

// Where the clean speech is: 5 ms frames above (loudest - 45 dB), gaps under 60 ms joined, blips under 30 ms dropped.
inline std::vector<Seg> segments(const Sig& s) {
    const int F = kit::FS / 200;
    const int nf = (int)(s.size() / F);
    std::vector<double> e(nf);
    double top = -300;
    for (int f = 0; f < nf; ++f) { e[f] = kit::dbOf(kit::power(s, (size_t)f * F, (size_t)(f + 1) * F)); top = std::max(top, e[f]); }
    const double thr = std::max(top - 45.0, -85.0);
    std::vector<Seg> out;
    int f = 0;
    while (f < nf) {
        while (f < nf && e[f] < thr) ++f;
        if (f >= nf) break;
        const int a = f;
        int lastOn = f;
        while (f < nf) {
            if (e[f] >= thr) lastOn = f;
            else if (f - lastOn > 12) break;
            ++f;
        }
        if (lastOn - a + 1 >= 6) out.push_back({a * F, (lastOn + 1) * F});
    }
    return out;
}

inline double energy(const Sig& x, int a, int b) {
    a = std::max(a, 0); b = std::min(b, (int)x.size());
    double e = 0;
    for (int i = a; i < b; ++i) e += (double)x[i] * x[i];
    return e;
}
// Attenuation in dB (positive = lost) of y against x over [a, b).
inline double lossDb(const Sig& x, const Sig& y, int a, int b) { return kit::dbOf(energy(x, a, b)) - kit::dbOf(energy(y, a, b)); }

// The loss over [a, b) counting only the time-frequency bins where the clean speech was at most 5 dB under the noise
// (what a listener could hear in the input). False when nothing there was audible.
inline bool audibleLoss(const Sig& s, const Sig& n, const Sig& out, int a, int b, double& loss) {
    const int N = 512, HOPN = 128;
    static std::vector<float> win;
    if (win.empty()) { win.resize(N); for (int i = 0; i < N; ++i) win[i] = (float)(0.5 - 0.5 * std::cos(2 * kit::PI * i / N)); }
    conv::FFT fft; fft.init(N);
    std::vector<conv::cf> fs(N), fn(N), fo(N);
    double es = 0, eo = 0;
    for (int c = a; c < b; c += HOPN) {
        const int st = c - N / 2;
        if (st < 0 || st + N > (int)s.size()) continue;
        for (int i = 0; i < N; ++i) { fs[i] = conv::cf(s[st + i] * win[i], 0); fn[i] = conv::cf(n[st + i] * win[i], 0); fo[i] = conv::cf(out[st + i] * win[i], 0); }
        fft.forward(fs.data()); fft.forward(fn.data()); fft.forward(fo.data());
        for (int k = 1; k < N / 2; ++k)
            if (std::norm(fs[k]) > 0.316 * std::norm(fn[k])) { es += std::norm(fs[k]); eo += std::norm(fo[k]); }
    }
    if (es <= 0) return false;
    loss = kit::dbOf(es) - kit::dbOf(eo);
    return true;
}

inline double siSdr(const Sig& ref, const Sig& est, int a, int b) {
    double rr = 0, re = 0;
    for (int i = a; i < b; ++i) { rr += (double)ref[i] * ref[i]; re += (double)ref[i] * est[i]; }
    const double al = re / (rr + 1e-30);
    double t = 0, e = 0;
    for (int i = a; i < b; ++i) { const double s = al * ref[i], d = est[i] - s; t += s * s; e += d * d; }
    return kit::dbOf(t) - kit::dbOf(e);
}

// STOI (Taal et al. 2011): 10 kHz, 256-sample frames, 15 third-octave bands from 150 Hz, 384 ms segments, -15 dB clip.
inline double stoi(const Sig& clean48, const Sig& proc48) {
    const Sig x = kit::resample(clean48, 48000, 10000), y = kit::resample(proc48, 48000, 10000);
    const int N = 256, HOPN = 128, NFFT = 512, J = 15, SEGN = 30;
    std::vector<double> w(N);
    for (int i = 0; i < N; ++i) w[i] = 0.5 - 0.5 * std::cos(2 * kit::PI * (i + 1) / (N + 1));
    const int nf = (int)((std::min(x.size(), y.size()) - N) / HOPN);
    std::vector<double> fe(nf);
    double top = -300;
    for (int f = 0; f < nf; ++f) {
        double e = 0; for (int i = 0; i < N; ++i) { const double v = x[f * HOPN + i] * w[i]; e += v * v; }
        fe[f] = 20 * std::log10(std::sqrt(e) + 1e-30); top = std::max(top, fe[f]);
    }
    std::vector<int> keep;
    for (int f = 0; f < nf; ++f) if (fe[f] > top - 40) keep.push_back(f);
    const int L = (int)keep.size() * HOPN + N;
    Sig xs(L, 0.f), ys(L, 0.f);
    for (size_t j = 0; j < keep.size(); ++j)
        for (int i = 0; i < N; ++i) { xs[j * HOPN + i] += (float)(x[keep[j] * HOPN + i] * w[i]); ys[j * HOPN + i] += (float)(y[keep[j] * HOPN + i] * w[i]); }
    conv::FFT fft; fft.init(NFFT);
    double cf[J], lo[J], hi[J];
    for (int j = 0; j < J; ++j) { cf[j] = 150.0 * std::pow(2.0, j / 3.0); lo[j] = cf[j] * std::pow(2.0, -1.0 / 6); hi[j] = cf[j] * std::pow(2.0, 1.0 / 6); }
    const int mf = (L - N) / HOPN;
    std::vector<std::vector<double>> X(J, std::vector<double>(mf)), Y(J, std::vector<double>(mf));
    std::vector<conv::cf> a(NFFT), b(NFFT);
    for (int m = 0; m < mf; ++m) {
        std::fill(a.begin(), a.end(), conv::cf(0, 0)); std::fill(b.begin(), b.end(), conv::cf(0, 0));
        for (int i = 0; i < N; ++i) { a[i] = conv::cf((float)(xs[m * HOPN + i] * w[i]), 0); b[i] = conv::cf((float)(ys[m * HOPN + i] * w[i]), 0); }
        fft.forward(a.data()); fft.forward(b.data());
        for (int j = 0; j < J; ++j) {
            double sx = 0, sy = 0;
            for (int k = 0; k <= NFFT / 2; ++k) {
                const double f = k * 10000.0 / NFFT;
                if (f >= lo[j] && f < hi[j]) { sx += std::norm(a[k]); sy += std::norm(b[k]); }
            }
            X[j][m] = std::sqrt(sx); Y[j][m] = std::sqrt(sy);
        }
    }
    const double clip = 1.0 + std::pow(10.0, 15.0 / 20.0);
    double sum = 0; int cnt = 0;
    for (int m = SEGN; m <= mf; ++m)
        for (int j = 0; j < J; ++j) {
            double nx = 0, ny = 0;
            for (int t = m - SEGN; t < m; ++t) { nx += X[j][t] * X[j][t]; ny += Y[j][t] * Y[j][t]; }
            const double al = std::sqrt(nx / (ny + 1e-30));
            std::vector<double> xv(SEGN), yv(SEGN);
            double mx = 0, my = 0;
            for (int t = 0; t < SEGN; ++t) { xv[t] = X[j][m - SEGN + t]; yv[t] = std::min(al * Y[j][m - SEGN + t], clip * xv[t]); mx += xv[t]; my += yv[t]; }
            mx /= SEGN; my /= SEGN;
            double sxy = 0, sxx = 0, syy = 0;
            for (int t = 0; t < SEGN; ++t) { const double dx = xv[t] - mx, dy = yv[t] - my; sxy += dx * dy; sxx += dx * dx; syy += dy * dy; }
            sum += sxy / (std::sqrt(sxx * syy) + 1e-30); ++cnt;
        }
    return cnt ? sum / cnt : 0.0;
}

// Musical noise: log of how much more peaky (kurtosis of the power spectrum) the noise got, over noise-only frames.
inline double logKurtosisRatio(const Sig& nIn, const Sig& nOut, const std::vector<char>& pauseMask) {
    const int N = 512, HOPN = 256;
    conv::FFT fft; fft.init(N);
    std::vector<conv::cf> a(N), b(N);
    double m2a = 0, m4a = 0, m2b = 0, m4b = 0; long cnt = 0;
    const int k0 = 2, k1 = (int)(8000.0 * N / kit::FS);
    for (size_t st = 0; st + N <= nIn.size(); st += HOPN) {
        bool ok = true;
        for (int i = 0; i < N && ok; i += 32) ok = pauseMask[st + i] != 0;
        if (!ok) continue;
        for (int i = 0; i < N; ++i) { const float wv = (float)(0.5 - 0.5 * std::cos(2 * kit::PI * i / N)); a[i] = conv::cf(nIn[st + i] * wv, 0); b[i] = conv::cf(nOut[st + i] * wv, 0); }
        fft.forward(a.data()); fft.forward(b.data());
        for (int k = k0; k < k1; ++k) { const double pa = std::norm(a[k]), pb = std::norm(b[k]); m2a += pa * pa; m4a += pa * pa * pa * pa; m2b += pb * pb; m4b += pb * pb * pb * pb; ++cnt; }
    }
    if (cnt < 100 || m2a <= 0 || m2b <= 0) return 0.0;
    const double ka = (m4a / cnt) / std::pow(m2a / cnt, 2), kb = (m4b / cnt) / std::pow(m2b / cnt, 2);
    return std::log(kb / ka);
}

// ---------- spectrogram picture ----------
struct Gdi {
    ULONG_PTR token = 0;
    Gdi() { Gdiplus::GdiplusStartupInput in; Gdiplus::GdiplusStartup(&token, &in, nullptr); }
    ~Gdi() { Gdiplus::GdiplusShutdown(token); }
};
inline bool pngClsid(CLSID* id) {
    UINT n = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&n, &size);
    if (!size) return false;
    std::vector<uint8_t> buf(size);
    auto* info = (Gdiplus::ImageCodecInfo*)buf.data();
    Gdiplus::GetImageEncoders(n, size, info);
    for (UINT i = 0; i < n; ++i) if (!wcscmp(info[i].MimeType, L"image/png")) { *id = info[i].Clsid; return true; }
    return false;
}
inline uint32_t heat(double t) {
    t = std::max(0.0, std::min(1.0, t));
    static const double c[6][3] = {{0, 0, 4}, {40, 11, 84}, {101, 21, 110}, {159, 42, 99}, {212, 72, 66}, {252, 255, 164}};
    const double p = t * 5; const int i = std::min(4, (int)p); const double f = p - i;
    const int r = (int)(c[i][0] + f * (c[i + 1][0] - c[i][0])), g = (int)(c[i][1] + f * (c[i + 1][1] - c[i][1])), b = (int)(c[i][2] + f * (c[i + 1][2] - c[i][2]));
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
struct Panel { std::wstring title; const Sig* x; };
// Stacked spectrograms (50 Hz to 16 kHz on a log-ish scale, 5 ms a column), marks at each speech start.
inline bool spectrogram(const std::wstring& path, const std::vector<Panel>& panels, const std::vector<Seg>& segs, int from = 0, int to = -1) {
    const int N = 1024, HOPN = 240, PH = 200, TITLE = 18;
    const size_t len = panels[0].x->size();
    if (to < 0 || to > (int)len) to = (int)len;
    const int cols = (to - from - N) / HOPN;
    if (cols <= 0) return false;
    const int W = cols, Hh = (int)panels.size() * (PH + TITLE);
    std::vector<uint32_t> px((size_t)W * Hh, 0xFF101010u);
    conv::FFT fft; fft.init(N);
    std::vector<conv::cf> a(N);
    std::vector<float> win(N);
    for (int i = 0; i < N; ++i) win[i] = (float)(0.5 - 0.5 * std::cos(2 * kit::PI * i / N));
    auto rowHz = [&](int y) { const double t = 1.0 - (double)y / (PH - 1); const double m = t * 2595 * std::log10(1 + 16000 / 700.0); return 700 * (std::pow(10.0, m / 2595) - 1); };
    double top = -300;
    std::vector<std::vector<float>> img(panels.size(), std::vector<float>((size_t)W * PH));
    for (size_t p = 0; p < panels.size(); ++p) {
        const Sig& x = *panels[p].x;
        for (int c = 0; c < W; ++c) {
            const int st = from + c * HOPN;
            for (int i = 0; i < N; ++i) a[i] = conv::cf(st + i < (int)x.size() ? x[st + i] * win[i] : 0.f, 0);
            fft.forward(a.data());
            for (int y = 0; y < PH; ++y) {
                const double hz = std::max(50.0, rowHz(y)), k = hz * N / kit::FS;
                const int k0 = (int)k; const double f = k - k0;
                const double pw = (1 - f) * std::norm(a[k0]) + f * std::norm(a[std::min(k0 + 1, N / 2)]);
                const float db = (float)kit::dbOf(pw);
                img[p][(size_t)y * W + c] = db;
                top = std::max(top, (double)db);
            }
        }
    }
    for (size_t p = 0; p < panels.size(); ++p)
        for (int y = 0; y < PH; ++y)
            for (int c = 0; c < W; ++c)
                px[(size_t)((int)p * (PH + TITLE) + TITLE + y) * W + c] = heat((img[p][(size_t)y * W + c] - (top - 85)) / 85);
    for (const Seg& s : segs) {
        const int c = (s.a - from) / HOPN;
        if (c < 0 || c >= W) continue;
        for (size_t p = 0; p < panels.size(); ++p)
            for (int y = 0; y < 6; ++y) px[(size_t)((int)p * (PH + TITLE) + TITLE + y) * W + c] = 0xFF40FF40u;
    }
    Gdiplus::Bitmap bmp(W, Hh, PixelFormat32bppARGB);
    Gdiplus::BitmapData bd;
    Gdiplus::Rect rc(0, 0, W, Hh);
    bmp.LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd);
    for (int y = 0; y < Hh; ++y) memcpy((uint8_t*)bd.Scan0 + (size_t)y * bd.Stride, &px[(size_t)y * W], (size_t)W * 4);
    bmp.UnlockBits(&bd);
    {
        Gdiplus::Graphics g(&bmp);
        Gdiplus::Font font(L"Segoe UI", 10);
        Gdiplus::SolidBrush br(Gdiplus::Color(255, 230, 230, 230));
        for (size_t p = 0; p < panels.size(); ++p)
            g.DrawString(panels[p].title.c_str(), -1, &font, Gdiplus::PointF(4.f, (float)(p * (PH + TITLE)) + 1.f), &br);
    }
    CLSID id;
    if (!pngClsid(&id)) return false;
    return bmp.Save(path.c_str(), &id) == Gdiplus::Ok;
}

}
