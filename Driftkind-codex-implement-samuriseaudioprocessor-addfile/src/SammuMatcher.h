#pragma once
#include <JuceHeader.h>
#include <atomic>
#ifdef SAMURISE_USE_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

inline float hann(float x) { return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * x); }

struct ScaleMask
{
    std::array<int, 12> mask{}; // 1 = allowed
    static ScaleMask makeMajor(int root) { return makeFromPCs(root, {0,2,4,5,7,9,11}); }
    static ScaleMask makeMinor(int root) { return makeFromPCs(root, {0,2,3,5,7,8,10}); }
    static ScaleMask makeFromPCs(int root, std::initializer_list<int> pcs)
    {
        ScaleMask m; m.mask.fill(0);
        for (auto pc : pcs)
            m.mask[(root + pc + 120) % 12] = 1;
        return m;
    }
};

struct Segment
{
    const juce::AudioBuffer<float>* src = nullptr;
    int start = 0;
    int length = 0;
    double srcSR = 44100.0;
    std::array<float,12> chroma{};
    float onsetDensity = 0.0f;
    int   dominantPC = 0;
    float rms = 0.0f;
};

class SegmentAnalyzer
{
public:
    SegmentAnalyzer()
    {
        window.setSize(1, 1 << 11);
        for (int i = 0; i < window.getNumSamples(); ++i)
            window.setSample(0, i, hann((float)i / (float)(window.getNumSamples()-1)));
    }

    void sliceIntoSegments(const juce::AudioBuffer<float>& in,
                           double fileSR, double bpm, int barsPerSeg,
                           std::vector<Segment>& out)
    {
        const double secPerBeat = 60.0 / bpm;
        const int segSamples = (int) std::round(barsPerSeg * 4 * secPerBeat * fileSR);
        const int hop = segSamples;
        for (int start = 0; start + segSamples <= in.getNumSamples(); start += hop)
        {
            Segment s;
            s.src = &in; s.start = start; s.length = segSamples; s.srcSR = fileSR;
            fingerprint(*s.src, s.start, s.length, fileSR, s.chroma, s.onsetDensity, s.dominantPC, s.rms);
            out.push_back(std::move(s));
        }
    }

private:
    juce::AudioBuffer<float> window;

    void fingerprint(const juce::AudioBuffer<float>& in, int start, int length, double sr,
                     std::array<float,12>& chromaOut, float& onsetDensity,
                     int& dominantPC, float& rmsOut)
    {
        juce::AudioBuffer<float> mono(1, length);
        mono.clear();
        for (int ch = 0; ch < in.getNumChannels(); ++ch)
            mono.addFrom(0, 0, in, ch, start, length, 1.0f / juce::jmax(1, in.getNumChannels()));

        const int fftSize = 1 << 11;
        const int hopSize = fftSize / 4;
        const int frames  = juce::jmax(1, (juce::jmax(0, length - fftSize)) / hopSize);

        std::array<double,12> chromaSum{}; chromaSum.fill(0.0);
        double rmsAccum = 0.0;
        int onsetCount = 0; float prevEnergy = 0.0f;
        int processedFrames = 0;

        juce::AudioBuffer<float> frame(1, fftSize);
        juce::dsp::FFT fft(11);
        juce::dsp::WindowingFunction<float> wfunc(fftSize, juce::dsp::WindowingFunction<float>::hann, true);

        for (int f = 0; f < frames; ++f)
        {
            const int pos = f * hopSize;
            frame.clear();
            const int available = juce::jmax(0, mono.getNumSamples() - pos);
            const int copyLen = juce::jmin(fftSize, available);
            if (copyLen <= 0)
                continue;

            frame.copyFrom(0, 0, mono, 0, pos, copyLen);
            if (copyLen < fftSize)
                frame.clear(0, copyLen, fftSize - copyLen);
            ++processedFrames;
            wfunc.multiplyWithWindowingTable(frame.getWritePointer(0), fftSize);
            fft.performRealOnlyForwardTransform(frame.getWritePointer(0));
            auto* reim = frame.getWritePointer(0);
            for (int k = 0; k < fftSize/2; ++k)
            {
                const float re = reim[2*k];
                const float im = reim[2*k+1];
                const float m  = std::sqrt(re*re + im*im) + 1e-9f;
                const float freq = (float)k * (float)sr / (float)fftSize;
                if (freq < 40.0f || freq > 8000.0f) continue;
                const float midi = 69.0f + 12.0f * std::log2(freq / 440.0f);
                const int pc = ((int)std::round(midi) % 12 + 12) % 12;
                chromaSum[pc] += m;
            }

            float energy = 0.0f;
            auto* d = frame.getReadPointer(0);
            for (int i = 0; i < fftSize; ++i) energy += d[i]*d[i];
            if (f > 0 && energy > prevEnergy * 1.35f) ++onsetCount;
            prevEnergy = energy;
            rmsAccum += std::sqrt(energy / (float)fftSize);
        }

        double total = 0.0; for (auto v : chromaSum) total += v;
        for (int i = 0; i < 12; ++i)
            chromaOut[i] = total > 0.0 ? (float)(chromaSum[i] / total) : (1.0f/12.0f);

        dominantPC = 0; float best = chromaOut[0];
        for (int i = 1; i < 12; ++i)
            if (chromaOut[i] > best) { best = chromaOut[i]; dominantPC = i; }

        const int normFrames = juce::jmax(1, processedFrames);
        onsetDensity = juce::jlimit(0.0f, 1.0f, (float)onsetCount / (float)normFrames);
        rmsOut = (float)(rmsAccum / (double)normFrames);
    }
};

