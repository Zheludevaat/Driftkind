#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "SammuMatcher.h"
#include "GranularEngine.h"
#include "SpectralEngine.h"
#include "HybridEngine.h"
#include <array>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>

//==============================================================================
// Represents a single audio file in the pool. Contains basic analysis results
// that guide the granular engine when selecting regions of the file.
struct SourceFile
{
    juce::AudioBuffer<float> audio;                  // Audio data (mono or stereo)
    std::vector<int> onsets;                         // Detected onset sample positions
    std::array<int, 12> pitchHist{};                 // Histogram of pitch classes
    float rms = 0.0f;                                // Root mean square level
    double sampleRate = 44100.0;                     // Sample rate of the audio file

    void analyze();                                  // Performs lightweight analysis
};

//==============================================================================
// Simplified audio processor that manages the file pool. The actual processor
// would expose parameters and perform real‑time processing, but for the purpose
// of this task we only focus on ingesting files and analysing them.
class SamuRiserAudioEditor;

class SamuRiserAudioProcessor : public juce::AudioProcessor
{
public:
    SamuRiserAudioProcessor();
    ~SamuRiserAudioProcessor() override;

    // Ingests a new audio file, analyses it and stores it in the pool.
    // Returns a Result detailing success or the reason for failure so the
    // caller can surface informative error messages.
    juce::Result addFile(const juce::File& file);

    //=== JUCE boilerplate =====================================================
    const juce::String getName() const override { return "SamuRiser"; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    void prepareToPlay(double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Build/refresh the rendered loop using the segment matcher
    void rebuildLoop(double bpm, int totalBars, int barsPerSeg,
                     bool minorMode, int keyRoot);
    void rebuildLoopAsync(double bpm, int totalBars, int barsPerSeg,
                          bool minorMode, int keyRoot);
    void cancelRebuild();
    float getRebuildProgress() const { return rebuildProgress.load(); }
    bool isRebuilding() const { return rebuilding.load(); }
    float getOutputLevel() const { return outputLevel.load(); }
    int getActiveGrains() const { return engine.getActiveCount(); }
    juce::Result exportLoopToFile(const juce::File& file);
    juce::Result savePreset(const juce::File& file);
    juce::Result loadPreset(const juce::File& file);
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    int getPoolSize() const { return (int)pool.load()->size(); }

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioFormatManager formatManager;                         // Reads audio files
    using Pool = std::vector<std::shared_ptr<SourceFile>>;
    std::atomic<std::shared_ptr<Pool>> pool { std::make_shared<Pool>() }; // lock-free pool

    std::unique_ptr<SegmentAnalyzer> analyzer;                      // Segment analysis
    std::unique_ptr<SegmentGluedRenderer> renderer;                 // Rendering glue
    SegmentMatcher matcher;                                         // Segment matcher
    GranularEngine engine;                                          // Real-time granular engine
    SpectralEngine spectralEngine;                                  // Spectral stub
    HybridEngine  hybridEngine;                                     // Hybrid stub

    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                 juce::dsp::IIR::Coefficients<float>>;
    juce::dsp::ProcessorChain<Filter, Filter> tiltEq;               // simple tilt EQ
    juce::dsp::Limiter<float> limiter;                              // output limiter

      std::atomic<std::shared_ptr<juce::AudioBuffer<float>>> outputLoop { std::make_shared<juce::AudioBuffer<float>>() }; // rendered loop
      int playHead = 0;                                               // Playback cursor
      int lastSeed = -1;                                              // Cache previous seed

      std::atomic<float> rebuildProgress {0.0f};
      std::atomic<bool> rebuilding {false};
      std::atomic<float> outputLevel {0.0f};                       // last RMS level

      std::thread rebuildThread;                                      // background rebuild
      std::atomic<bool> cancelRebuildFlag { false };                  // cancellation flag
      juce::SmoothedValue<float> specCutoffSmoothed;                  // parameter smoothing
      juce::SmoothedValue<float> hybridMixSmoothed;
  };

