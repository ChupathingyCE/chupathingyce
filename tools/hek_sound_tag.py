"""Writes Halo Editing Kit sound tags (.sound, .sound_looping) for the mods
tests (tools/test_mods.py) and for trying the mods folder by hand
(docs/mods.md): a 16-bit PCM .sound from a WAV file, or from a tone made
here, split into chained permutations the way tool.exe and invader-sound
split long sounds, and a .sound_looping that plays it.

The file layout is the one port/linux/game/mod_sounds.c reads: a 64-byte
header ending in 'blam', the body struct big-endian, then what each field
owns in field order (a reference's path, a block's elements then each
one's own data, a data field's bytes); PCM samples are big-endian too.

    python3 tools/hek_sound_tag.py --wav music.wav --rate 44100 --stereo \
        out/sound/music/intro.sound
    python3 tools/hek_sound_tag.py --tone 440 --seconds 3 out/tone.sound
    python3 tools/hek_sound_tag.py --looping sound/music/intro \
        out/sound/music/intro_loop.sound_looping

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import math
import struct
import wave
from pathlib import Path

SOUND = b"snd!"
SOUND_LOOPING = b"lsnd"
SOUND_VERSION = 4
SOUND_LOOPING_VERSION = 3

CLASS_MUSIC = 4
ENCODING_MONO = 0
ENCODING_STEREO = 1
RATE_22050 = 0
RATE_44100 = 1
COMPRESSION_PCM = 0
COMPRESSION_OGG = 3

# the permutation size tool.exe and invader-sound split long sounds at
# (invader-sound's --split: 227 KiB of samples)
SPLIT_SIZE = 0x38E00


def _header(group, version):
    """The 64-byte tag file header."""
    head = bytearray(64)
    head[0x24:0x28] = group
    struct.pack_into(">I", head, 0x2C, 64)
    struct.pack_into(">HH", head, 0x38, version, 255)
    head[0x3C:0x40] = b"blam"
    return bytes(head)


def reference(group, path):
    """A tag reference's struct bytes and the path that follows its owner."""
    path = path.encode("ascii") if path else b""
    fields = struct.pack(">4sIIi", group, 0, len(path), -1)
    trailer = path + b"\0" if path else b""
    return fields, trailer


def block(count):
    return struct.pack(">Iii", count, 0, 0)


def data(size):
    return struct.pack(">IIIII", size, 0, 0, 0, 0)


def permutation(name, samples, compression=COMPRESSION_PCM, next_index=-1, gain=1.0, skip=0.0):
    name = name.encode("ascii")[:31]
    fields = struct.pack(">32sffhhiIIIi", name, skip, gain, compression, next_index, -1, 0, 0,
                         len(samples) if compression == COMPRESSION_PCM else 0, -1)
    fields += data(len(samples)) + data(0) + data(0)
    assert len(fields) == 124
    return fields, samples


def pitch_range(name, permutations, actual_count, natural_pitch=1.0, bend=(0.0, 0.0)):
    name = name.encode("ascii")[:31]
    fields = struct.pack(">32sfffhhfIhh", name, natural_pitch, bend[0], bend[1], actual_count, 0, 0.0, 0,
                         -1, -1)
    fields += block(len(permutations))
    assert len(fields) == 72
    trailer = b"".join(p[0] for p in permutations) + b"".join(p[1] for p in permutations)
    return fields, trailer


def sound(pitch_ranges, sound_class=CLASS_MUSIC, encoding=ENCODING_STEREO, rate=RATE_44100,
          compression=COMPRESSION_PCM, flags=0, minimum=0.9, maximum=5.0, promotion=""):
    fields = struct.pack(">Ihhffffffffff", flags, sound_class, rate, minimum, maximum, 0.0, 1.0, 1.0, 0.0, 0.0,
                         0.0, 1.0, 0.0)
    fields += b"\0" * 12 + struct.pack(">fff", 1.0, 1.0, 1.0)
    fields += b"\0" * 12 + struct.pack(">fff", 1.0, 1.0, 1.0)
    fields += b"\0" * 12 + struct.pack(">hh", encoding, compression)
    promotion_fields, promotion_path = reference(SOUND, promotion)
    fields += promotion_fields
    fields += struct.pack(">hhiiiIi", 0, 0, 0, 0, 0, 0xFFFFFFFF, -1)
    fields += block(len(pitch_ranges))
    assert len(fields) == 164, len(fields)
    trailer = promotion_path + b"".join(r[0] for r in pitch_ranges) + b"".join(r[1] for r in pitch_ranges)
    return _header(SOUND, SOUND_VERSION) + fields + trailer


