#include "SamuRiserAudioProcessor.h"
#include "SamuRiserAudioEditor.h"
#include <thread>

//==============================================================================
SamuRiserAudioProcessor::SamuRiserAudioProcessor()
    : apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    formatManager.registerBasicFormats();
}

SamuRiserAudioProcessor::~SamuRiserAudioProcessor()
{
    cancelRebuild();
}

//==============================================================================
// Analysis of a single audio file. This routine intentionally favours clarity
// over efficiency as it is intended only as a lightweight, offline analysis
// used when files are added to the pool.
void SourceFile::analyze()
{
    // Reset previous analysis results in case this file is re-analysed
    onsets.clear();
    pitchHist.fill(0);
    rms = 0.0f;

    const int numChannels = audio.getNumChannels();
    const int numSamples  = audio.getNumSamples();
    if (numChannels == 0 || numSamples == 0)
        return;

    // Mix the input down to mono for analysis.
    std::vector<float> mono (numSamples, 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* src = audio.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            mono[i] += src[i];
    }
    const float invCh = 1.0f / static_cast<float> (numChannels);
    for (auto& s : mono)
        s *= invCh;

    //=========================================================================
    // RMS calculation
    double sumSq = 0.0;
    for (float s : mono)
        sumSq += static_cast<double> (s) * static_cast<double> (s);
    rms = std::sqrt (sumSq / static_cast<double> (numSamples));

    //=========================================================================
    // Simple onset detection. We apply a naïve first order high‑pass filter and
    // look for sharp increases in short‑term energy. The window is derived from
    // the file's sample rate so that timing remains consistent across sources.
    const int window = juce::jmax(1, (int)std::round(0.01 * sampleRate));
    std::vector<float> hp (numSamples, 0.0f);
    float prev = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono[i];
        hp[i] = x - prev; // differentiator acts as a crude high‑pass filter
        prev = x;
    }

    std::vector<float> energy (numSamples, 0.0f);
    double env = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float e = hp[i] * hp[i];
        env += e;
        if (i >= window)
            env -= hp[i - window] * hp[i - window];
        energy[i] = static_cast<float> (env / window);
    }

    // Pick simple peaks that cross an energy threshold.
    const float threshold = rms * rms * 4.0f; // scale by RMS to be level independent
    for (int i = 1; i < numSamples - 1; ++i)
    {
        if (energy[i] > threshold && energy[i] > energy[i - 1] && energy[i] > energy[i + 1])
            onsets.push_back (i);
    }

    //=========================================================================
    // Pitch class histogram. We analyse successive FFT frames and vote for the
    // dominant bin's pitch class. This gives a rough idea of the file's overall
    // harmonic content and is used as a guideline for key‑aware granulation.
    const int fftOrder = 11; // 2048‑point FFT
    const int fftSize  = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    juce::HeapBlock<float> fftData (2 * fftSize);
    juce::dsp::WindowingFunction<float> windowFn (fftSize, juce::dsp::WindowingFunction<float>::hann);

    for (int pos = 0; pos + fftSize < numSamples; pos += fftSize / 2)
    {
        float* re = fftData.get();
        float* im = re + fftSize;
        std::fill (re, re + fftSize, 0.0f);
        std::fill (im, im + fftSize, 0.0f);

        for (int i = 0; i < fftSize; ++i)
            re[i] = mono[pos + i];

        windowFn.multiplyWithWindowingTable (re, fftSize);
        fft.perform (re, im, false);

        int maxIndex = 0;
        float maxMag = 0.0f;
        for (int i = 0; i < fftSize / 2; ++i)
        {
            const float mag = std::sqrt (re[i] * re[i] + im[i] * im[i]);
            if (mag > maxMag)
            {
                maxMag = mag;
                maxIndex = i;
            }
        }

        if (maxMag > 1.0e-6f)
        {
            const float freq = (static_cast<float> (maxIndex) * static_cast<float> (sampleRate)) / static_cast<float> (fftSize);
            const float midi = 69.0f + 12.0f * std::log2 (freq / 440.0f);
            const int   pc   = ((static_cast<int> (std::round (midi)) % 12) + 12) % 12;
            ++pitchHist[(size_t) pc];
        }
    }
}

