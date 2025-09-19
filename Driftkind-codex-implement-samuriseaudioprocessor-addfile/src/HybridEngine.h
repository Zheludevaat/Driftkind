#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "SpectralEngine.h"

class HybridEngine
{
public:
    void setSampleRate(double sr) { sampleRate = sr; spectral.setSampleRate(sr); }
    void setMix(float m) { mix = juce::jlimit(0.0f, 1.0f, m); }
    void setPitchRatio(float r) { spectral.setPitchRatio(r); }

    // Blend spectral and granular outputs for a simple "hybrid" flavour.
    void process(juce::AudioBuffer<float>& buffer, int numSamples, double bpm, int bars);

private:
    double sampleRate = 44100.0;
    SpectralEngine spectral;
    juce::AudioBuffer<float> temp;
    float mix = 0.5f;
};
