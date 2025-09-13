#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "../src/SamuRiserAudioProcessor.h"
#include "../src/GranularEngine.h"
#include "../src/SpectralEngine.h"
#include "../src/HybridEngine.h"
#include "../src/SammuMatcher.h"

struct AnalysisTest : juce::UnitTest {
    AnalysisTest() : juce::UnitTest("SourceFile Analysis", "Analysis") {}
    void runTest() override {
        beginTest("Analysis resets previous results");
        SourceFile sf; sf.audio.setSize(1, 1024); sf.sampleRate = 48000.0;
        for (int i = 0; i < sf.audio.getNumSamples(); ++i)
            sf.audio.setSample(0, i, i % 2 ? 0.5f : -0.5f);
        sf.analyze();
        auto onsetCount = (int)sf.onsets.size(); auto hist = sf.pitchHist;
        sf.analyze();
        expectEquals((int)sf.onsets.size(), onsetCount);
        expect(sf.pitchHist == hist);
    }
};

struct GranularEngineTest : juce::UnitTest {
    GranularEngineTest() : juce::UnitTest("Granular Engine", "DSP") {}
    void runTest() override {
        beginTest("snapToScale aligns to key");
        GranularEngine ge; ge.setKey(9, true);
        int note = ge.snapToScale(61);
        expect(note == 60 || note == 62);

        beginTest("process produces audio");
        ge.setSampleRate(48000.0); ge.setSeed(42); ge.setDensity(1.0f); ge.setTexture(0.0f);
        std::vector<std::shared_ptr<SourceFile>> pool;
        auto src = std::make_shared<SourceFile>();
        src->audio.setSize(1, 48000); src->sampleRate = 48000.0;
        for (int i = 0; i < src->audio.getNumSamples(); ++i)
            src->audio.setSample(0, i, std::sin(2.0 * juce::MathConstants<double>::pi * i / 100.0));
        pool.push_back(src); ge.setPool(&pool);
        juce::AudioBuffer<float> out; out.setSize(1, 512);
        ge.process(out, 512, 120.0, 1);
        bool nonZero = false;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (std::abs(out.getSample(0, i)) > 0.0f) { nonZero = true; break; }
        expect(nonZero);
    }
};

struct MatcherRendererTest : juce::UnitTest {
    MatcherRendererTest() : juce::UnitTest("Matcher and Renderer", "DSP") {}
    void runTest() override {
        beginTest("Matcher builds schedule");
        Segment a; a.length = 100; a.chroma = {1,0,0,0,0,0,0,0,0,0,0,0}; a.onsetDensity=0.5f; a.rms=0.5f; a.dominantPC=0;
        Segment b; b.length = 100; b.chroma = {0,1,0,0,0,0,0,0,0,0,0,0}; b.onsetDensity=0.5f; b.rms=0.5f; b.dominantPC=1;
        std::vector<std::vector<Segment>> pools = {{a},{b}};
        MatchSettings ms; ms.barsPerSeg = 1; ms.scale = ScaleMask::makeMajor(0);
        SegmentMatcher matcher; auto schedule = matcher.buildSchedule(pools, 2, ms);
        expectEquals((int)schedule.size(), 2);

        beginTest("Renderer outputs expected length");
        juce::AudioBuffer<float> dummy; dummy.setSize(1, 48000);
        Segment seg; seg.src = &dummy; seg.start = 0; seg.length = 48000; seg.srcSR = 48000.0;
        SegmentGluedRenderer renderer; renderer.setSampleRate(48000.0);
        std::vector<Segment> sched = {seg, seg};
        juce::AudioBuffer<float> loop; std::atomic<bool> cancel(false);
        renderer.render(sched, 120.0, 2, 1, loop, cancel);
        expectEquals(loop.getNumSamples(), 192000);
    }
};

struct EngineInteropTest : juce::UnitTest {
    EngineInteropTest() : juce::UnitTest("Engine Interop", "DSP") {}
    void runTest() override {
        beginTest("Spectral and Hybrid engines produce audio");
        SpectralEngine se; se.setSampleRate(48000.0); juce::AudioBuffer<float> buf; buf.setSize(1, 1024);
        for (int i = 0; i < buf.getNumSamples(); ++i) buf.setSample(0, i, std::sin(0.01f*(float)i));
        auto copy = buf; se.setPitchRatio(1.0f); se.process(buf, buf.getNumSamples(), 120.0, 1);
        bool specNonZero=false; double err=0.0; for(int i=0;i<buf.getNumSamples();++i){float v=buf.getSample(0,i); specNonZero|=(v!=0.0f); float d=v-copy.getSample(0,i); err+=d*d;}
        expect(specNonZero); expectLessThan(std::sqrt(err / buf.getNumSamples()), 0.01);

        HybridEngine he; he.setSampleRate(48000.0);
        juce::AudioBuffer<float> hbuf; hbuf.setSize(1,512); for(int i=0;i<hbuf.getNumSamples();++i) hbuf.setSample(0,i,std::sin(0.02f*(float)i));
        he.process(hbuf, hbuf.getNumSamples(), 120.0, 1);
        bool hybNonZero=false; for(int i=0;i<hbuf.getNumSamples();++i) if(hbuf.getSample(0,i)!=0.0f){hybNonZero=true;break;}
        expect(hybNonZero);
    }
};

