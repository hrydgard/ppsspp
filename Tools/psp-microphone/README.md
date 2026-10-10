# PSP microphone recorder

Small interactive PSP homebrew used to test host microphone capture in PPSSPP.
It uses `sceAudioInputBlocking()` at 44100 Hz, with 1024-sample mono S16 blocks,
and plays the recorded samples through `sceAudioOutputBlocking()`.

## Build

With [PSPSDK](https://github.com/pspdev/pspsdk) on your PATH, run `make` in this
directory. Open the resulting `EBOOT.PBP` in PPSSPP.

Alternatively, from this directory:

```sh
docker run --rm -v "$PWD:/src" -w /src pspdev/pspdev:latest make
```

## Test

Select your microphone in PPSSPP's Audio settings. The peak meter updates while
the recorder is ready or recording.

- Press PSP **X** to start recording. The screen shows red **Recording**.
- Speak into the microphone, then press **X** again. The screen shows green
  **Play**, and the application plays the recorded audio.
- Press **X** during playback to start a new recording.
- Press **Start** to exit.

Recording is held in memory and limited to 60 seconds. Listen for gaps or
distortion during playback; a nonzero peak alone does not establish correct
capture. Negative input or output results are displayed on screen.

This is an interactive host-audio check, separate from the deterministic
`pspautotests` suite. It was tested with PPSSPP's Linux SDL microphone backend;
it has not been tested on a physical PSP. The separate rate/block-size checks
described in the microphone PR used an additional local validation program.