def looping_sound(loop, start="", stop="", gain=1.0, fade_in=0.0, fade_out=0.0, flags=0):
    fields = struct.pack(">Ifiifiiifii", flags, 0.0, 0, 0, 0.0, 0, 0, -1, 0.0, 0, 0)
    assert len(fields) == 44
    damage, damage_path = reference(b"cdmg", "")
    fields += damage + block(1) + block(0)
    assert len(fields) == 84
    track = struct.pack(">Ifff", 0, gain, fade_in, fade_out) + b"\0" * 32
    paths = b""
    for group, path in ((SOUND, start), (SOUND, loop), (SOUND, stop)):
        f, p = reference(group, path)
        track += f
        paths += p
    track += b"\0" * 32
    for path in ("", ""):
        f, p = reference(SOUND, path)
        track += f
        paths += p
    assert len(track) == 160
    return _header(SOUND_LOOPING, SOUND_LOOPING_VERSION) + fields + damage_path + track + paths


def tone(frequency, seconds, rate, channels):
    frames = int(seconds * rate)
    out = bytearray()
    for frame in range(frames):
        value = int(12000 * math.sin(2 * math.pi * frequency * frame / rate))
        for _ in range(channels):
            out += struct.pack(">h", value)
    return bytes(out)


def read_wav(path):
    """A WAV's 16-bit samples, byte-swapped to the tag's big-endian order."""
    with wave.open(str(path), "rb") as wav:
        if wav.getsampwidth() != 2:
            raise SystemExit("the WAV must be 16-bit")
        frames = wav.readframes(wav.getnframes())
        swapped = bytearray(frames)
        swapped[0::2], swapped[1::2] = frames[1::2], frames[0::2]
        return bytes(swapped), wav.getframerate(), wav.getnchannels()


def split(samples, frame_size):
    """Chunks of at most SPLIT_SIZE bytes, each a whole number of frames."""
    step = SPLIT_SIZE - SPLIT_SIZE % frame_size
    return [samples[i:i + step] for i in range(0, len(samples), step)] or [b""]


def sound_from_samples(samples, rate, channels, name="default", sound_class=CLASS_MUSIC, do_split=True):
    if rate not in (22050, 44100):
        raise SystemExit("the rate must be 22050 or 44100")
    if channels not in (1, 2):
        raise SystemExit("mono or stereo")
    chunks = split(samples, 2 * channels) if do_split else [samples]
    permutations = []
    for index, chunk in enumerate(chunks):
        permutations.append(permutation(name if index == 0 else f"{name}_{index}", chunk,
                                        next_index=index + 1 if index + 1 < len(chunks) else -1))
    ranges = [pitch_range("default", permutations, 1)]
    return sound(ranges, sound_class=sound_class, encoding=ENCODING_STEREO if channels == 2 else ENCODING_MONO,
                 rate=RATE_44100 if rate == 44100 else RATE_22050)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("out", type=Path)
    parser.add_argument("--wav", type=Path, help="a 16-bit WAV to put in a .sound")
    parser.add_argument("--tone", type=float, help="a sine tone's frequency instead of a WAV")
    parser.add_argument("--seconds", type=float, default=2.0)
    parser.add_argument("--rate", type=int, default=44100)
    parser.add_argument("--stereo", action="store_true")
    parser.add_argument("--no-split", action="store_true", help="one permutation however long")
    parser.add_argument("--looping", metavar="SOUND_PATH", help="write a .sound_looping of this sound tag path")
    args = parser.parse_args()
    if args.looping:
        out = looping_sound(args.looping)
    elif args.wav:
        samples, rate, channels = read_wav(args.wav)
        out = sound_from_samples(samples, rate, channels, do_split=not args.no_split)
    elif args.tone:
        channels = 2 if args.stereo else 1
        out = sound_from_samples(tone(args.tone, args.seconds, args.rate, channels), args.rate, channels,
                                 do_split=not args.no_split)
    else:
        parser.error("--wav, --tone or --looping")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f"{args.out}: {len(out)} bytes")


if __name__ == "__main__":
    main()