//==============================================================================
juce::Result SamuRiserAudioProcessor::addFile (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        auto msg = "File does not exist: " + file.getFullPathName();
        juce::Logger::writeToLog(msg);
        return juce::Result::fail(msg);
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        auto msg = "Unsupported or unreadable file: " + file.getFullPathName();
        juce::Logger::writeToLog(msg);
        return juce::Result::fail(msg);
    }

    auto src = std::make_shared<SourceFile>();

    juce::AudioBuffer<float> temp;
    temp.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&temp, 0, (int) reader->lengthInSamples, 0, true, true);

    double hostSR = getSampleRate();
    if (hostSR > 0.0 && std::abs (reader->sampleRate - hostSR) > 1.0)
    {
        const double ratio = reader->sampleRate / hostSR;
        const int destSamples = (int) std::ceil ((double) temp.getNumSamples() / ratio);
        src->audio.setSize (temp.getNumChannels(), destSamples);

        for (int ch = 0; ch < temp.getNumChannels(); ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, temp.getReadPointer (ch), src->audio.getWritePointer (ch), destSamples);
        }

        src->sampleRate = hostSR;
    }
    else
    {
        src->audio = temp;
        src->sampleRate = reader->sampleRate;
    }

    src->analyze();

    auto newPool = std::make_shared<Pool>(*pool.load());
    newPool->emplace_back(src);
    pool.store(newPool);
    engine.setPool(newPool);
    return juce::Result::ok();
}

//==============================================================================
void SamuRiserAudioProcessor::prepareToPlay (double sampleRate, int)
{
    analyzer = std::make_unique<SegmentAnalyzer>();
    renderer = std::make_unique<SegmentGluedRenderer>();
    renderer->setSampleRate(sampleRate);
    engine.setSampleRate(sampleRate);
    engine.setPool(pool.load());
    spectralEngine.setSampleRate(sampleRate);
    hybridEngine.setSampleRate(sampleRate);
    specCutoffSmoothed.reset(sampleRate, 0.05);
    hybridMixSmoothed.reset(sampleRate, 0.05);
    specCutoffSmoothed.setCurrentAndTargetValue(*apvts.getRawParameterValue("specCutoff"));
    hybridMixSmoothed.setCurrentAndTargetValue(*apvts.getRawParameterValue("hybridMix"));
    playHead = 0;

    juce::dsp::ProcessSpec spec { sampleRate, 512, (juce::uint32)getTotalNumOutputChannels() };
    tiltEq.prepare(spec);
    *tiltEq.get<0>().state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate, 1000.0, 0.707f,
                                                                               juce::Decibels::decibelsToGain(-1.0f));
    *tiltEq.get<1>().state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, 1000.0, 0.707f,
                                                                               juce::Decibels::decibelsToGain(1.0f));
    limiter.prepare(spec);
}

