# sceAudio: how the PSP's audio output actually behaves

This describes the behavior `Core/HLE/sceAudio.cpp` and `Core/HLE/__sceAudio.cpp` model, and
where that behavior came from. Confirmed on a real PSP by `pspautotests/tests/audio/blocking`.

The headline, because it is the thing most likely to be assumed wrong: **the blocking output
calls are not a queue that callers line up behind.** Each channel holds one buffer, the
Output2/SRC channel holds two, and a caller who finds no room is told
`SCE_ERROR_AUDIO_CHANNEL_BUSY` and expected to go away and come back.

## Two different pieces of hardware

`sceAudio` presents nine channels, but they are not the same thing underneath, which is why the
emulator keeps them in two different structures rather than one array of nine: `AudioChannel`
for the eight the mixer walks, and a single `AudioSRCChannel` for the ninth.

**Channels 0-7** go through a software mixer running as a kernel thread. Every time the audio
DMA finishes a block, that thread wakes, takes up to 64 samples from each channel that has a
buffer, sums them into a 32-bit accumulator, clamps, and writes 64 stereo frames into the other
half of a double buffer.

**Channel 8** - `sceAudioOutput2*`, `sceAudioSRC*` and `sceVaudio*` are all the same channel -
never touches the mixer. It builds DMA descriptors that point straight at the game's buffer and
lets the codec resample it. `sceAudioOutput2Reserve(n)` is literally
`sceAudioSRCChReserve(n, 44100, 2)`, and the other Output2 entry points are one-instruction tail
calls to the SRC ones.

They also have separate DMA channels, so one starting does not disturb the other.

## The driver plays out of the game's memory

Nothing is copied when a buffer is handed over. `sceAudioOutputBlocking` stores the pointer, and
the mixer walks it forward 64 samples at a time until it is spent, then clears it and signals.
`sceAudioGetChannelRestLen` counts down in steps of 64 for exactly that reason.

The emulator used to copy the whole buffer into a ring of samples at enqueue time, with the
volume already applied. That made a game rewriting a buffer it had already handed over
invisible, and it made the queue depth an emulator choice rather than a hardware fact.

## One buffer per channel

The driver's single enqueue point, which all four `sceAudioOutput*` functions reach, is:

```c
if (sampleCount == 0)     return SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
if (sampleAddress != 0)   return SCE_ERROR_AUDIO_CHANNEL_BUSY;
remaining = sampleCount;
sampleAddress = ptr;      // may be 0
return sampleCount;
```

`sceAudioOutput` and `sceAudioOutputPanned` return that result as it stands, so a second
non-blocking output is refused until the first buffer has finished.

The blocking pair adds a wait, but only for the first thread to ask:

```c
r = enqueue(...);
if (r != BUSY)         return r;
if (channel->waiting)  return BUSY;      // somebody else is already parked here
channel->waiting = 1;
wait for this channel's bit in the driver's event flag;
retry the enqueue;
channel->waiting = 0;
```

**Only one thread can be parked on a channel.** That single flag is why a game that runs a movie
thread and a sound-effect thread over one output gets sensible behavior on hardware and did not
in the emulator: the loser is told the channel is busy and skips its turn, where an emulator that
blocked it instead made the two threads alternate, halving the movie's audio rate. That is
https://github.com/hrydgard/ppsspp/issues/12888, and it is what
`tests/audio/blocking/contend` pins down.

The same flag locks out `sceAudioChRelease`, `sceAudioSetChannelDataLen` and
`sceAudioChangeChannelConfig`, which all return `SCE_ERROR_AUDIO_CHANNEL_BUSY` while a thread is
parked. `sceAudioChangeChannelVolume` does not check anything.

A null pointer is accepted and sets `remaining` without setting `sampleAddress`, so nothing
plays. That is the documented way to wait for a channel to drain, and it is the one case where
the two rest-length calls disagree - see below.

## Two buffers on the SRC channel

Channel 8 has two DMA descriptors, so two buffers can be in flight. The third caller is refused
outright and does not even get the chance to wait:

```c
if (!reserved)                    return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
if (both descriptors armed)       return SCE_ERROR_AUDIO_CHANNEL_BUSY;
arm one;
wait for one descriptor to retire;
return sampleCount;
```

Starting the DMA sets the same event flag bit that a retiring descriptor does, which is why the
first output after an idle stretch returns without waiting while every one after it waits exactly
one buffer. The emulator models that unclaimed completion with a flag on the channel.

## The two rest-length calls are not the same function twice

```c
sceAudioGetChannelRestLen(ch)     = remaining + (waiting ? sampleCount : 0)
sceAudioGetChannelRestLength(ch)  = (sampleAddress ? remaining : 0) + (waiting ? sampleCount : 0)
sceAudioOutput2GetRestSample()    = (armed descriptor count) * current sampleCount
```

Both channel versions count a parked thread's buffer as well as the one playing. They only
differ after an output with a null pointer, which leaves `remaining` set with no buffer, and the
hardware duly reports `0x400` from one and `0` from the other.

`sceAudioOutput2GetRestSample` reports in units of the *current* length, so after
`sceAudioOutput2ChangeLength(64)` a 4096-sample buffer armed earlier reads back as `0x40`.

