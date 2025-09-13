#include "HybridEngine.h"

//==============================================================================
void HybridEngine::process(juce::AudioBuffer<float>& buffer, int numSamples,
                           double bpm, int bars)
{
    temp.setSize(buffer.getNumChannels(), numSamples, false, false, true);
    temp.makeCopyOf(buffer);

    spectral.process(buffer, numSamples, bpm, bars);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < numSamples; ++i)
        {
            float g = temp.getSample(ch, i);
            float s = buffer.getSample(ch, i);
            buffer.setSample(ch, i, s * (1.0f - mix) + g * mix);
        }
}