//==============================================================================
void SamuRiserAudioProcessor::rebuildLoop(double bpm, int totalBars, int barsPerSeg,
                                         bool minorMode, int keyRoot)
{
    if (analyzer == nullptr || renderer == nullptr)
        return;

      MatchSettings ms;
      ms.barsPerSeg = barsPerSeg;
      ms.scale = minorMode ? ScaleMask::makeMinor(keyRoot)
                           : ScaleMask::makeMajor(keyRoot);

      auto poolCopy = *pool.load();

      std::vector<std::vector<Segment>> pools;
      for (size_t i = 0; i < poolCopy.size(); ++i)
      {
          if (cancelRebuildFlag.load()) return;
          auto& src = poolCopy[i];
          std::vector<Segment> segs;
          analyzer->sliceIntoSegments(src->audio, src->sampleRate, bpm, barsPerSeg, segs);
          pools.push_back(std::move(segs));
          rebuildProgress = 0.5f * (float)(i + 1) / (float)poolCopy.size();
      }

    if (cancelRebuildFlag.load()) return;

    auto schedule = matcher.buildSchedule(pools, totalBars, ms);
    rebuildProgress = 0.75f;

    if (cancelRebuildFlag.load()) return;

    auto newLoop = std::make_shared<juce::AudioBuffer<float>>();
    newLoop->setSize(2, 1);
    renderer->render(schedule, bpm, totalBars, barsPerSeg, *newLoop, cancelRebuildFlag);
    if (cancelRebuildFlag.load()) return;
    outputLoop.store(newLoop);
    playHead = 0;
    rebuildProgress = 1.0f;
}

void SamuRiserAudioProcessor::cancelRebuild()
{
    cancelRebuildFlag = true;
    if (rebuildThread.joinable())
        rebuildThread.join();
    cancelRebuildFlag = false;
    rebuilding = false;
    rebuildProgress = 0.0f;
}

void SamuRiserAudioProcessor::rebuildLoopAsync(double bpm, int totalBars, int barsPerSeg,
                                              bool minorMode, int keyRoot)
{
    cancelRebuild();

    rebuilding = true;
    rebuildProgress = 0.0f;
    cancelRebuildFlag = false;

    rebuildThread = std::thread([this, bpm, totalBars, barsPerSeg, minorMode, keyRoot]
    {
        rebuildLoop(bpm, totalBars, barsPerSeg, minorMode, keyRoot);
        rebuilding = false;
    });
}

juce::Result SamuRiserAudioProcessor::exportLoopToFile(const juce::File& file)
{
    if (! file.hasFileExtension(".wav"))
        return juce::Result::fail("Only .wav export supported");

    auto loop = outputLoop.load();
    if (loop->getNumSamples() == 0)
        return juce::Result::fail("No loop to export");
    juce::AudioBuffer<float> buffer = *loop; // copy rendered loop

      juce::WavAudioFormat format;
      std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
      if (stream == nullptr)
          return juce::Result::fail("Could not open file for writing");

      std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),
                                                                           getSampleRate(),
                                                                           (unsigned int)buffer.getNumChannels(),
                                                                           16,
                                                                           {}, 0));
      if (writer == nullptr)
          return juce::Result::fail("Failed to create WAV writer");

      writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
      return juce::Result::ok();
  }

