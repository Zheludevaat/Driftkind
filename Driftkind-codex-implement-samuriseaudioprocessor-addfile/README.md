# Driftkind

## Quick start (non‑technical users)

This section gives a high‑level overview of how to get up and running with **SAMU‑RISER** without wading through development details.  If you just want to install the plug‑in, load it in your DAW and start making loops, follow these simple steps:

1. **Download and run the installer**.  On macOS or Linux, open a terminal in the project folder and run:

   ```sh
   ./install.sh
   ```

   On Windows, double‑click `install.bat`.  These scripts will fetch JUCE (if necessary), configure and build the plug‑in with CMake, and copy the resulting `.vst3` (and `.component` on macOS) into your system’s plug‑ins folder.  The scripts may prompt you for your administrator password to install files into system directories.

2. **Launch your DAW and insert the plug‑in**.  SAMU‑RISER appears as an instrument plug‑in (synth) rather than an effect.  Create a new instrument track and load it just as you would any other virtual instrument.  On macOS you’ll see both VST3 and Audio Unit versions; on Windows and Linux only VST3 is currently supported.

3. **Import audio files**.  Drag and drop one or more of your own WAV/AIFF files onto the plug‑in window.  SAMU‑RISER analyses each file in the background and builds a “pool” of material from which it will spawn grains.  Imported files aren’t stored in presets, so you’ll need to re‑import them each time you start a new session.

4. **Choose your loop parameters**.  Use the on‑screen controls to set the tempo (`BPM`), length (`Bars`), key and mode (Major/Minor), and adjust creative parameters like `Density`, `Texture`, `Hybrid Mix` (when using the Hybrid engine), `Humanize`, `Stereo Width` and whether the limiter is active.  The plug‑in automatically resynchronises to your DAW’s tempo when the transport is running.

5. **Rebuild**.  Press the **Rebuild** button to generate a fresh loop from the analysed pool.  Changing the random seed will produce a different arrangement of grains.  Loops play back in real time so you can immediately audition the result.

6. **Export and save presets**.  When you’ve found a loop you like, click **Export** to write a 16‑bit WAV file to disk.  Use **Save** to store parameter settings (except for the imported audio pool) into an XML preset, and **Load** to recall them later.  Because presets exclude audio data, you can share them with collaborators without transferring large files.

That’s it!  For more detailed information on each parameter and the underlying architecture, read on.

## One-click Install

Run `./install.sh` on macOS or Linux, or `install.bat` on Windows. The script will:

1. Ensure CMake and JUCE are available (cloning JUCE into `deps/juce` if missing).
2. Configure and build the project via CMake.
3. Copy the resulting plugin into your system's VST/AU directory.

`./install.sh --help` provides more information.

## Usage

After building, load **SAMU-RISER** in your DAW. Drag audio files onto the plug-in window to add them to the pool. The main controls are:

| Parameter | Description |
|-----------|-------------|
| BPM       | Target tempo for the generated loop |
| Bars      | Length of the loop in bars |
| Key Note  | Musical key root (C..B) |
| Mode      | Major or Minor scale |
| Density   | Number of simultaneous grains |
| Texture   | Bias toward transient vs. sustained material |
| Flavor    | Selects Granular, Spectral, or Hybrid engine |
| Humanize  | Adds timing jitter to grain spawning |
| Seed      | Random seed for deterministic generation |

Press **Rebuild** to generate a new loop from the analysed pool. The rendered loop can play in real time or be exported.

SAMU-RISER is built as a synth plug‑in, so most hosts list it among their instruments.

Use **Export** to write the current N-bar loop to a WAV file. Presets can be saved or loaded via the **Save** and **Load** buttons, allowing loop configurations to be recalled across sessions.

### Parameters

The plug‑in exposes a number of parameters via the GUI and as DAW automatable controls:

