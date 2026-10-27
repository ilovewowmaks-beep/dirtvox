#pragma once
// DSP Dirt Vox: фильтры, компрессор, цепочка обработки вокала и анализатор.
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <array>

namespace dv
{
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

// Биквад по RBJ cookbook, transposed direct form II.
struct Biquad
{
    enum Type { LP, HP, BP, Peak };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    void set (Type t, double sr, double f, double q, double gainDb = 0)
    {
        f = std::clamp (f, 10.0, sr * 0.45);
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        double nb0 = 1, nb1 = 0, nb2 = 0, na0 = 1, na1 = 0, na2 = 0;
        switch (t)
        {
            case LP:   nb0 = (1 - cw) / 2; nb1 = 1 - cw;    nb2 = (1 - cw) / 2; na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha; break;
            case HP:   nb0 = (1 + cw) / 2; nb1 = -(1 + cw); nb2 = (1 + cw) / 2; na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha; break;
            case BP:   nb0 = alpha;        nb1 = 0;         nb2 = -alpha;       na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha; break;
            case Peak: nb0 = 1 + alpha * A; nb1 = -2 * cw;  nb2 = 1 - alpha * A; na0 = 1 + alpha / A; na1 = -2 * cw; na2 = 1 - alpha / A; break;
        }
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }

    inline float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

// Компрессор с мягким коленом; детектор — пик по обоим каналам, сглаживание в дБ.
struct Comp
{
    float thr = -20, ratio = 4, knee = 6, makeup = 0, attC = 0, relC = 0, gr = 0;
    double sr = 44100;

    void prepare (double s) { sr = s; gr = 0; }

    void set (float t, float r, float attMs, float relMs, float mk)
    {
        thr = t; ratio = r; makeup = mk;
        attC = (float) std::exp (-1.0 / (attMs * 0.001 * sr));
        relC = (float) std::exp (-1.0 / (relMs * 0.001 * sr));
    }

    // Возвращает усиление в дБ для текущего уровня (в дБ).
    inline float gainDb (float lvl)
    {
        const float over = lvl - thr;
        float red;
        if (2 * over < -knee)                red = 0;
        else if (2 * std::abs (over) <= knee) red = (1 - 1 / ratio) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
        else                                  red = (1 - 1 / ratio) * over;
        const float c = red > gr ? attC : relC;
        gr = red + c * (gr - red);
        return makeup - gr;
    }
};

struct Params
{
    float trim = 0, hpf = 90, mud = 2, presence = 3, compThr = -24, deess = -30;
    float dirt = 0.6f, tone = 9000, mix = 0.8f, output = 0;
    int character = 1;
};

// Цепочка: trim → HPF 24 дБ/окт → вырез каши → присутствие → компрессор →
// [2x оверсэмплинг: параллельный перегруз] → де-эссер → склейка → выход + мягкий клиппер.
class VocalChain
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sr = sampleRate;
        nch = std::clamp (numChannels, 1, 2);
        os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) nch, 1,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        maxN = std::max (16, maxBlock);
        os->initProcessing ((size_t) maxN);
        for (auto& v : dryBuf) v.assign ((size_t) maxN * 2 + 16, 0.0f);
        comp1.prepare (sr); glue.prepare (sr);
        deAtt = (float) std::exp (-1.0 / (0.0015 * sr));
        deRel = (float) std::exp (-1.0 / (0.060 * sr));
        for (int c = 0; c < 2; ++c)
        {
            for (auto* b : { &hp1[c], &hp2[c], &mud[c], &pres[c], &preEmph[c], &deEmph[c], &tone1[c], &tone2[c], &xl1[c], &xl2[c], &xh1[c], &xh2[c] })
                b->reset();
            xl1[c].set (Biquad::LP, sr, 7000, 0.7071); xl2[c].set (Biquad::LP, sr, 7000, 0.7071);
            xh1[c].set (Biquad::HP, sr, 7000, 0.7071); xh2[c].set (Biquad::HP, sr, 7000, 0.7071);
            const double osr = sr * 2;
            preEmph[c].set (Biquad::Peak, osr, 1200, 0.7, 2.5);
            deEmph[c].set (Biquad::Peak, osr, 1200, 0.7, -2.5);
        }
        deEnv = 0; dryPow = wetPow = 1.0e-6; wetComp = 1;
        cached = Params(); cached.hpf = -1; // заставим пересчитать коэффициенты
        trimS.reset (sr, 0.05); outS.reset (sr, 0.05);
        trimS.setCurrentAndTargetValue (1); outS.setCurrentAndTargetValue (1);
    }

    int latency() const { return os ? (int) std::lround (os->getLatencyInSamples()) : 0; }

    float getDeessGr() const { return lastDeGr; }
    float getCompGr() const  { return comp1.gr; }

    void process (juce::AudioBuffer<float>& buf, const Params& p)
    {
        const int total = buf.getNumSamples();
        const int ch = std::min (nch, buf.getNumChannels());
        if (total == 0 || ch == 0) return;
        updateCoefficients (p);
        // Хост может прислать блок больше заявленного — режем, чтобы не выделять память в аудиопотоке.
        for (int start = 0; start < total; start += maxN)
            processChunk (buf, start, std::min (maxN, total - start), ch, p);
        if (buf.getNumChannels() > 1 && ch == 1) buf.copyFrom (1, 0, buf, 0, 0, total);
    }