struct HybridMixTest : juce::UnitTest {
    HybridMixTest() : juce::UnitTest("Hybrid mix", "DSP") {}
    void runTest() override {
        beginTest("Hybrid mix balances spectral and granular outputs");
        HybridEngine he; he.setSampleRate(48000.0);

        juce::AudioBuffer<float> input; input.setSize(1, 512);
        for (int i = 0; i < input.getNumSamples(); ++i)
            input.setSample(0, i, std::sin(0.01f * (float)i));

        // reference spectral-only output
        juce::AudioBuffer<float> spectralOnly = input;
        SpectralEngine se; se.setSampleRate(48000.0); se.setPitchRatio(1.0f);
        se.process(spectralOnly, spectralOnly.getNumSamples(), 120.0, 1);

        he.setMix(0.0f);
        juce::AudioBuffer<float> buf = input;
        he.process(buf, buf.getNumSamples(), 120.0, 1);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            expectWithinAbsoluteError(buf.getSample(0, i), spectralOnly.getSample(0, i), 1e-5f);

        he.setMix(1.0f);
        buf = input;
        he.process(buf, buf.getNumSamples(), 120.0, 1);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            expectWithinAbsoluteError(buf.getSample(0, i), input.getSample(0, i), 1e-5f);

        he.setMix(0.5f);
        buf = input;
        he.process(buf, buf.getNumSamples(), 120.0, 1);
        float specSample = spectralOnly.getSample(0, 0);
        float drySample = input.getSample(0, 0);
        expectWithinAbsoluteError(buf.getSample(0, 0), 0.5f * specSample + 0.5f * drySample, 1e-4f);
    }
};

struct ResampleTest : juce::UnitTest {
    ResampleTest() : juce::UnitTest("Processor resampling", "Processor") {}
    void runTest() override {
        beginTest("addFile resamples sources to host rate");
        SamuRiserAudioProcessor proc; proc.prepareToPlay(44100.0, 512);
        juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sr_test.wav");
        juce::WavAudioFormat wf; {
            std::unique_ptr<juce::FileOutputStream> os(tmp.createOutputStream());
            auto writer = std::unique_ptr<juce::AudioFormatWriter>(wf.createWriterFor(os.release(), 48000, 1, 16, {}, 0));
            juce::AudioBuffer<float> tb; tb.setSize(1,48000); tb.clear(); writer->writeFromAudioSampleBuffer(tb,0,tb.getNumSamples());
        }
        auto res = proc.addFile(tmp); tmp.deleteFile();
        expect(res.wasOk());
        expectEquals(proc.getPoolSize(), 1);
    }
};

struct EditorTest : juce::UnitTest {
    EditorTest() : juce::UnitTest("Editor", "UI") {}
    void runTest() override {
        beginTest("Processor creates editor");
        SamuRiserAudioProcessor proc; auto editor = std::unique_ptr<juce::AudioProcessorEditor>(proc.createEditor());
        expect(editor != nullptr);
    }
};

struct PresetTest : juce::UnitTest {
    PresetTest() : juce::UnitTest("Preset save/load", "Processor") {}
    void runTest() override {
        beginTest("Parameters round-trip through preset");
        SamuRiserAudioProcessor proc; proc.prepareToPlay(44100.0, 512);
        auto* bpmParam = proc.getAPVTS().getRawParameterValue("bpm");
        *const_cast<float*>(bpmParam) = 123.0f;
        juce::File preset = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("preset.xml");
        proc.savePreset(preset);
        *const_cast<float*>(bpmParam) = 99.0f;
        proc.loadPreset(preset);
        preset.deleteFile();
        expectWithinAbsoluteError(*bpmParam, 123.0f, 0.001f);
    }
};

struct ExportErrorTest : juce::UnitTest {
    ExportErrorTest() : juce::UnitTest("Export errors", "Processor") {}
    void runTest() override {
        beginTest("Non-WAV export fails");
        SamuRiserAudioProcessor proc; proc.prepareToPlay(44100.0, 512);
        juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("out.txt");
        auto r = proc.exportLoopToFile(tmp);
        expect(r.failed());
    }
};

static AnalysisTest analysisTest;
static GranularEngineTest granularTest;
static MatcherRendererTest matcherRendererTest;
static EngineInteropTest engineInteropTest;
static HybridMixTest hybridMixTest;
static ResampleTest resampleTest;
static EditorTest editorTest;
static PresetTest presetTest;
static ExportErrorTest exportTest;
struct RebuildCancelTest : juce::UnitTest {
    RebuildCancelTest() : juce::UnitTest("Rebuild cancel", "Processor") {}
    void runTest() override {
        beginTest("Rebuild can be cancelled");
        SamuRiserAudioProcessor proc; proc.prepareToPlay(44100.0, 512);
        juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cancel.wav");
        juce::WavAudioFormat wf; {
            std::unique_ptr<juce::FileOutputStream> os(tmp.createOutputStream());
            auto writer = std::unique_ptr<juce::AudioFormatWriter>(wf.createWriterFor(os.release(), 44100, 1, 16, {}, 0));
            juce::AudioBuffer<float> tb; tb.setSize(1,44100); tb.clear(); writer->writeFromAudioSampleBuffer(tb,0,tb.getNumSamples());
        }
        proc.addFile(tmp); tmp.deleteFile();
        proc.rebuildLoopAsync(120.0, 4, 2, true, 9);
        proc.cancelRebuild();
        expect(!proc.isRebuilding());
    }
};
static RebuildCancelTest cancelTest;

int main() {
    juce::UnitTestRunner runner;
    runner.runAllTests();
    return runner.getFailures() > 0 ? 1 : 0;
}

