#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "SpectralEngine.h"
#include "HybridEngine.h"

namespace
{
float rmsDiff(const float* a, const float* b, int numSamples)
{
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += diff * diff;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(numSamples)));
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 2048;
    constexpr int numChannels = 2;

    juce::AudioBuffer<float> input;
    input.setSize(numChannels, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        input.setSample(0, i, std::sin(2.0 * juce::MathConstants<double>::pi * i / 64.0));
        input.setSample(1, i, std::cos(2.0 * juce::MathConstants<double>::pi * i / 43.0));
    }

    auto spectralInput = input;
    SpectralEngine spectralMulti;
    spectralMulti.setSampleRate(sampleRate);
    spectralMulti.setPitchRatio(1.0f);
    spectralMulti.process(spectralInput, numSamples, 120.0, 1);

    SpectralEngine spectralSingle0;
    spectralSingle0.setSampleRate(sampleRate);
    spectralSingle0.setPitchRatio(1.0f);
    juce::AudioBuffer<float> single0;
    single0.setSize(1, numSamples);
    single0.copyFrom(0, 0, input, 0, 0, numSamples);
    spectralSingle0.process(single0, numSamples, 120.0, 1);

    SpectralEngine spectralSingle1;
    spectralSingle1.setSampleRate(sampleRate);
    spectralSingle1.setPitchRatio(1.0f);
    juce::AudioBuffer<float> single1;
    single1.setSize(1, numSamples);
    single1.copyFrom(0, 0, input, 1, 0, numSamples);
    spectralSingle1.process(single1, numSamples, 120.0, 1);

    const float specDiff0 = rmsDiff(spectralInput.getReadPointer(0), single0.getReadPointer(0), numSamples);
    const float specDiff1 = rmsDiff(spectralInput.getReadPointer(1), single1.getReadPointer(0), numSamples);

    if (specDiff0 > 1.0e-3f || specDiff1 > 1.0e-3f)
    {
        std::cerr << "SpectralEngine multichannel mismatch: diff0=" << specDiff0
                  << ", diff1=" << specDiff1 << std::endl;
        std::_Exit(1);
    }

    auto hybridInput = input;
    HybridEngine hybrid;
    hybrid.setSampleRate(sampleRate);
    hybrid.setPitchRatio(1.0f);
    hybrid.setMix(0.35f);
    hybrid.process(hybridInput, numSamples, 120.0, 1);

    auto spectralForHybrid = input;
    SpectralEngine spectralForMix;
    spectralForMix.setSampleRate(sampleRate);
    spectralForMix.setPitchRatio(1.0f);
    spectralForMix.process(spectralForHybrid, numSamples, 120.0, 1);

    juce::AudioBuffer<float> expected;
    expected.makeCopyOf(spectralForHybrid);
    expected.applyGain(1.0f - 0.35f);
    for (int ch = 0; ch < numChannels; ++ch)
        expected.addFrom(ch, 0, input, ch, 0, numSamples, 0.35f);

    const float hybridDiff0 = rmsDiff(hybridInput.getReadPointer(0), expected.getReadPointer(0), numSamples);
    const float hybridDiff1 = rmsDiff(hybridInput.getReadPointer(1), expected.getReadPointer(1), numSamples);

    if (hybridDiff0 > 1.0e-3f || hybridDiff1 > 1.0e-3f)
    {
        std::cerr << "HybridEngine multichannel mismatch: diff0=" << hybridDiff0
                  << ", diff1=" << hybridDiff1 << std::endl;
        std::_Exit(1);
    }

    std::cout << "Multi-channel spectral and hybrid tests passed." << std::endl;
    std::_Exit(0);
}
