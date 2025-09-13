#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <memory>
#include <cmath>

struct SourceFile;
struct Grain
{
    std::shared_ptr<const SourceFile> src; // reference-counted source audio
    float pos = 0.0f;                      // current fractional position in samples
    int   len = 0;                         // grain length in samples
    float rate = 1.0f;                     // playback rate (pitch)
    float amp = 1.0f;                      // linear amplitude
    float envPhase = 0.0f;                 // 0..1 envelope phase
    float envInc = 0.0f;                   // envelope increment per sample
    bool  active = false;
};

class GranularEngine
{
public:
    GranularEngine();

    void setSampleRate(double sr) { sampleRate = sr; density.reset(sr, 0.05); texture.reset(sr, 0.05); }
    void setPool(std::shared_ptr<const std::vector<std::shared_ptr<SourceFile>>> p) { pool = std::move(p); }
    void setKey(int root, bool minor) { keyRoot = root; isMinor = minor; }
    void setSeed(uint32_t s) { rng.setSeed(s); }
    void setDensity(float d) { density.setTargetValue(juce::jlimit(0.0f, 1.0f, d)); }
    void setTexture(float t) { texture.setTargetValue(juce::jlimit(0.0f, 1.0f, t)); }
    void setHumanize(bool h) { humanize = h; }

    // Map a MIDI note to the nearest allowed pitch in the current key.
    int snapToScale(int midiNote) const;

    void spawnGrains(int samplesNeeded);
    void renderGrains(juce::AudioBuffer<float>& out, int numSamples);
    void process(juce::AudioBuffer<float>& out, int numSamples, double bpm, int bars);
    void renderLoop(juce::AudioBuffer<float>& out, double bpm, int bars);

    // Current number of active grains for UI metering
    int getActiveCount() const { return activeCount.load(); }

private:
    double sampleRate = 44100.0;                                  // processing sample rate
    std::shared_ptr<const std::vector<std::shared_ptr<SourceFile>>> pool; // analysed file pool
    int   keyRoot = 0;                                            // 0=C, 1=C# ... 11=B
    bool  isMinor = false;                                       // major/minor flag
    juce::dsp::SmoothedValue<float> density {0.5f};             // smoothed density
    juce::dsp::SmoothedValue<float> texture {0.5f};             // smoothed texture
    bool humanize = false;                                      // timing variance toggle
    juce::Random rng { 0x1234abcd };                             // deterministic random
    double currentBpm = 120.0;                                   // cached tempo
    int samplesUntilNext = 0;                                     // samples until next spawn
    int loopSamples = 0;                                          // total samples in loop
    int playHead = 0;                                             // current playback position

    static constexpr int maxGrains = 64;
    std::array<Grain, maxGrains> grains;                         // active grains
    std::array<float, 2048> envTable{};                          // precomputed Hann

    std::atomic<int> activeCount {0};                             // active grain counter

    float pitchToRate(int semitones) const { return std::pow(2.0f, semitones / 12.0f); }
    float getEnv(float phase) const;
};

