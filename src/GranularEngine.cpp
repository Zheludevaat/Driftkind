#include "GranularEngine.h"
#include "SamuRiserAudioProcessor.h" // for SourceFile

#include <algorithm>
#include <numeric>

//==============================================================================
// Returns the nearest MIDI note that belongs to the current key/scale.
int GranularEngine::snapToScale(int midiNote) const
{
    // Allowed pitch classes for major and minor scales
    static constexpr int majorScale[7] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int minorScale[7] = {0, 2, 3, 5, 7, 8, 10};

    const auto& scale = isMinor ? minorScale : majorScale;

    // Build a lookup of allowed pitch classes shifted by keyRoot
    std::array<bool, 12> allowed{};
    allowed.fill(false);
    for (int step : scale)
        allowed[(step + keyRoot + 12) % 12] = true;

    // Normalise the incoming note to a positive pitch class
    const int baseNote = midiNote;
    const int pc = ((midiNote % 12) + 12) % 12;

    if (allowed[pc])
        return baseNote; // already in scale

    // Search outward for the nearest allowed pitch class
    for (int offset = 1; offset <= 12; ++offset)
    {
        int upPC = (pc + offset) % 12;
        if (allowed[upPC])
            return baseNote + offset;

        int downPC = (pc - offset + 12) % 12;
        if (allowed[downPC])
            return baseNote - offset;
    }

    return baseNote; // fallback, shouldn't reach here
}

//==============================================================================
GranularEngine::GranularEngine()
{
    for (size_t i = 0; i < envTable.size(); ++i)
        envTable[i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi *
                                             (float)i / (envTable.size() - 1));
}

float GranularEngine::getEnv(float phase) const
{
    const float pos = juce::jlimit(0.0f, 1.0f, phase) * (envTable.size() - 1);
    const int idx = (int)pos;
    const int next = juce::jmin(idx + 1, (int)envTable.size() - 1);
    const float frac = pos - (float)idx;
    return envTable[(size_t)idx] + frac * (envTable[(size_t)next] - envTable[(size_t)idx]);
}

//==============================================================================
// Schedule new grains based on density, texture and a 16th-note tempo grid.
void GranularEngine::spawnGrains(int samplesNeeded)
{
    auto p = pool; // atomic load
    if (!p || p->empty())
        return;

    // Guard against invalid or extremely low BPM values which could otherwise
    // produce huge grain intervals or divide-by-zero errors. 20 BPM is a
    // sensible lower bound for musical material.
    const double safeBpm = juce::jmax(20.0, currentBpm);

    // Length of a 16th note in samples
    const int grid = static_cast<int>(sampleRate * 60.0 / (safeBpm * 4.0));

    samplesUntilNext -= samplesNeeded;

    while (samplesUntilNext <= 0)
    {
        int jitter = 0;
        if (humanize)
        {
            const int maxJit = (int)(0.02 * sampleRate); // +/-20ms
            jitter = rng.nextInt(maxJit * 2) - maxJit;
        }

        samplesUntilNext += grid + jitter;

        const float dens = density.getNextValue();
        if (rng.nextFloat() > dens)
            continue; // skip this slot based on density

        // Find an inactive grain slot
        Grain* g = nullptr;
        for (auto& gr : grains)
        {
            if (!gr.active)
            {
                g = &gr;
                break;
            }
        }

        if (g == nullptr)
            break; // no free grain slots

        auto src = (*p)[rng.nextInt((int)p->size())];

        int start = 0;
        const float tex = texture.getNextValue();
        if (!src->onsets.empty() && rng.nextFloat() < (1.0f - tex))
            start = src->onsets[rng.nextInt((int)src->onsets.size())];
        else
            start = rng.nextInt(src->audio.getNumSamples());

        const int maxLen = juce::jmin(2048, src->audio.getNumSamples() - start);
        if (maxLen < 2)
            continue;

        // Determine dominant pitch class and snap to key. If the source has no
        // detectable pitch content (empty histogram) we skip snapping so that
        // noise or percussive material plays back unshifted.
        int semitone = 0;
        const int pitchSum = std::accumulate(src->pitchHist.begin(), src->pitchHist.end(), 0);
        if (pitchSum > 0)
        {
            const auto it = std::max_element(src->pitchHist.begin(), src->pitchHist.end());
            const int dominantPC = (int)std::distance(src->pitchHist.begin(), it);
            const int baseNote = 60 + dominantPC;
            const int snapped = snapToScale(baseNote);
            semitone = snapped - baseNote;
        }

        g->src = src;
        g->pos = static_cast<float>(start);
        g->len = maxLen;
        g->rate = pitchToRate(semitone);
        g->amp = 1.0f;
        g->envPhase = 0.0f;
        g->envInc = 1.0f / static_cast<float>(maxLen);
        g->active = true;
    }
}

//==============================================================================
void GranularEngine::renderGrains(juce::AudioBuffer<float>& out, int numSamples)
{
    const int outChans = out.getNumChannels();

    int active = 0;
    for (auto& g : grains)
    {
        if (!g.active || g.src == nullptr)
            continue;
        ++active;

        const auto& srcBuf = g.src->audio;
        const int srcChans = srcBuf.getNumChannels();
        const int srcLen   = srcBuf.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            if (g.envPhase >= 1.0f)
            {
                g.active = false;
                break;
            }

            const int idx = (int) g.pos;
            if (idx < 1 || idx + 2 >= srcLen)
            {
                g.active = false;
                break;
            }

            const float frac = g.pos - static_cast<float>(idx);
            const float env  = getEnv(g.envPhase);

            for (int ch = 0; ch < outChans; ++ch)
            {
                const float* src = srcBuf.getReadPointer(ch % srcChans);
                const float y0 = src[idx - 1];
                const float y1 = src[idx];
                const float y2 = src[idx + 1];
                const float y3 = src[idx + 2];
                const float a0 = y3 - y2 - y0 + y1;
                const float a1 = y0 - y1 - a0;
                const float a2 = y2 - y0;
                const float a3 = y1;
                const float sample = ((a0 * frac + a1) * frac + a2) * frac + a3;
                out.addSample(ch, i, sample * env * g.amp);
            }

            g.pos += g.rate;
            g.envPhase += g.envInc;
        }
    }

    activeCount.store(active, std::memory_order_relaxed);
}

//==============================================================================
void GranularEngine::process(juce::AudioBuffer<float>& out, int numSamples, double bpm, int bars)
{
    out.clear();

    currentBpm = bpm;
    loopSamples = static_cast<int>(sampleRate * 60.0 / bpm * 4.0 * bars);

    spawnGrains(numSamples);
    renderGrains(out, numSamples);

    playHead += numSamples;
    if (loopSamples > 0)
        playHead %= loopSamples;
}

//==============================================================================
void GranularEngine::renderLoop(juce::AudioBuffer<float>& out, double bpm, int bars)
{
    const int total = static_cast<int>(sampleRate * 60.0 / bpm * 4.0 * bars);
    out.setSize(2, total); // stereo buffer
    out.clear();

    // Reset state
    for (auto& g : grains)
        g.active = false;
    samplesUntilNext = 0;
    playHead = 0;

    juce::AudioBuffer<float> temp(out.getNumChannels(), 512);
    int rendered = 0;
    while (rendered < total)
    {
        const int block = juce::jmin(512, total - rendered);
        temp.setSize(out.getNumChannels(), block, false, false, true);
        process(temp, block, bpm, bars);
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            out.addFrom(ch, rendered, temp, ch, 0, block);
        rendered += block;
    }
}