//==============================================================================
void SamuRiserAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&)
{
    buffer.clear();

    float bpmParam     = *apvts.getRawParameterValue("bpm");
    const int bars     = (int)*apvts.getRawParameterValue("bars");
    const int keyNote  = (int)*apvts.getRawParameterValue("keyNote");
    const bool isMinor = ((int)*apvts.getRawParameterValue("keyMode")) == 1;
    const int seed     = (int)*apvts.getRawParameterValue("seed");
    const float density = *apvts.getRawParameterValue("density");
    const float texture = *apvts.getRawParameterValue("texture");
    const bool humanize = (*apvts.getRawParameterValue("humanize")) > 0.5f;
    const int flavor = (int)*apvts.getRawParameterValue("flavor");
    const float specCutoff = *apvts.getRawParameterValue("specCutoff");
    const float hybridMix = *apvts.getRawParameterValue("hybridMix");
    specCutoffSmoothed.setTargetValue(specCutoff);
    hybridMixSmoothed.setTargetValue(hybridMix);

    if (auto* ph = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (ph->getCurrentPosition(info) && info.bpm > 0.0)
            bpmParam = (float)info.bpm;
    }

      if (seed != lastSeed)
      {
          engine.setSeed((uint32_t)seed);
          lastSeed = seed;
      }
    engine.setKey(keyNote, isMinor);
    engine.setDensity(density);
    engine.setTexture(texture);
    engine.setHumanize(humanize);

    spectralEngine.setCutoff(specCutoffSmoothed.getNextValue());
    hybridEngine.setMix(hybridMixSmoothed.getNextValue());

    // Determine dominant pitch class across the pool for spectral shifting
    float pitchRatio = 1.0f;
    {
        auto poolCopy = pool.load();
        int counts[12]{};
        for (const auto& src : *poolCopy)
            for (int i = 0; i < 12; ++i)
                counts[i] += src->pitchHist[i];
        int dominant = -1, maxCount = 0;
        for (int i = 0; i < 12; ++i)
            if (counts[i] > maxCount) { maxCount = counts[i]; dominant = i; }
        if (maxCount > 0)
        {
            int diff = keyNote - dominant;
            while (diff > 6) diff -= 12;
            while (diff < -6) diff += 12;
            pitchRatio = std::pow(2.0f, diff / 12.0f);
        }
    }
    spectralEngine.setPitchRatio(pitchRatio);
    hybridEngine.setPitchRatio(pitchRatio);

    switch (flavor)
    {
        case 1:
        {
            auto loop = outputLoop.load();
            if (loop && loop->getNumSamples() > 0)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int n = 0; n < buffer.getNumSamples(); ++n)
                    {
                        const int p = (playHead + n) % loop->getNumSamples();
                        const float v = loop->getSample(juce::jmin(ch, loop->getNumChannels()-1), p);
                        buffer.setSample(ch, n, v);
                    }
                playHead = (playHead + buffer.getNumSamples()) % juce::jmax(1, loop->getNumSamples());
            }
            else
            {
                engine.process(buffer, buffer.getNumSamples(), bpmParam, bars);
            }
            spectralEngine.process(buffer, buffer.getNumSamples(), bpmParam, bars);
            break;
        }
        case 2:
        {
            auto loop = outputLoop.load();
            if (loop && loop->getNumSamples() > 0)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int n = 0; n < buffer.getNumSamples(); ++n)
                    {
                        const int p = (playHead + n) % loop->getNumSamples();
                        const float v = loop->getSample(juce::jmin(ch, loop->getNumChannels()-1), p);
                        buffer.setSample(ch, n, v);
                    }
                playHead = (playHead + buffer.getNumSamples()) % juce::jmax(1, loop->getNumSamples());
            }
            else
            {
                engine.process(buffer, buffer.getNumSamples(), bpmParam, bars);
            }
            hybridEngine.process(buffer, buffer.getNumSamples(), bpmParam, bars);
            break;
        }
        default:
        {
            auto loop = outputLoop.load();
            if (loop && loop->getNumSamples() > 0)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int n = 0; n < buffer.getNumSamples(); ++n)
                    {
                        const int p = (playHead + n) % loop->getNumSamples();
                        const float v = loop->getSample(juce::jmin(ch, loop->getNumChannels()-1), p);
                        buffer.setSample(ch, n, v);
                    }
                playHead = (playHead + buffer.getNumSamples()) % juce::jmax(1, loop->getNumSamples());
            }
            else
            {
                engine.process(buffer, buffer.getNumSamples(), bpmParam, bars);
            }
            break;
        }
    }

    const bool bypassBus = (*apvts.getRawParameterValue("busBypass")) > 0.5f;
    if (! bypassBus)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        tiltEq.process(ctx);

        float widthCap = *apvts.getRawParameterValue("width");
        if (buffer.getNumChannels() >= 2)
        {
            auto* left = buffer.getWritePointer(0);
            auto* right = buffer.getWritePointer(1);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float mid = 0.5f * (left[i] + right[i]);
                float side = 0.5f * (left[i] - right[i]);
                side = juce::jlimit(-widthCap, widthCap, side);
                left[i] = mid + side;
                right[i] = mid - side;
            }
        }

        if ((*apvts.getRawParameterValue("limiter")) > 0.5f)
        {
            limiter.setThreshold(*apvts.getRawParameterValue("limitThresh"));
            limiter.process(ctx);
        }
    }

    const float ceiling = juce::Decibels::decibelsToGain(-1.0f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = juce::jlimit(-ceiling, ceiling, data[i]);
    }

    // compute RMS level for UI feedback
    double sum = 0.0;
    const int total = buffer.getNumChannels() * buffer.getNumSamples();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float v = data[i];
            sum += (double)v * (double)v;
        }
    }
    outputLevel = (float)std::sqrt(sum / std::max(1, total));
}