private:
    void processChunk (juce::AudioBuffer<float>& buf, int start, int n, int ch, const Params& p)
    {
        float* d[2] = { buf.getWritePointer (0, start), buf.getWritePointer (ch > 1 ? 1 : 0, start) };

        // 1) Trim, EQ, компрессор
        trimS.setTargetValue (dbToGain (p.trim));
        for (int i = 0; i < n; ++i)
        {
            const float g = trimS.getNextValue();
            float peak = 0;
            for (int c = 0; c < ch; ++c)
            {
                float x = d[c][i] * g;
                x = hp2[c].process (hp1[c].process (x));
                x = pres[c].process (mud[c].process (x));
                d[c][i] = x;
                peak = std::max (peak, std::abs (x));
            }
            const float cg = dbToGain (comp1.gainDb (gainToDb (peak)));
            for (int c = 0; c < ch; ++c) d[c][i] *= cg;
        }

        // 2) Перегруз на удвоенной частоте (меньше алиасинга)
        auto block = juce::dsp::AudioBlock<float> (buf).getSubsetChannelBlock (0, (size_t) ch).getSubBlock ((size_t) start, (size_t) n);
        auto up = os->processSamplesUp (block);
        const int un = (int) up.getNumSamples();
        const float drive = dbToGain (6.0f + p.dirt * 36.0f);
        double dp = 0, wp = 0;
        for (int c = 0; c < ch; ++c)
        {
            float* s = up.getChannelPointer ((size_t) c);
            for (int i = 0; i < un; ++i)
            {
                const float dry = s[i];
                float w = preEmph[c].process (dry) * drive;
                w = shape (w, p.character);
                w = tone2[c].process (tone1[c].process (deEmph[c].process (w)));
                dp += (double) dry * dry; wp += (double) w * w;
                s[i] = w; // уровень выровняем ниже
                dryBuf[(size_t) c][(size_t) i] = dry;
            }
        }
        // Автовыравнивание громкости мокрого сигнала под сухой (медленно, ~0.3 с)
        const double k = std::exp (-(double) un / (0.3 * sr * 2));
        dryPow = dryPow * k + dp / std::max (1, un * ch) * (1 - k);
        wetPow = wetPow * k + wp / std::max (1, un * ch) * (1 - k);
        const float targetComp = (float) std::clamp (std::sqrt (dryPow / std::max (wetPow, 1.0e-12)), 0.05, 4.0);
        const float mix = std::clamp (p.mix, 0.0f, 1.0f);
        for (int c = 0; c < ch; ++c)
        {
            float* s = up.getChannelPointer ((size_t) c);
            float wc = wetComp;
            const float step = (targetComp - wetComp) / (float) std::max (1, un);
            for (int i = 0; i < un; ++i)
            {
                wc += step;
                s[i] = dryBuf[(size_t) c][(size_t) i] * (1 - mix) + s[i] * wc * mix;
            }
        }
        wetComp = targetComp;
        os->processSamplesDown (block);

        // 3) Де-эссер (LR4 кроссовер 5.5 кГц), 4) склейка, 5) выход + клиппер
        outS.setTargetValue (dbToGain (p.output));
        const float ceil = dbToGain (-1.0f);
        float maxDe = 0;
        for (int i = 0; i < n; ++i)
        {
            float lo[2], hi[2], hpk = 0;
            for (int c = 0; c < ch; ++c)
            {
                const float x = d[c][i];
                lo[c] = xl2[c].process (xl1[c].process (x));
                hi[c] = xh2[c].process (xh1[c].process (x));
                hpk = std::max (hpk, std::abs (hi[c]));
            }
            const float lvl = gainToDb (hpk);
            const float tgt = std::min (6.0f, std::max (0.0f, (lvl - p.deess) * (1 - 1.0f / 3)));
            deEnv = tgt + (tgt > deEnv ? deAtt : deRel) * (deEnv - tgt);
            maxDe = std::max (maxDe, deEnv);
            const float hg = dbToGain (-deEnv);
            float peak = 0;
            for (int c = 0; c < ch; ++c)
            {
                d[c][i] = lo[c] + hi[c] * hg;
                peak = std::max (peak, std::abs (d[c][i]));
            }
            const float g = dbToGain (glue.gainDb (gainToDb (peak))) * outS.getNextValue();
            for (int c = 0; c < ch; ++c) d[c][i] = softClip (d[c][i] * g, ceil);
        }
        lastDeGr = maxDe;
    }

    static inline float softClip (float x, float c)
    {
        const float a = std::abs (x) / c, t = 0.7f;
        if (a < t) return x;
        return std::copysign (c * (t + (1 - t) * std::tanh ((a - t) / (1 - t))), x);
    }

    static inline float shape (float w, int character)
    {
        switch (character)
        {
            case 0: return std::tanh (w); // Warm — ламповая мягкость
            case 2: {                      // Blown — разорванный динамик: фолд + крош
                const float t = std::tanh (w * 1.5f);
                const float f = std::sin (t * 2.2f);
                return 0.5f * f + 0.5f * std::round (f * 24.0f) / 24.0f;
            }
            default: {                     // Rage — асимметричный жёсткий перегруз
                const float b = 0.35f;
                const float y = std::tanh (w + b) - std::tanh (b);
                return 0.6f * y + 0.4f * std::clamp (w, -0.9f, 0.9f);
            }
        }
    }

    static bool differs (float a, float b) { return std::abs (a - b) > 1.0e-6f; }

    void updateCoefficients (const Params& p)
    {
        if (differs (p.hpf, cached.hpf))
            for (int c = 0; c < 2; ++c) { hp1[c].set (Biquad::HP, sr, p.hpf, 0.541); hp2[c].set (Biquad::HP, sr, p.hpf, 1.307); }
        if (differs (p.mud, cached.mud))
            for (int c = 0; c < 2; ++c) mud[c].set (Biquad::Peak, sr, 320, 1.1, -p.mud);
        if (differs (p.presence, cached.presence))
            for (int c = 0; c < 2; ++c) pres[c].set (Biquad::Peak, sr, 3800, 0.8, p.presence);
        if (differs (p.tone, cached.tone))
            for (int c = 0; c < 2; ++c) { tone1[c].set (Biquad::LP, sr * 2, p.tone, 0.541); tone2[c].set (Biquad::LP, sr * 2, p.tone, 1.307); }
        if (differs (p.compThr, cached.compThr))
        {
            // Мейкап примерно компенсирует среднее сжатие сигнала около −18 dBFS RMS.
            const float makeup = std::max (0.0f, (-14.0f - p.compThr)) * (1 - 1.0f / 4) * 0.8f;
            comp1.set (p.compThr, 4.0f, 4.0f, 90.0f, makeup);
            glue.set (-10.0f, 2.0f, 15.0f, 150.0f, 1.0f);
        }
        cached = p;
    }

    double sr = 44100;
    int nch = 2, maxN = 512;
    Biquad hp1[2], hp2[2], mud[2], pres[2], preEmph[2], deEmph[2], tone1[2], tone2[2], xl1[2], xl2[2], xh1[2], xh2[2];
    Comp comp1, glue;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::array<std::vector<float>, 2> dryBuf;
    double dryPow = 1.0e-6, wetPow = 1.0e-6;
    float wetComp = 1, deEnv = 0, deAtt = 0, deRel = 0, lastDeGr = 0;
    juce::SmoothedValue<float> trimS, outS;
    Params cached;
};