struct MatchSettings
{
    float chromaWeight  = 0.8f;
    float onsetWeight   = 0.2f;
    float minSimilarity = 0.65f;
    int   barsPerSeg    = 2;
    ScaleMask scale     = ScaleMask::makeMinor(9);
};

inline float cosine12(const std::array<float,12>& a, const std::array<float,12>& b)
{
    double dot=0, na=0, nb=0; for (int i=0;i<12;++i){ dot+=a[i]*b[i]; na+=a[i]*a[i]; nb+=b[i]*b[i]; }
    return (float)(dot / (std::sqrt(na*nb)+1e-9));
}

class SegmentMatcher
{
public:
    std::vector<Segment> buildSchedule(const std::vector<std::vector<Segment>>& pools,
                                       int totalBars, const MatchSettings& ms)
    {
        std::vector<Segment> schedule;
        if (pools.empty()) return schedule;

        Segment seed = pickSeed(pools, ms);
        schedule.push_back(seed);

        const int segBars = ms.barsPerSeg;
        const int needSegs = juce::jmax(1, totalBars / segBars) - 1;

        Segment cur = seed;
        for (int i = 0; i < needSegs; ++i)
        {
            Segment best; float bestScore = -1.0f; bool found=false;
            for (const auto& pool : pools)
                for (const auto& cand : pool)
                {
                    if (cand.src == cur.src && std::abs(cand.start - cur.start) < cand.length) continue;
                    float sim = similarity(cur, cand, ms);
                    if (sim > ms.minSimilarity && sim > bestScore)
                    {
                        bestScore = sim; best = cand; found = true;
                    }
                }
            schedule.push_back(found ? best : seed);
            cur = schedule.back();
        }
        return schedule;
    }

private:
    static bool pcAllowed(int pc, const ScaleMask& m) { return m.mask[(pc+120)%12] != 0; }

    Segment pickSeed(const std::vector<std::vector<Segment>>& pools, const MatchSettings& ms)
    {
        Segment best; float score=-1e9f;
        for (const auto& pool : pools)
            for (const auto& s : pool)
            {
                float inKey = pcAllowed(s.dominantPC, ms.scale) ? 1.0f : 0.0f;
                float sc = 0.7f * inKey + 0.3f * s.rms;
                if (sc > score) { score = sc; best = s; }
            }
        return best;
    }

    float similarity(const Segment& a, const Segment& b, const MatchSettings& ms)
    {
        float c = cosine12(a.chroma, b.chroma);
        float o = 1.0f - std::abs(a.onsetDensity - b.onsetDensity);
        return ms.chromaWeight * c + ms.onsetWeight * o;
    }
};

class SegmentGluedRenderer
{
public:
    void setSampleRate(double sampleRate) { sr = sampleRate; }

