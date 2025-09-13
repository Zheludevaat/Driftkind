# Driftkind

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

## Development

The project uses JUCE 8 and CMake. Parameters are managed via `AudioProcessorValueTreeState`, with state saved/restored automatically. Unit tests live under `tests/` and can be run with `ctest` after building.

Continuous integration runs on GitHub Actions for macOS, Windows and Linux. Each job invokes the one‑click installer, builds the plug‑in, runs the unit tests and packages a ZIP artifact.

The spectral engine preallocates its working buffers to avoid per-block heap churn and can optionally use the [Rubber Band Library](https://breakfastquay.com/rubberband/) for formant-preserving pitch shifts when compiled with `SAMURISE_USE_RUBBERBAND`.

The granular engine employs cubic interpolation with interpolated Hann envelopes to reduce aliasing and high-frequency roll-off when grains are pitched far from their original register.

Package signed binaries with `cpack` after setting `CODESIGN_IDENTITY` (macOS) or `SIGNTOOL_EXE` (Windows) in the environment. Pull requests should run `cmake -B build -S .` and `ctest --test-dir build` before submission.

## License

This project is released under the [MIT License](LICENSE).