// Анализатор: слушает вход, копит энергию по полосам только там, где есть голос (гейт −50 dBFS).
struct Analyzer
{
    static constexpr int win = 1024;
    static constexpr int maxWins = 4096;

    void prepare (double s)
    {
        sr = s;
        low.set (Biquad::LP, sr, 120, 0.7071);
        mud.set (Biquad::BP, sr, 320, 1.0);
        pres.set (Biquad::BP, sr, 3800, 1.0);
        sib.set (Biquad::BP, sr, 7000, 1.5);
        reset();
    }

    void reset()
    {
        for (auto* b : { &low, &mud, &pres, &sib }) b->reset();
        tot = lo = md = pr = sb = 0; n = 0; nWin = 0; peak = 0;
        wTot = wLo = wMd = wPr = wSb = 0; wN = 0; wPeak = 0;
    }

    void push (const float* L, const float* R, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float a = low.process (m), b = mud.process (m), c = pres.process (m), d = sib.process (m);
            wTot += m * m; wLo += a * a; wMd += b * b; wPr += c * c; wSb += d * d;
            wPeak = std::max (wPeak, std::abs (m));
            if (++wN == win)
            {
                const float db = 10.0f * std::log10 ((float) (wTot / win) + 1.0e-20f);
                if (db > -50.0f)
                {
                    tot += wTot; lo += wLo; md += wMd; pr += wPr; sb += wSb; n += win;
                    peak = std::max (peak, wPeak);
                    if (nWin < maxWins) { winDb[(size_t) nWin] = db; sibDb[(size_t) nWin] = 10.0f * std::log10 ((float) (wSb / win) + 1.0e-20f); ++nWin; }
                }
                wTot = wLo = wMd = wPr = wSb = 0; wN = 0; wPeak = 0;
            }
        }
    }

    double seconds() const { return (double) n / sr; }

    double sr = 44100;
    Biquad low, mud, pres, sib;
    double tot = 0, lo = 0, md = 0, pr = 0, sb = 0, wTot = 0, wLo = 0, wMd = 0, wPr = 0, wSb = 0;
    long long n = 0;
    int wN = 0, nWin = 0;
    float peak = 0, wPeak = 0;
    std::array<float, maxWins> winDb {}, sibDb {};
};
} // namespace dv
