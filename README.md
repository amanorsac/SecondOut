# SecondOut

A master-bus utility plugin (VST3 + Standalone on Windows; VST3, AU + Standalone on
macOS) that duplicates the DAW's audio to a second, independently-selected physical
output device — without touching the DAW's own audio engine or device. Typical use:
DAW stays on its main interface for monitoring while SecondOut feeds program audio to
an interface connected to an OBS switcher.

On multi-output interfaces (Focusrite 18i20, Behringer UMC1820, etc.) each plugin
instance picks its own stereo output pair, so several instances can feed different
outputs of the same interface at once.

## How it works

```
DAW audio thread ──processBlock()──► lock-free SPSC ring buffer (~400 ms window)
                                            │
WASAPI (shared mode) callback ──► Catmull-Rom resampler (adjustable ratio)
                                            │
                                     Device B output
```

Two independent clocks are involved (the DAW's driver and Device B's WASAPI stream).
A servo-style drift corrector watches the ring buffer's fill level and nudges the
resampling ratio by a few PPM at a time (slew-limited, clamped) so the buffer stays
centred at ~50 % without audible pitch movement. The same resampler also bridges
outright rate mismatches (e.g. 48 kHz DAW → 44.1 kHz device).

Behavioral notes:

- **Prefill:** after (re)starting a device, output stays silent until the buffer
  reaches 50 % fill, which centres the servo target automatically.
- **Underrun:** pads with silence and re-enters prefill — no clicks, no crash.
- **Device disappears mid-session:** status drops to *disconnected*; the DAW is never
  affected. Reselect the device from the dropdown when it returns.
- **Overfill (bypass toggled, consumer stalled):** backlog beyond the effective
  window is dropped consumer-side so latency stays bounded (~200 ms nominal).
- Device B runs in **shared mode** (WASAPI on Windows, CoreAudio on macOS) — reliability
  over latency, appropriate for a program feed. On Windows an ASIO device can be chosen
  explicitly instead; ASIO is exclusive, so never pick the interface the DAW itself uses.

## Layout

| Path | Purpose |
|---|---|
| `Source/RingBuffer.h` | Lock-free SPSC ring buffer (pure C++, no JUCE) |
| `Source/Resampler.h` | Pull-based stereo Catmull-Rom resampler (pure C++) |
| `Source/DriftCorrector.h` | Fill-level servo → PPM ratio trim (pure C++) |
| `Source/SecondaryDeviceManager.*` | Device B lifecycle (WASAPI/ASIO or CoreAudio) + consumer callback |
| `Source/License/` | License activation client; signed-proof verification (BCrypt / SecKey) |
| `Source/PluginProcessor.*` | Processor — passthrough + tap, gated on a valid license |
| `Source/PluginEditor.*` | Device dropdown, output-pair picker, status LED, fill meter, bypass |
| `Tests/` | Unit tests for the three pure-C++ components |

## Building

JUCE is a git submodule — clone with `--recurse-submodules` (or run
`git submodule update --init` afterwards). CMake 3.22+ on both platforms.

**Windows** (Visual Studio 2022+ with the C++ workload):

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target SecondOut_VST3 SecondOut_Standalone
```

ASIO support needs Steinberg's SDK, which can't be redistributed: download it from
https://www.steinberg.net/asiosdk and unzip so that `ThirdParty/ASIOSDK/common/iasiodrv.h`
exists. Without it the build is WASAPI-only (CMake prints a warning). The installer is
built with Inno Setup from `installer/SecondOut.iss`.

**macOS** (Xcode 14+):

```bash
cmake -S . -B build -G Xcode
cmake --build build --config Release --target SecondOut_VST3 SecondOut_AU SecondOut_Standalone
```

Produces a universal (Apple Silicon + Intel) binary, minimum macOS 10.13. Bundles land
under `build/SecondOut_artefacts/Release/{VST3,AU,Standalone}/`. To try them locally,
copy `SecondOut.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `SecondOut.component`
to `~/Library/Audio/Plug-Ins/Components/`.

**CI**: `.github/workflows/build.yml` builds both platforms on every push. The macOS job
produces a `.pkg` installer as a workflow artifact; if the repository secrets
`MACOS_CERTIFICATE` (base64 `.p12`), `MACOS_CERTIFICATE_PASSWORD`, `APPLE_ID`,
`APPLE_TEAM_ID` and `APPLE_APP_PASSWORD` are set it is also code-signed and notarised,
otherwise it's uploaded unsigned (fine for your own Mac; Gatekeeper will block it on
anyone else's).

Run the unit tests on either platform:

```bash
cmake --build build --config Release --target SecondOutTests
ctest --test-dir build -C Release --output-on-failure
```

## Releasing

`.github/workflows/build.yml` builds both platforms on every push. The macOS job
signs, notarises and staples a `.pkg`; the Windows installer is still built
locally with Inno Setup and is not yet Authenticode-signed.

Signing uses these **organisation** secrets (Amanorsac-Studio):

| Secret | What it is |
|---|---|
| `MAC_CERT_P12` / `MAC_CERT_PASSWORD` | Developer ID **Application** cert — signs the .vst3/.component/.app |
| `MAC_INSTALLER_P12` / `MAC_INSTALLER_PASSWORD` | Developer ID **Installer** cert — signs the .pkg (a different certificate) |
| `APPLE_TEAM_ID` | Apple Developer team identifier |
| `ASC_KEY_P8` / `ASC_KEY_ID` / `ASC_ISSUER_ID` | App Store Connect API key, used by `notarytool` |

`APPLE_DIST_P12` and `MAS_INSTALLER_P12` are the Mac App Store pair and are
deliberately **not** used here — direct distribution needs the Developer ID
certificates, and importing a second identity makes `codesign`'s identity
matching ambiguous.

**This repository must stay private.** Those secrets are scoped to "Private
repositories"; GitHub withholds org secrets from public repos, so a public
SecondOut gets empty strings and silently produces an unsigned build. (Keeping
the source closed also matters for a paid product — the licensing crypto does
not depend on source secrecy, but public source makes a check-removed build
trivial to produce.)

Pushing a **tag** with any signing secret missing fails the build rather than
publishing; any other ref warns and labels the artefact `UNSIGNED`.

## Testing checklist before live use

1. Unit tests pass (ring buffer stress, drift servo convergence, resampler quality).
2. 2–3 h soak with a DAW: buffer meter stays centred, underrun counter stays 0.
3. Unplug Device B mid-stream → *disconnected*, DAW unaffected; replug + reselect → recovers.
4. Change the DAW sample rate mid-session → plugin re-prefills and the UI shows the
   resampling ratio; no garbage audio.
5. Full rehearsal with the real chain (WING → DAW → SecondOut → Device B → OBS).