    void render(const std::vector<Segment>& schedule, double bpm, int totalBars,
                int barsPerSeg, juce::AudioBuffer<float>& out, std::atomic<bool>& cancelFlag)
    {
        const int tgtLen = barsToSamples(totalBars, bpm);
        out.setSize(2, tgtLen, false, true, true);
        out.clear();

        const int segLenTgt = barsToSamples(barsPerSeg, bpm);
        const int xfade = (int) (0.02 * sr);

        juce::AudioBuffer<float> tmp(2, segLenTgt + xfade * 2);

        int cursor = 0;
        for (size_t i = 0; i < schedule.size(); ++i)
        {
            if (cancelFlag.load()) return;
            renderStretchedSegment(schedule[i], segLenTgt + (i==0?xfade:2*xfade), tmp);
            const int writeStart = juce::jmax(0, cursor - xfade);
            const int readStart  = (i==0?0:xfade);
            const int writeLen   = juce::jmin(out.getNumSamples() - writeStart, tmp.getNumSamples() - readStart);

            for (int n = 0; n < writeLen; ++n)
            {
                float w = 1.0f;
                const int pos = n + writeStart - cursor + xfade;
                if (pos < xfade) w = (float)pos / (float)xfade;
                else if (pos > (segLenTgt + xfade)) w = 1.0f - (float)(pos - (segLenTgt + xfade)) / (float)xfade;

                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                {
                    float a = out.getSample(ch, writeStart + n);
                    float b = tmp.getSample(juce::jmin(ch, tmp.getNumChannels()-1), readStart + n);
                    out.setSample(ch, writeStart + n, a * (1.0f - w) + b * w);
                }
            }

            cursor += segLenTgt;
            if (cursor >= out.getNumSamples() || cancelFlag.load()) break;
        }
    }

private:
    double sr = 48000.0;

    int barsToSamples(int bars, double bpm) const
    {
        const double sec = bars * 4.0 * 60.0 / bpm;
        return (int) std::round(sec * sr);
    }

    void renderStretchedSegment(const Segment& s, int targetLen, juce::AudioBuffer<float>& dst)
    {
#ifdef SAMURISE_USE_RUBBERBAND
        RubberBand::RubberBandStretcher stretcher(sr, 1,
            RubberBand::RubberBandStretcher::OptionProcessRealTime);
        stretcher.setTimeRatio((double)targetLen / (double)s.length);
        juce::AudioBuffer<float> mono(1, s.length);
        for (int ch = 0; ch < s.src->getNumChannels(); ++ch)
            mono.addFrom(0, 0, *s.src, ch, s.start, s.length, 1.0f / juce::jmax(1, s.src->getNumChannels()));
        stretcher.process(mono.getArrayOfReadPointers(), s.length, false);
        juce::AudioBuffer<float> outBuf(1, targetLen);
        stretcher.retrieve(outBuf.getArrayOfWritePointers(), targetLen);
        dst.makeCopyOf(outBuf, true);
        dst.setSize(2, targetLen, true, true, true);
        dst.copyFrom(1, 0, dst, 0, 0, targetLen); // duplicate mono to stereo
#else
        const int grain = (int)(0.08 * sr);
        const int hopIn = grain / 2;
        const int hopOut = grain / 3;
        const float rate = (float) s.length / (float) targetLen;

        dst.setSize(2, targetLen);
        dst.clear();

        juce::AudioBuffer<float> mono(1, s.length);
        mono.clear();
        for (int ch = 0; ch < s.src->getNumChannels(); ++ch)
            mono.addFrom(0, 0, *s.src, ch, s.start, s.length, 1.0f / juce::jmax(1, s.src->getNumChannels()));

        int srcPos = 0, dstPos = 0;
        juce::AudioBuffer<float> g(1, grain);
        while (dstPos + grain < dst.getNumSamples() && srcPos + grain < mono.getNumSamples())
        {
            for (int i = 0; i < grain; ++i)
            {
                const float x = (srcPos + (int)(i * rate));
                const int   i0 = juce::jlimit(0, mono.getNumSamples()-2, (int)x);
                const float t  = x - (float)i0;
                const float s0 = mono.getSample(0, i0);
                const float s1 = mono.getSample(0, i0+1);
                g.setSample(0, i, s0 + (s1 - s0) * t);
            }
            for (int i = 0; i < grain; ++i)
            {
                const float w = hann((float)i / (float)(grain-1));
                const float v = g.getSample(0, i) * w;
                for (int ch = 0; ch < dst.getNumChannels(); ++ch)
                {
                    const int p = dstPos + i;
                    if (p < dst.getNumSamples())
                        dst.addSample(ch, p, v * 0.7f);
                }
            }
            srcPos += hopIn;
            dstPos += hopOut;
        }
#endif
    }
};

