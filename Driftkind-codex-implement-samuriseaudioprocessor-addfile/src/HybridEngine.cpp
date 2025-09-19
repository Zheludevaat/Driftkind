#include "HybridEngine.h"

//==============================================================================
void HybridEngine::process(juce::AudioBuffer<float>& buffer, int numSamples,
                           double bpm, int bars)
{
    // Only resize the temporary buffer if the dimensions have changed.  JUCE will
    // avoid reallocating if the size is the same, but checking first reduces
    // overhead on the audio thread.
    if (temp.getNumChannels() != buffer.getNumChannels() || temp.getNumSamples() != numSamples)
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