juce::Result SamuRiserAudioProcessor::savePreset(const juce::File& file)
{
    if (auto xml = apvts.copyState().createXml())
    {
        if (xml->writeTo(file))
            return juce::Result::ok();
    }
    return juce::Result::fail("Failed to save preset");
}

juce::Result SamuRiserAudioProcessor::loadPreset(const juce::File& file)
{
    juce::XmlDocument doc(file);
    std::unique_ptr<juce::XmlElement> xml(doc.getDocumentElement());
    if (xml == nullptr)
        return juce::Result::fail("Invalid preset file");

    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    return juce::Result::ok();
}

juce::AudioProcessorEditor* SamuRiserAudioProcessor::createEditor()
{
    return new SamuRiserAudioEditor(*this, apvts);
}

juce::AudioProcessorValueTreeState::ParameterLayout SamuRiserAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;
    params.emplace_back(std::make_unique<AudioParameterFloat>("bpm", "BPM", NormalisableRange<float>(20.f, 300.f, 0.01f), 140.f));
    params.emplace_back(std::make_unique<AudioParameterInt>("bars", "Bars", 1, 128, 16));
    params.emplace_back(std::make_unique<AudioParameterChoice>("keyNote", "Key Note", StringArray{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}, 9));
    params.emplace_back(std::make_unique<AudioParameterChoice>("keyMode", "Mode", StringArray{"Major","Minor"}, 1));
    params.emplace_back(std::make_unique<AudioParameterInt>("seed", "Seed", 0, 1'000'000, 42));
    params.emplace_back(std::make_unique<AudioParameterFloat>("density", "Density", NormalisableRange<float>(0.f,1.f,0.001f), 0.6f));
    params.emplace_back(std::make_unique<AudioParameterFloat>("texture", "Texture", NormalisableRange<float>(0.f,1.f,0.001f), 0.5f));
    params.emplace_back(std::make_unique<AudioParameterChoice>("flavor", "Flavor", StringArray{"Granular","Spectral","Hybrid"}, 0));
    params.emplace_back(std::make_unique<AudioParameterFloat>("hybridMix", "Hybrid Mix", NormalisableRange<float>(0.f,1.f,0.001f), 0.5f));
    params.emplace_back(std::make_unique<AudioParameterBool>("humanize", "Humanize", false));
    params.emplace_back(std::make_unique<AudioParameterFloat>("specCutoff", "Spectral Cutoff",
                        NormalisableRange<float>(200.f, 20000.f, 1.f, 0.5f), 5000.f));
    params.emplace_back(std::make_unique<AudioParameterFloat>("width", "Stereo Width", NormalisableRange<float>(0.f,1.f,0.001f), 0.9f));
    params.emplace_back(std::make_unique<AudioParameterBool>("limiter", "Limiter", true));
    params.emplace_back(std::make_unique<AudioParameterFloat>("limitThresh", "Limit Threshold", NormalisableRange<float>(-24.f,0.f,0.1f), -1.0f));
    params.emplace_back(std::make_unique<AudioParameterBool>("busBypass", "Bypass Bus", false));
    return { params.begin(), params.end() };
}

void SamuRiserAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState())
    {
        juce::MemoryOutputStream stream(destData, false);
        state.writeToStream(stream);
    }
}

void SamuRiserAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ValueTree tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid())
        apvts.replaceState(tree);
}

