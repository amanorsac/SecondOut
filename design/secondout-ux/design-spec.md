# SecondOut UX design specification

## Design intent

SecondOut should read as a broadcast confidence monitor: calm when healthy, unmistakable when attention is required. The revision keeps the existing control model and compact 440 px width while making status the dominant visual object.

## What changed

- The status card is taller and carries a 4 px state rail, large state word, LED, plain-language subtitle, and a compact operational badge.
- Error states say what happened and what to do next. Retry is present only when useful.
- Device selection remains above status, preserving the setup flow. The output toggle is visually separate and labels ON/OFF below the control.
- Buffer Health is isolated from diagnostics, with a bright 50% marker and restrained scale labels.
- Diagnostics use larger values, quieter labels, and fixed column widths to prevent jitter.
- The UI remains fixed at 440 × 456 px. Resizing adds complexity without helping the configure-once/glance-often use case.

## Color tokens

| Token | Hex | Use |
|---|---:|---|
| canvas-0 | #0C0F13 | window background |
| canvas-1 | #101318 | upper background |
| surface-1 | #151A20 | cards and diagnostics |
| surface-2 | #1A1F26 | hero highlight |
| control | #171B21 | buttons and selector |
| hairline | #303843 | component edge |
| text-primary | #F4F7FA | values and primary copy |
| text-secondary | #C5CDD7 | supporting copy |
| text-muted | #8E99A7 | labels |
| text-dim | #56616E | footer and tertiary copy |
| healthy | #3FE083 | streaming / safe buffer |
| caution | #F2B84B | buffering / edge zones / resampling |
| fault | #FF5C68 | disconnected / open failure |
| neutral | #84909F | no device / inactive |

State colors never carry meaning alone: every state also changes the state word, subtitle, badge, and (where needed) action.

## Type

Use Segoe UI Variable when available, then Segoe UI. Avoid condensed faces.

| Role | Size | Weight | Tracking |
|---|---:|---:|---:|
| Status word | 21 px | 700–750 | +0.4 px |
| Metric value | 14 px | 700 | 0 |
| Product title | 13 px | 600–650 | 0 |
| Body / device | 10 px | 500 | 0 |
| Labels | 8 px | 700 | +1.0 px |
| Microcopy | 7–8.5 px | 400–700 | 0 to +1.1 px |

Use tabular numerals for Buffer, Drift Trim, sample rates, latency, and underruns.

## Geometry and spacing

- Window: 440 × 456 px.
- Outer gutter: 16 px. Major vertical rhythm: 12 px.
- Controls: 38 px high, 9 px radius. Toggle: 53 × 28 px.
- Cards: 11 px radius, 1 px hairline. No heavy shadows.
- Hero: 408 × 96 px. Status LED: 12 px core with restrained halo.
- Minimum pointer target: 28 px for desktop; primary selectors and buttons are 38 px.
- Align all left content to x=16; inside-card content begins at 13 px or 46 px after the LED.

## State behavior

### Streaming
Green rail and LED. Show LIVE · STABLE plus a low-amplitude activity waveform. Waveform indicates signal activity, not level; do not make it look like a meter.

### Buffering
Amber rail and LED. Show PREPARING and “Usually ready in a few seconds.” Avoid progress percentages because completion time is not deterministic.

### No device
Neutral rail and LED. Device selector becomes the obvious next action. Keep Output off and disable it until a valid device is selected.

### Disconnected
Red rail and LED. Explain that the device stopped responding. Offer Retry after the device is reconnected. Increment and preserve underruns for the session.

### Could not open
Red rail and LED. Lead with the likely contention cause and offer Retry. Keep the selected device name so the user knows which endpoint failed.

### Resampling
In Streaming or Buffering, place an amber chip below the subtitle: RESAMPLING · 48.0 → 44.1 kHz. It should not replace or compete with the primary state. Use the warning triangle only if resampling has a measurable cost or quality implication; otherwise the amber label is enough.

## Motion

- LED: steady in healthy state; no blinking in fault states. Blinking can be misread from across a room and becomes fatiguing.
- Streaming waveform: 1.8–2.4 s organic loop, opacity 0.55–0.95, amplitude variation only. Respect reduced motion by freezing at a representative frame.
- Buffer Health: interpolate fill movement over 160 ms using cubic-bezier(0.22, 1, 0.36, 1). Never animate the target marker.
- State transitions: 180 ms crossfade for text plus color; error transitions may be immediate if triggered by device loss.

## Accessibility and implementation notes

- Primary text and state colors are selected for strong contrast on the dark surfaces; validate final rasterization with the actual JUCE renderer.
- Never encode health only in hue. Text, labels, position, and action copy reinforce meaning.
- Expose state changes to accessibility APIs without repeatedly announcing buffer fluctuations.
- Keep device names single-line with middle ellipsis, preserving both vendor and endpoint suffix when possible.
- Add hover/focus descriptions for Buffer Health and Drift Trim; the default screen remains terse.
- Details expands downward, increasing window height temporarily rather than compressing the hero. Recommended rows: DAW rate, device rate, estimated output latency, audio format, last device error.