## Where the emulator has to fake something

The mixer thread outranks whoever called, so on hardware the first 64 samples of a buffer are
consumed before the output call has returned - a channel reserved for exactly 64 samples is free
again immediately. The emulator's mixer is a timer event instead, so it re-phases that event to
the moment the mixer's DMA starts and runs one block right then. The SRC channel gets the phase
reset but no early read, since its DMA feeds the codec directly. Without this the answer to
`sceAudioGetChannelRestLen` right after an output would depend on where the timer happened to
be, and would differ from run to run.

Two things are still approximate, both below one mix block:

- A descriptor retires when the DMA transfer finishes, which is slightly ahead of the audio
  being heard, so hardware frees an Output2 buffer about 100us earlier than the emulator does.
- The emulator only retires SRC buffers on block boundaries, so a buffer whose length is not a
  multiple of the block can be up to 1.5ms late. `audio/output2/frequency` and
  `audio/output2/rest` are in `tests_next` for this reason.

## Argument checking

Recorded here because several of these were guesses before, and because the *order* of the
checks is observable.

| function | checks, in order |
|---|---|
| `sceAudioChReserve` | the free-channel search runs 7 down to 0 and needs the channel both released and finished playing; bad channel `80260003`; already reserved `80260003`; count not a positive multiple of 64 up to 0xFFC0 `80260006`; format not 0 or 0x10 `80260007` |
| `sceAudioChRelease` | bad channel `80260003`; not reserved `80260008`; a thread parked `80260002`. Clears only the reservation - a buffer already playing plays out. |
| `sceAudioSetChannelDataLen` | bad channel `80260003`; **bad length `80260006`, before the reservation is looked at**; parked thread `80260002`; not reserved `80260001` |
| `sceAudioChangeChannelConfig` | bad channel `80260003`; parked thread or buffer in flight `80260002`; not reserved `80260008`; bad format `80260007` |
| `sceAudioChangeChannelVolume` | either side above 0xFFFF `8026000b`; bad channel `80260003`; **no reservation check**; a negative volume leaves that side alone |
| `sceAudioOutput`, `sceAudioOutputPanned` | each volume compared signed, so negatives pass and mean "leave unchanged" |
| `sceAudioOutputPannedBlocking` | the two volumes are ORed before comparing, so a negative one fails with `8026000b` |
| `sceAudioOutput2ChangeLength` | **length outside 17..4111 `80260006`, before the reservation**; not reserved `80260008` |
| `sceAudioSRCChReserve` | channels not 2 or 4 `80000104`; channels 4 `80000003`; count outside 17..4111 `80000104`; bad frequency `8026000a`; already reserved `80268002` |
| `sceAudioSRCChRelease` | not reserved `80260008`; a descriptor still armed `80268002` |

Accepted SRC frequencies are 8000, 11025, 12000, 16000, 22050, 24000, 32000 and 48000, plus
whatever the output is currently running at - which is how 44100 and 0 get through.

## sceAudioOneshotOutput

The odd one out: it plays a single buffer on a channel it never reserves. The channel stays
free as far as everything else is concerned, so `sceAudioChRelease` on it answers `80260008`,
and it returns to the pool by itself once the buffer runs out. It returns the channel number,
takes any positive sample count - alignment and the 0xFFC0 ceiling do not apply - and unlike
the other outputs a negative volume is an error rather than "leave it alone". There is no busy
check either: a second one-shot while the first is playing just replaces it.

No game is known to call it. It is implemented because the behavior turned out to be simple
once traced, not because anything needed it.

## sceVaudio is a third shape

`sceVaudio` reserves the same channel as Output2 and SRC, but its own release is not the same
call. It hands the channel a null pointer first - the drain idiom above - and only then does the
ordinary SRC release. So it blocks for one buffer and returns 0 where the other two would refuse
with `80268002`, and whatever was playing is played out rather than cut off. It also ignores its
own reservation flag and releases whatever holds the SRC channel, which is why
`tests/audio/sceaudio/reserve` gets away with what it calls the "wrong release".

`sceVaudioChReserve` marks vaudio reserved *before* delegating and does not undo that when the
delegate fails, so a caller that got `80268002` because Output2 held the channel is told
`80000021` next time round until a release clears it.

## What a call costs

Worth knowing because a game whose losing thread retries in a loop feels it directly.
`tests/audio/blocking/overhead` measures it in buckets:

| call | cost |
|---|---|
| Output2/SRC output, both descriptors already armed | 30-100us |
| Output2/SRC output, any other outcome, including an unreserved channel | over 100us |
| mixer output that is refused, blocking or not | under 10us |
| mixer output that starts the DMA | over 100us |

Every SRC output ends up querying the codec, which is the slow part; only the refused case gets
out before it. Mixer channels are the other way round - cheap unless the call is the one that
brings the DMA up.

## Savestates

`AudioChannel` is at section version 4 and the SRC channel has a section of its own. Anything
older stored a ring of already-mixed samples, which cannot be turned back into a buffer pointer
and a position, so loading one drops the pending audio and wakes any parked threads. That costs
a fraction of a second of silence on load and nothing else. Older states also carry the SRC
channel as a ninth entry in the channel array, which is read and discarded.
