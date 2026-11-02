# AI noise cancellation (the mic chain)

Pick **AI** under Noise isolation. The cog next to it opens the **Noise Cancellation** window, laid out like the mic
page of SteelSeries Sonar. The code is `src/isolation/mic_chain.hpp`; the window is `paintMic` / `micProc` in
`src/app.cpp`.

## What runs, in order

The mic arrives at 48 kHz mono and goes through the chain in 10 ms frames (480 samples). Every part always runs, so
switching one on or off crossfades over 20 ms with no click.

1. **AI noise cancellation**: RNNoise 0.2 (`src/third_party/rnnoise`, BSD-3-Clause), a small recurrent network that
   gives a gain per band each frame. The Min..Max slider sets how much of the original is left: in pauses Min takes about 10 dB
   of noise away, the default (70) 30 dB, and Max all RNNoise can (60 dB and more).
2. **Noise reduction** (only while the AI is off, as in Sonar):
   - **Background**: a spectral noise reducer (256-point STFT, hop 128) that follows the noise floor by its minimum and
     takes it down with a Wiener gain, between 6 and 30 dB.
   - **Impact**: sudden noise such as keys and mouse clicks. It looks ahead 3 ms, and a sound that jumps up and dies
     within 20 ms is turned down until the room is back at its level (150 ms at most). A key or mouse press seen by
     Pixl marks the next 120 ms as a likely click. While you talk it only dips 6 dB for 8 ms, so words stay whole.
3. **Noise gate**, timed like Sonar's: it listens to a 2 ms power average (so a lone noise peak does not open it),
   stays open 80 ms after the last moment above the threshold, then closes over 50 ms to 60 dB down. With *Set the
   threshold from the room by itself* it sits 10 dB over the room's floor (between -70 and -20 dB).
4. **Compressor**: soft knee (6 dB), 5 ms attack, 150 ms release. Level 0.00..1.00 moves the threshold from -10 to
   -36 dB and the ratio from 1.5:1 to 6:1, with half of the gain reduction made back up. While Sonar match is on, the
   Compressor drives the match's own leveller instead: Level 0.50 is the 2.4:1 Sonar gave, 1.00 is 3.8:1.
5. **Sonar match** (the switch top right): the tone, evenness and level SteelSeries Sonar gave the same voice. See
   below.

RNNoise's output is two frames behind its input, so the dry and noise reduction paths are delayed to match. The whole
chain adds **30 ms** (1440 samples); Sonar added 157 ms on the same PC. It uses about 1.3% of one CPU core.

New installs start like the measured Sonar: AI at Max, the gate on at -40 dB, the compressor on at 0.50, Sonar match
on. Those were also the settings on the Sonar that was measured.

## Sonar match

The same 45 seconds of speech (talking, pauses, typing, quiet and loud) were recorded at once from the raw Fifine
microphone and from *SteelSeries Sonar - Microphone*, with Sonar's noise cancellation on. Comparing the two showed what
Sonar does:

- **Pauses**: 82 dB down, digital silence.
- **Tone**: compared with 400-630 Hz, Sonar leaves the bass (below 250 Hz) about 15 dB stronger and the highs
  (6-16 kHz) about 23 dB stronger, and cuts everything above 16 kHz.
- **Evenness**: about 2.4:1 over the whole speaking range: 20 dB of difference in the raw voice is 8.5 dB after Sonar.
- **Level**: speech 21 dB under the raw mic (6.5 dB of it is the Sonar microphone's Windows level, 65%).

The match stage is two shelves, two peaks and a 4th-order top cut, then a soft-knee leveller (10 ms attack, 200 ms
release) and the output level. Its numbers were fitted by running Pixl's chain on the raw recording and moving them
until the output matched Sonar's in 13 bands and in output level against input level. On that recording:

| | Sonar | Pixl (AI Max, gate -40 dB, compressor 0.50, Sonar match) |
|---|---|---|
| speech level, median / loud end | -41.5 / -39.0 dBFS | -41.7 / -39.0 dBFS |
| pauses, median / 90% | -130 / -125 dBFS | -161 / -131 dBFS |
| quiet word starts (raw -45..-41 dB) | -71.8 dBFS | -69.4 dBFS |
| tone, every band 50 Hz-23 kHz | | within 2.1 dB of Sonar |
| output against input, 8 ranges | | within 1.4 dB of Sonar |

The match was measured on one voice and one Fifine; another mic or voice gets the same curve, not a new fit.

## How it was tested

`tests/mic/mic_test.cpp` mixes three recorded voices (`tests/mic/make-voices.ps1`) with seven kinds of noise and
measures each system: how much of each word's start and end is lost where it can be heard, how far the noise goes down
in pauses and under speech, and STOI (speech intelligibility, 0..1). Results with the default settings (AI, level 70):

| case | start / end loss | noise down in pauses | STOI in > out |
|---|---|---|---|
| Soft speech after silence, fan 15 dB SNR | 3.7 / 1.2 dB | 30 dB | 0.98 > 0.99 |
| Quiet endings, air conditioning 10 dB SNR | 6.8 / 3.9 dB | 30 dB | 0.94 > 0.97 |
| Whisper with a fan, 5 dB SNR | 3.2 / 2.1 dB | 30 dB | 0.84 > 0.87 |
| Speech while typing | 0.6 / 0.5 dB | 3 dB | 0.92 > 0.95 |
| Rapid words, fan 12 dB SNR | 0.7 / 0.7 dB | 30 dB | 0.98 > 0.98 |
| Hum, coil whine, PC fan, mouse | 0.4 / 1.1 dB | 15 dB | 1.00 > 1.00 |
| Clean speech | 0.2 / 0.0 dB | - | 1.00 |

At Max the pauses go 62 to 76 dB down. Noise reduction (Background and Impact at 0.50) leaves clean speech untouched,
takes typing down 7 dB in pauses and keeps STOI where it was.

Build and run the bench with MSVC: compile `mic_test.cpp` and the RNNoise sources (`/D__SSE2__ /std:c11`), then run it
from the repository folder. It writes `build/mic/out/results.md`.

## Limits

- RNNoise is trained on speech and common noise. Typing under speech is its weak spot: it keeps your voice and takes
  only about 3 dB of the keys. Turn the AI off and Impact on if keys are the main problem.
- Quiet word endings in strong noise lose a few dB at the default level; lower the AI slider if endings sound clipped.
- The chain adds 30 ms to the mic.
