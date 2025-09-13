#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#ifdef SAMURISE_USE_RUBBERBAND
 #include <rubberband/RubberBandStretcher.h>
#endif

class SpectralEngine
{
public:
    void setSampleRate(double sr);
    void setCutoff(float c) { cutoff = c; }
    void setPitchRatio(float r) { pitchRatio = r; }

    // Apply a simple FFT-based low-pass filter or optional Rubber Band pitch shift.
    // This remains a lightweight placeholder for a true spectral resynthesis engine
    // and exists to provide a distinct processing path from the granular engine.
    void process(juce::AudioBuffer<float>& buffer, int numSamples, double, int);

private:
    double sampleRate = 44100.0;
    juce::dsp::FFT fft{10};                                   // 1024-point FFT
    juce::dsp::WindowingFunction<float> window{ (size_t)1024,
        juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftBuffer;                             // real/imag pairs
    std::vector<float> prevInPhase;                           // phase vocoder state
    std::vector<float> prevOutPhase;                          // phase vocoder state
    std::vector<float> prevMag;                               // magnitude envelope
    juce::AudioBuffer<float> accum;                           // reuse accumulation buffer
    std::vector<float> dest;                                  // reused FFT output
#ifdef SAMURISE_USE_RUBBERBAND
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
#endif
    float cutoff = 5000.0f;                                   // Hz
    float pitchRatio = 1.0f;
};
