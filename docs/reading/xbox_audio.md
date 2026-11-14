# The Xbox's 3D sound and reverb

The desktop builds play the game's DirectSound calls through a software
mixer (`port/linux/src/dsound_sdl.c`). The Xbox's MCPX audio processor is
not there on a PC, phone or console, so the mixer stands in for two things
it did: the head-related transfer function (HRTF) every 3D voice went
through, and the I3DL2 reverb of the DSP effects image the game downloads.
Both are models measured against an Xbox, not a cycle-accurate MCPX.

## Where they came from

The models are OpenH1's, from
[OpenH1/OpenH1 revision 6750db5a](https://github.com/OpenH1/OpenH1/tree/6750db5a6c9716857bd1e63d9ec188a20c49f1d3),
as chestahh brought them into [GulchCE](https://github.com/chestahh/GulchCE)
(commit 2704f5c0). The files keep their attribution:

- `port/linux/src/dsound_xbox_hrtf.inl`: the HRTF. Each ear's level in nine
  bands and its delay, for every 15 degrees round and 60, 40, 20 and 0
  degrees below and above, plus the poles, fitted to recordings of an
  Xbox's output (a sweep played through Halo's own DirectSound calls).
- `port/linux/src/dsound_xbox_reverb.inl`: the reverb. The DSP program's
  reverb redone in floating point, with the XDK DirectSound's conversion of
  the listener properties into its parameters, then refined against
  recordings: a four-channel reflection network and a four-line feedback
  delay network at 24 kHz, with the DSP's delay quantization and its
  100 ms delay hold on a change of environment.
- `port/linux/src/dsound_xbox_types.h`: the HRTF's constants and a voice's
  parameters.

The rest of that GulchCE commit (its resamplers, LFE filter, level floor,
Doppler law and test fixtures) was not taken then; Chupathingy's mixer keeps its
own resampling, Doppler, obstruction and surround handling around the two
models. Some of it came later, compared one by one against our mixer (below).

## More of the OpenH1 team's measurements (0.9.1a)

The OpenH1 team (goatbutt and the OpenH1 team) recorded more of an Xbox
after 2026-11-06 and matched their mixer to it. Each change was tried on our
mixer and kept only where it measured closer to the Xbox, with no second
path beside the old one:

- **The Xbox's resampler** (`audio.resampling = "xbox"`, the new default): a
  sinc at the source's Nyquist rate over four frames each side, Kaiser beta
  8.3, never narrowed. A click at half the output's rate comes out as the
  Xbox's did (0.599, -0.120, 0.023, -0.002 against the Xbox's 0.598, -0.120,
  0.024, -0.003). It leaves some images above a 22 kHz sound's band, as the
  Xbox did. `"sinc"` keeps the longer, cleaner low pass.
- **The Doppler law**: the first order 1 + (vl + vs) / c. An Xbox shifted a
  source passing at 20 units a second by 1.178 and 0.822, at 40 by 1.356 and
  0.644; the old law gave 1.216 and 0.849, then 1.551 and 0.738.
- **Parameter ramps** of 64 frames (1.3 ms), carried from one mix to the
  next, for volume, pan, pitch (the Doppler shift too, stepped at each of the
  game's updates rather than glided), the low passes and the HRTF. An Xbox
  settles each within 1 to 2 ms. A change now takes the same time however
  many frames SDL asks for.
- **A voice's own Room HF** does nothing on an Xbox; the environment's Room
  HF and occlusion still muffle the room send.
- **A 20 Hz high pass** before the limiter, for the maps' sounds that carry
  energy below hearing.

The 100 ms hold of a new room's reverb delays was already in
`dsound_xbox_reverb.inl`. `tools/test_audio_doppler.py` checks the Doppler
law, the Xbox resampler's weights and the high pass.

## What they replaced, and why

Before, the mixer had its own two: binaural rendering from the SADIE II
KEMAR responses, and a generic 8-line I3DL2 feedback delay network. Both
were measured offline against the figures OpenH1 took from the Xbox (the
reverb's by running the retail DSP program on a DSP56300 interpreter; the
HRTF's from the recordings):

- The reverb's decay times were close either way, but the old late
  reverberation was 5 to 16 dB too quiet against the Xbox's, differently
  in each room (hallway -7 dB, hangar -14 dB, underwater -16 dB), and the
  mountains' reflections came a quarter second late (a clamp where the
  DSP wrapped its delay line). The Xbox model tracks the Xbox's levels
  within a constant 1.6 dB in every environment, and its first reflections
  to the frame.
- The KEMAR head's time between the ears was 0.81 ms to the side where the
  Xbox's is 0.72; its far-ear shadow was 7 to 10 dB deeper at 4 to 8 kHz;
  a sound behind lost 3 dB at 16 kHz where the Xbox's loses 20; and a sound
  straight below was all but indistinguishable from one ahead where the
  Xbox darkens it by 11 to 32 dB above 4 kHz. The Xbox model matches the
  recordings within a decibel, as it was fitted to them.

Both cost the same: about 0.06% of one core per voice for the HRTF, and
0.2% for the reverb.

## How the mixer uses them

`hrtf_place` gives a voice's parameters from its direction and distance;
`hrtf_process` renders a mix of it. The mixer keeps its own crossfade with
the stereo pan (`audio.hrtf` on, off or auto; `hrtf_share`), ramps the
parameters over each mix as it does its gains, and applies the HRTF on
stereo output only, as before: surround output pans 3D voices around the
speakers.

The reverb keeps the mixer's interface (`reverb_update`, `reverb_process`,
`reverb_clear`): each 3D voice's room send, at the listener's and its own
room levels with occlusion and the Room HF low pass, on a mono bus. Its four
channels play from the front and back pairs on 5.1; on 7.1 the back pair is
shared between the side and back speakers; on stereo each side's back
channel plays at -3 dB with its front, as the Xbox's stereo downmix did.
`audio.reverb = false` fades it out and stops it.

`tools/test_xbox_audio.py` builds both models alone and checks them against
the Xbox's figures (each environment's decay times and first reflections;
the HRTF's time between the ears and its front, back and below cues).
