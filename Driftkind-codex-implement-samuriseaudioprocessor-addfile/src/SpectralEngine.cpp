#include "SpectralEngine.h"

void SpectralEngine::setSampleRate(double sr)
{
    sampleRate = sr;
#ifdef SAMURISE_USE_RUBBERBAND
    stretcher.reset(new RubberBand::RubberBandStretcher(
        sr, 1, RubberBand::RubberBandStretcher::OptionProcessRealTime |
            RubberBand::RubberBandStretcher::OptionPitchHighConsistency));
#endif
}

//==============================================================================
void SpectralEngine::process(juce::AudioBuffer<float>& buffer, int numSamples,
                             double, int)
{
#ifdef SAMURISE_USE_RUBBERBAND
    if (stretcher)
    {
        stretcher->setPitchScale(pitchRatio);
        float* chans[2] = {
            buffer.getWritePointer(0),
            buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : buffer.getWritePointer(0)
        };
        stretcher->process(chans, (size_t)numSamples, false);
        stretcher->retrieve(chans, (size_t)numSamples);
        return;
    }
#endif

    const int fftSize = fft.getSize();
    const int hopSize = fftSize / 4; // 75% overlap for smoother OLA

    if ((int)fftBuffer.size() < 2 * fftSize)
        fftBuffer.resize(2 * fftSize, 0.0f);
    if ((int)dest.size() < 2 * fftSize)
        dest.resize(2 * fftSize, 0.0f);
    if (accum.getNumChannels() < buffer.getNumChannels() ||
        accum.getNumSamples() < numSamples + fftSize)
        accum.setSize(buffer.getNumChannels(), numSamples + fftSize, false, false, true);
    accum.clear();

    const int bins = fftSize / 2;
    if ((int)prevInPhase.size() < buffer.getNumChannels())
    {
        prevInPhase.resize(buffer.getNumChannels());
        prevOutPhase.resize(buffer.getNumChannels());
        prevMag.resize(buffer.getNumChannels());
    }

    const float freqPerBin = (float)sampleRate / (float)fftSize;
    const float expPhase = juce::MathConstants<float>::twoPi * hopSize / (float)fftSize;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto& inPhase = prevInPhase[ch];
        auto& outPhase = prevOutPhase[ch];
        auto& magHistory = prevMag[ch];
        if ((int)inPhase.size() < bins + 1)
        {
            inPhase.assign(bins + 1, 0.0f);
            outPhase.assign(bins + 1, 0.0f);
            magHistory.assign(bins + 1, 0.0f);
        }

        accum.clear(ch, 0, accum.getNumSamples());

        const float* read = buffer.getReadPointer(ch);
        for (int pos = 0; pos + fftSize <= numSamples; pos += hopSize)
        {
            std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
            std::memcpy(fftBuffer.data(), read + pos, fftSize * sizeof(float));

            window.multiplyWithWindowingTable(fftBuffer.data(), fftSize);
            fft.performRealOnlyForwardTransform(fftBuffer.data());

            std::fill(dest.begin(), dest.end(), 0.0f);
            for (int i = 0; i < bins; ++i)
            {
                float freq = i * freqPerBin;
                if (freq > cutoff) continue;

                float re = fftBuffer[2 * i];
                float im = fftBuffer[2 * i + 1];
                float m = std::sqrt(re * re + im * im);
                float phase = std::atan2(im, re);

                magHistory[i] = 0.7f * magHistory[i] + 0.3f * m;

                float delta = phase - inPhase[i];
                inPhase[i] = phase;
                delta -= expPhase * i;
                delta = std::fmod(delta + juce::MathConstants<float>::pi,
                                  juce::MathConstants<float>::twoPi) -
                        juce::MathConstants<float>::pi;
                float trueFreq = (i + delta / expPhase) * pitchRatio;
                int base = (int)trueFreq;
                float frac = trueFreq - (float)base;
                if (base < bins)
                {
                    float mag = magHistory[i];
                    float phase0 = outPhase[base] + expPhase * trueFreq;
                    outPhase[base] = phase0;
                    float cos0 = std::cos(phase0);
                    float sin0 = std::sin(phase0);
                    dest[2 * base] += mag * (1.0f - frac) * cos0;
                    dest[2 * base + 1] += mag * (1.0f - frac) * sin0;

                    if (base + 1 < bins)
                    {
                        float phase1 = outPhase[base + 1] + expPhase * (trueFreq + 1.0f);
                        outPhase[base + 1] = phase1;
                        float cos1 = std::cos(phase1);
                        float sin1 = std::sin(phase1);
                        dest[2 * (base + 1)] += mag * frac * cos1;
                        dest[2 * (base + 1) + 1] += mag * frac * sin1;
                    }
                }
            }

            fft.performRealOnlyInverseTransform(dest.data());
            window.multiplyWithWindowingTable(dest.data(), fftSize);

            const float scale = 1.0f / (float)fftSize;
            for (int i = 0; i < fftSize; ++i)
                accum.addSample(ch, pos + i, dest[i] * scale);
        }

        float* write = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            write[i] = accum.getSample(ch, i);
    }
}