| Parameter  | Range/Type                    | Meaning |
|-----------|-------------------------------|---------|
| **BPM**   | 20–300 BPM                    | Target tempo for the generated loop.  If the host provides a tempo via its play head, the internal BPM will follow the host. |
| **Bars**  | 1–128 integer                 | Length of the loop in bars.  A rebuild will produce a loop of this length. |
| **Key Note** | C..B                        | Root note of the key used for scale‑snapping of grains. |
| **Mode**  | Major / Minor                 | Scale mode used for pitch snapping. |
| **Seed**  | 0–1 000 000 integer          | Random seed for deterministic loop generation.  Changing this seed regenerates a different arrangement. |
| **Density** | 0–1 float                    | Controls how many grains are active simultaneously.  Lower values produce sparser textures; higher values produce denser clouds. |
| **Texture** | 0–1 float                    | Biases grain spawning toward onsets (transients) vs. random locations.  0 focuses on transients; 1 focuses on sustained material. |
| **Flavor** | Granular / Spectral / Hybrid | Selects which engine is used.  Granular uses grain synthesis, Spectral uses an FFT‑based resynthesis, and Hybrid blends the two. |
| **Hybrid Mix** | 0–1 float                 | When the Hybrid flavor is selected, this controls the blend between the spectral (0) and granular (1) outputs. |
| **Humanize** | On/Off                     | Adds random timing jitter to grain spawning for a looser feel. |
| **Spectral Cutoff** | 200–20 000 Hz float | Low‑pass cutoff applied in the spectral engine. |
| **Stereo Width** | 0–1 float              | Scales the stereo width of the output.  0 collapses to mono, 1 preserves the original side level. |
| **Limiter** | On/Off                      | Enables a simple limiter to prevent clipping. |
| **Limit Threshold** | –24…0 dB float      | Threshold for the limiter when enabled. |
| **Bypass Bus** | On/Off                  | Bypasses the EQ/limiter “bus” processing stage. |

> **Note:** Preset files save only parameter values.  The imported audio pool is not included, so after loading a preset you must re‑import your audio files.

### Export & presets

Use the **Export** button to write the current loop to a 16‑bit WAV file.  Support for 24‑bit and 32‑bit float export can be added by extending `exportLoopToFile()` (see `SamuRiserAudioProcessor::exportLoopToFile`).  **Save** and **Load** buttons serialize and restore the `AudioProcessorValueTreeState` to/from XML.

### Development & tests

This project uses JUCE 8 and requires CMake 3.15+ and a C++20 compiler.  To build manually:

```sh
cmake -B build -S . -DJUCE_DIR=/path/to/JUCE -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The unit tests live under `tests/` and are compiled into a separate executable.  After building, run `ctest --test-dir build` to execute all tests.  A CI workflow in `.github/workflows/build.yml` builds and tests the project on Windows, macOS and Linux.

### Rubber Band time‑stretching (optional)

If you have the [Rubber Band Library](https://breakfastquay.com/rubberband/) installed, you can enable high‑quality time‑stretching and formant‑preserving pitch shifting by setting `-DSAMURISE_USE_RUBBERBAND=ON` when configuring CMake.  On systems without Rubber Band, the plug‑in will fall back to a simpler FFT‑based approach.

### Standalone build

In addition to VST3 and AudioUnit formats, the CMake configuration now builds a **Standalone** version.  You can run this binary directly to test the synth outside of a DAW.

## Development

The project uses JUCE 8 and CMake. Parameters are managed via `AudioProcessorValueTreeState`, with state saved/restored automatically. Unit tests live under `tests/` and can be run with `ctest` after building.

Continuous integration runs on GitHub Actions for macOS, Windows and Linux. Each job invokes the one‑click installer, builds the plug‑in, runs the unit tests and packages a ZIP artifact.

The spectral engine preallocates its working buffers to avoid per-block heap churn and can optionally use the [Rubber Band Library](https://breakfastquay.com/rubberband/) for formant-preserving pitch shifts when compiled with `SAMURISE_USE_RUBBERBAND`.

The granular engine employs cubic interpolation with interpolated Hann envelopes to reduce aliasing and high-frequency roll-off when grains are pitched far from their original register.

Package signed binaries with `cpack` after setting `CODESIGN_IDENTITY` (macOS) or `SIGNTOOL_EXE` (Windows) in the environment. Pull requests should run `cmake -B build -S .` and `ctest --test-dir build` before submission.

## License

This project is released under the [MIT License](LICENSE).
