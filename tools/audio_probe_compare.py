#!/usr/bin/env python3
"""Compares two recordings of the audio probe (port/linux/game/audio_probe.c):
the Xbox's and a desktop build's, or two desktop builds', of the same probe
file, and says which of the desktop mixer's reverb parameters
(port/linux/src/dsound_sdl.c) would bring the second nearer the first.

    python3 tools/audio_probe_compare.py REFERENCE TEST [--json FILE]

REFERENCE and TEST are the recordings' names without their suffixes, as the
probe writes them beside the probe file: retail_maps-bloodgulch-xbox
(-final.wav and -events.txt; tools/audio_probes/retail_maps.txt) and
retail_maps-bloodgulch-pc (with -dry.wav and -reverb.wav, the desktop
mixer's buses). The reference is the one to match (the Xbox).

Every sound of the probe is played from the same samples on both, so the
desktop recording's dry bus (the voices before the reverb) is what went
into both mixers. Each sound's response, the room the mixer made of it, is
the final mix deconvolved by the dry bus (when a recording has one). From
it, per sound and per spot:

- the decay: EDT and T20 (from the Schroeder integral), and T20 at 1 kHz and
  at 5 kHz, whose ratio is the decay's HF ratio;
- the reverb-to-direct ratio, and the early (the first 80 ms after the
  direct sound) and late energy each relative to the direct sound;
- the tail's brightness: the late part's energy above 4 kHz against its
  energy from 500 Hz to 2 kHz;
- the early reflections: the first few peaks of the response in the 100 ms
  after the direct sound;
- the overall level of the final mix, and how alike its left and right are
  in the tail (1 the same, 0 unrelated: a wide reverb is near 0).

Without a dry bus, only what the final mixes give alone (the level, the
late-to-early energy, the tail's brightness and width).

The two recordings are put together by the probe's logged events (each
sound's frame) and then by the responses' direct sound. Needs numpy.
"""

import argparse
import json
import math
import re
import sys
from pathlib import Path

import numpy as np

# the spans of a response (seconds)
DIRECT_HALF_WIDTH = 0.0025
EARLY_END = 0.080
REFLECTION_SEARCH = 0.100
PRE_ROLL = 0.25
MAXIMUM_LAG = 0.30
# a sound's window is the time to the next, at most this
MAXIMUM_WINDOW = 3.5
# the bands of the decay's HF ratio and of the tail's brightness (Hz)
LOW_BAND = (707.0, 1414.0)
HIGH_BAND = (3536.0, 7071.0)
BRIGHT_BAND = (4000.0, 16000.0)
BODY_BAND = (500.0, 2000.0)


# ---------- the recordings

def read_wav(path):
    """The frames of a WAVE file (16-bit PCM or 32-bit float), as float64
    stereo in -1..1, and its rate."""
    data = Path(path).read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAVE file")
    position = 12
    fmt = None
    samples = None
    while position + 8 <= len(data):
        chunk = data[position:position + 4]
        size = int.from_bytes(data[position + 4:position + 8], "little")
        body = data[position + 8:position + 8 + size]
        if chunk == b"fmt ":
            fmt = (int.from_bytes(body[0:2], "little"), int.from_bytes(body[2:4], "little"),
                   int.from_bytes(body[4:8], "little"), int.from_bytes(body[14:16], "little"))
        elif chunk == b"data":
            samples = body
            # (a recording cut short has a data size of 0 or past the end)
            if size == 0 or position + 8 + size > len(data):
                samples = data[position + 8:]
        position += 8 + size + (size & 1)
    if fmt is None or samples is None:
        raise ValueError(f"{path}: no fmt or data chunk")
    tag, channels, rate, bits = fmt
    if tag == 3 and bits == 32:
        frames = np.frombuffer(samples[:len(samples) // 4 * 4], dtype="<f4").astype(np.float64)
    elif tag == 1 and bits == 16:
        frames = np.frombuffer(samples[:len(samples) // 2 * 2], dtype="<i2").astype(np.float64) / 32768.0
    else:
        raise ValueError(f"{path}: format {tag}, {bits} bits is not read")
    frames = frames[:len(frames) // channels * channels].reshape(-1, channels)
    if channels == 1:
        frames = np.repeat(frames, 2, axis=1)
    return frames[:, :2], rate


def read_events(path):
    """The probe's log: the start, spots, sounds and environments, each a
    dictionary of its key=value words."""
    events = []
    for line in Path(path).read_text().splitlines():
        words = line.split()
        if not words:
            continue
        record = {"kind": words[0]}
        for word in words[1:]:
            if "=" in word:
                key, value = word.split("=", 1)
                record[key] = value
        # (a tag's path may have spaces: the rest of the line)
        match = re.search(r" tag=(.*)$", line)
        if match:
            record["tag"] = match.group(1)
        events.append(record)
    return events


class Recording:
    def __init__(self, prefix):
        self.prefix = str(prefix)
        self.final, self.rate = read_wav(self.prefix + "-final.wav")
        self.dry = self.reverb = None
        if Path(self.prefix + "-dry.wav").exists():
            self.dry, _ = read_wav(self.prefix + "-dry.wav")
        if Path(self.prefix + "-reverb.wav").exists():
            self.reverb, _ = read_wav(self.prefix + "-reverb.wav")
        self.events = read_events(self.prefix + "-events.txt")
        self.platform = next((e.get("platform") for e in self.events if e["kind"] == "start"), "?")
        self.sounds = []
        spot = None
        environment = None
        for event in self.events:
            if event["kind"] == "spot":
                spot = event
            elif event["kind"] == "environment":
                environment = event
            elif event["kind"] == "sound":
                self.sounds.append({"index": int(event["index"]), "spot": event.get("spot", "?"),
                                    "label": event.get("label", "?"), "frame": int(event["frame"]),
                                    "started": event.get("started") == "yes", "environment": environment,
                                    "spot_frame": int(spot["frame"]) if spot else 0})
        ends = [int(e["frame"]) for e in self.events if e["kind"] == "end"]
        self.end = ends[0] if ends else len(self.final)
        # each sound's window: to the next sound or the next spot
        boundaries = sorted({s["frame"] for s in self.sounds} |
                            {int(e["frame"]) for e in self.events if e["kind"] in ("spot", "end")})
        for sound in self.sounds:
            later = [b for b in boundaries if b > sound["frame"]]
            sound["window_end"] = later[0] if later else self.end


# ---------- measures

def mid(frames):
    return frames.mean(axis=1)


def db(value, floor=1e-20):
    return 10.0 * math.log10(max(value, floor))


def band_filter(signal, rate, low, high):
    spectrum = np.fft.rfft(signal)
    frequencies = np.fft.rfftfreq(len(signal), 1.0 / rate)
    spectrum[(frequencies < low) | (frequencies > high)] = 0
    return np.fft.irfft(spectrum, len(signal))


def band_energy(signal, rate, low, high):
    spectrum = np.abs(np.fft.rfft(signal)) ** 2
    frequencies = np.fft.rfftfreq(len(signal), 1.0 / rate)
    return float(spectrum[(frequencies >= low) & (frequencies <= high)].sum())


def smooth(values, width):
    width = max(1, int(width))
    return np.convolve(values, np.ones(width) / width, mode="same")


def decay_times(response, rate):
    """EDT and T20 (seconds) of a response from its direct sound, from the
    Schroeder integral, its noise floor taken off (a simple Lundeby)."""
    energy = response ** 2
    if len(energy) < rate * 0.05 or energy.sum() <= 0:
        return None, None
    tail = energy[int(len(energy) * 0.85):]
    noise = float(tail.mean()) if len(tail) else 0.0
    envelope = smooth(energy, rate * 0.01)
    # (the response ends where it is down at its noise)
    above = np.nonzero(envelope > noise * 3.0)[0]
    end = int(above[-1]) if len(above) else len(energy)
    end = max(end, int(rate * 0.05))
    integral = np.cumsum((energy[:end] - noise)[::-1])[::-1]
    integral = np.maximum(integral, 1e-30)
    curve = 10.0 * np.log10(integral / integral[0])
    times = np.arange(len(curve)) / rate

    def fit(top, bottom):
        span = np.nonzero((curve <= top) & (curve >= bottom))[0]
        if len(span) < rate * 0.01 or curve[-1] > bottom:
            return None
        slope = np.polyfit(times[span], curve[span], 1)[0]
        return -60.0 / slope if slope < 0 else None

    return fit(0.0, -10.0), fit(-5.0, -25.0)


def lag_of(output, dry, rate):
    """How far (frames) output is behind dry: their cross-correlation's peak
    within MAXIMUM_LAG."""
    length = 1 << int(math.ceil(math.log2(len(dry) + len(output))))
    correlation = np.fft.irfft(np.fft.rfft(output, length) * np.conj(np.fft.rfft(dry, length)), length)
    reach = int(MAXIMUM_LAG * rate)
    candidates = np.concatenate([np.arange(0, reach), np.arange(length - reach, length)])
    peak = int(candidates[np.argmax(np.abs(correlation[candidates]))])
    return peak if peak < length // 2 else peak - length


def direct_filter(output, dry, lag, half_width):
    """The filter of 2 half_width + 1 taps around lag that makes most of
    output of dry (least squares): the direct sound, with whatever panning
    or head filter the mixer gives it."""
    length = 1 << int(math.ceil(math.log2(2 * len(dry))))
    x = np.fft.rfft(dry, length)
    autocorrelation = np.fft.irfft(np.abs(x) ** 2, length)
    cross = np.fft.irfft(np.fft.rfft(output, length) * np.conj(x), length)
    taps = 2 * half_width + 1
    offsets = np.arange(taps)
    matrix = autocorrelation[np.abs(offsets[:, None] - offsets[None, :])]
    vector = cross[(lag - half_width + offsets) % length]
    matrix = matrix + np.eye(taps) * matrix[0, 0] * 1e-9
    return np.linalg.solve(matrix, vector)


def deconvolve(output, dry, rate):
    """The response that makes output of dry, regularized by the dry
    sound's own spectrum around each frequency, and left out where the dry
    sound has nothing."""
    length = 1 << int(math.ceil(math.log2(len(dry) + len(output))))
    x = np.fft.rfft(dry, length)
    y = np.fft.rfft(output, length)
    power = np.abs(x) ** 2
    width = max(1, int(length / rate * 20.0))
    local = np.convolve(power, np.ones(width) / width, mode="same")
    # (against the strongest of the spectrum but its few loudest bins: a
    # sound's rumble can stand far above the rest)
    level = 10.0 * np.log10(local / np.percentile(local, 99.0) + 1e-30)
    mask = np.clip((level + 60.0) / 10.0, 0.0, 1.0)
    return np.fft.irfft(y * np.conj(x) / (power + 1e-3 * local + 1e-15 * local.max()) * mask, length)


def response_measures(output, dry, rate):
    """The direct sound and the room of one sound: output split into the
    dry sound through a short filter (the direct sound) and the rest (the
    reflections and the reverberation), whose response to the dry sound is
    measured."""
    lag = lag_of(output, dry, rate)
    half_width = int(DIRECT_HALF_WIDTH * rate)
    direct = direct_filter(output, dry, lag, half_width)
    start = lag - half_width
    shaped = np.convolve(dry, direct)
    predicted = np.zeros(len(output))
    begin = max(0, start)
    span = shaped[begin - start:begin - start + len(output) - begin]
    predicted[begin:begin + len(span)] = span
    residual = output - predicted
    room = deconvolve(residual, dry, rate)
    # (the room from the direct sound on)
    room = np.roll(room, -lag)[:len(dry)]
    room[:half_width] = 0.0
    # (the levels from the signals themselves: the direct sound as
    # predicted, the rest from the sound's onset, the first EARLY_END of it
    # the early part)
    early_end = int(EARLY_END * rate)
    envelope = smooth(np.abs(predicted), rate * 0.001)
    onset = int(np.argmax(envelope > envelope.max() * 0.1)) if envelope.max() > 0 else 0
    direct_energy = float(np.sum(predicted ** 2))
    early = float(np.sum(residual[onset:onset + early_end] ** 2))
    late = float(np.sum(residual[onset + early_end:] ** 2))
    # (the decay from the room's loudest, past the reflections and reverb
    # delays: the slope the reverb's decay time sets)
    loudest = int(np.argmax(smooth(room ** 2, rate * 0.005)))
    edt, t20 = decay_times(room[loudest:], rate)
    _, t20_low = decay_times(band_filter(room, rate, *LOW_BAND)[loudest:], rate)
    _, t20_high = decay_times(band_filter(room, rate, *HIGH_BAND)[loudest:], rate)
    late_part = room[early_end:]
    brightness = None
    if len(late_part) > rate * 0.05:
        body = band_energy(late_part, rate, *BODY_BAND)
        if body > 0:
            brightness = db(band_energy(late_part, rate, *BRIGHT_BAND) / body)
    # the early reflections: peaks of the room's envelope less than 30 dB
    # under the direct sound's
    envelope = smooth(np.abs(room), rate * 0.0005)
    direct_peak = float(np.abs(direct).max())
    reflections = []
    stop = min(len(envelope) - 1, int(REFLECTION_SEARCH * rate))
    for index in range(half_width + 1, stop):
        value = envelope[index]
        if value > direct_peak * 10 ** (-30 / 20) and value >= envelope[index - 1] and value > envelope[index + 1]:
            if not reflections or index / rate * 1000 - reflections[-1]["ms"] > 1.0:
                reflections.append({"ms": round(index / rate * 1000, 1),
                                    "db": round(20 * math.log10(value / direct_peak), 1)})
        if len(reflections) >= 5:
            break
    return {
        "lag_ms": round(lag / rate * 1000, 1),
        "direct_to_reverb_db": db(direct_energy) - db(early + late),
        "early_db": db(early) - db(direct_energy),
        "late_db": db(late) - db(direct_energy),
        "edt": edt, "t20": t20, "t20_1k": t20_low, "t20_5k": t20_high,
        "hf_ratio": (t20_high / t20_low) if t20_high and t20_low else None,
        "tail_brightness_db": brightness,
        "reflections": reflections,
    }


def signal_measures(frames, rate):
    """What the final mix gives alone, from the sound's onset."""
    both = mid(frames)
    envelope = smooth(np.abs(both), rate * 0.001)
    if not len(envelope) or envelope.max() <= 0:
        return None
    onset = int(np.argmax(envelope > envelope.max() * 0.1))
    frames = frames[onset:]
    both = both[onset:]
    split = int(0.15 * rate)
    early, late = both[:split], both[split:]
    measures = {
        "onset": onset,
        "rms_db": db(float(np.mean(both[:int(0.5 * rate)] ** 2))),
        "peak_db": 20 * math.log10(max(float(np.abs(frames).max()), 1e-10)),
        "late_to_early_db": db(float(np.sum(late ** 2))) - db(float(np.sum(early ** 2))),
        "tail_brightness_db": None, "tail_correlation": None,
    }
    if len(late) > rate * 0.05:
        body = band_energy(late, rate, *BODY_BAND)
        if body > 0:
            measures["tail_brightness_db"] = db(band_energy(late, rate, *BRIGHT_BAND) / body)
        left, right = frames[split:, 0], frames[split:, 1]
        if left.std() > 0 and right.std() > 0:
            measures["tail_correlation"] = float(np.corrcoef(left, right)[0, 1])
    return measures


def measure(recording, sound, dry_source, dry_sound):
    """A sound's measures in recording; dry_source the recording whose dry
    bus is the input (dry_sound its record of the same sound)."""
    rate = recording.rate
    start = sound["frame"] - int(PRE_ROLL * rate)
    stop = min(sound["window_end"], sound["frame"] + int(MAXIMUM_WINDOW * rate))
    start = max(0, start)
    final = recording.final[start:stop]
    result = {"signal": signal_measures(final, rate)}
    if dry_source is not None and dry_sound is not None:
        dry_start = max(0, dry_sound["frame"] - int(PRE_ROLL * rate))
        dry = mid(dry_source.dry[dry_start:dry_start + len(final)])
        if len(dry) == len(final) and np.any(dry):
            result["response"] = response_measures(mid(final), dry, rate)
    if recording.reverb is not None and recording.dry is not None:
        wet = float(np.sum(mid(recording.reverb[start:stop]) ** 2))
        direct = float(np.sum(mid(recording.dry[start:stop]) ** 2))
        result["bus_reverb_to_dry_db"] = db(wet) - db(direct)
    return result


# ---------- the report

def median(values):
    values = [v for v in values if v is not None and not (isinstance(v, float) and math.isnan(v))]
    return float(np.median(values)) if values else None


def fmt(value, unit="", digits=2):
    return "-" if value is None else f"{value:.{digits}f}{unit}"


def spot_summary(rows, side, group, key):
    return median([(row[side].get(group) or {}).get(key) for row in rows])


def suggestions(spot, rows, environment):
    """What to change in dsound_sdl.c for this spot's environment."""
    lines = []

    def both(group, key):
        return spot_summary(rows, "reference", group, key), spot_summary(rows, "test", group, key)

    reference, test = both("signal", "rms_db")
    if reference is not None and test is not None and abs(reference - test) > 1.5:
        lines.append(f"overall level: the test is {test - reference:+.1f} dB from the reference; before the reverb, "
                     f"match the levels (audio.volume, or voice_gains' gains) or the reverb's figures below are off "
                     f"by as much")
    for key, name, gain in (("early_db", "early reflections (the first 80 ms)", "reflections_gain"),
                            ("late_db", "late reverberation", "late_gain")):
        reference, test = both("response", key)
        if reference is None or test is None or abs(reference - test) <= 2.0:
            continue
        change = reference - test
        if change > 20.0:
            lines.append(f"{name}: the test's are {change:.0f} dB weaker ({test:.0f} dB against {reference:.0f} dB "
                         f"of the direct sound): see first that they reach the output at all ({gain} in "
                         f"reverb_parameters_set, the room send in voice_gains), then match them")
        else:
            lines.append(f"{name} {change:+.1f} dB needed: {gain} in reverb_parameters_set "
                         f"x{10 ** (change / 20):.2f}")
    if both("response", "late_db")[1] is None:
        reference, test = both("signal", "late_to_early_db")
        if reference is not None and test is not None and abs(reference - test) > 2.0:
            lines.append(f"the tail (150 ms on) against the start is {reference - test:+.1f} dB off (no dry bus for "
                         f"more): late_gain and reflections_gain")
    for key, name in (("t20", "T20"), ("edt", "EDT")):
        reference, test = both("response", key)
        if reference and test and abs(math.log(reference / test)) > math.log(1.1):
            decay = float(environment.get("decay_time", "nan")) if environment else float("nan")
            lines.append(f"decay: {name} {test:.2f} s against {reference:.2f} s: the decay time in "
                         f"reverb_parameters_set x{reference / test:.2f}"
                         + (f" (this environment's {decay:.2f} s as {decay * reference / test:.2f} s)"
                            if not math.isnan(decay) else ""))
            break
    reference, test = both("response", "hf_ratio")
    if reference and test and abs(math.log(reference / test)) > math.log(1.15):
        lines.append(f"HF decay ratio {test:.2f} against {reference:.2f}: the high decay (flDecayHFRatio, its "
                     f"clamp at 1.0, and the lines' damping) x{reference / test:.2f}")
    reference, test = both("response", "tail_brightness_db")
    if reference is None or test is None:
        reference, test = both("signal", "tail_brightness_db")
    if reference is not None and test is not None and abs(reference - test) > 2.0:
        lines.append(f"tail brightness {test - reference:+.1f} dB (4 kHz up against 0.5-2 kHz): "
                     + ("darker the room send's HF low pass (voice_gains: room_hf at the HF reference) or the "
                        "lines' damping" if test > reference else
                        "brighter: less HF loss in the room send's low pass (voice_gains) or the lines' damping"))
    reference_first = median([(row["reference"].get("response") or {}).get("reflections", [{}])[0].get("ms")
                              if (row["reference"].get("response") or {}).get("reflections") else None
                              for row in rows])
    test_first = median([(row["test"].get("response") or {}).get("reflections", [{}])[0].get("ms")
                         if (row["test"].get("response") or {}).get("reflections") else None for row in rows])
    if reference_first is not None and test_first is not None and abs(reference_first - test_first) > 3.0:
        lines.append(f"first reflection at {test_first:.1f} ms against {reference_first:.1f} ms: the taps "
                     f"(reverb_tap_times) or the reflections delay {reference_first - test_first:+.1f} ms")
    reference, test = both("signal", "tail_correlation")
    if reference is not None and test is not None and test - reference > 0.15:
        lines.append(f"the tail's left and right are more alike ({test:.2f} against {reference:.2f}): a wider "
                     f"reverb (reverb_left_signs and reverb_right_signs, or taps of their own to each side)")
    if not lines:
        lines.append("nothing over the thresholds")
    return lines


def compare(reference_prefix, test_prefix):
    reference = Recording(reference_prefix)
    test = Recording(test_prefix)
    if reference.rate != test.rate:
        raise SystemExit(f"the recordings' rates differ ({reference.rate}, {test.rate})")
    dry_source = test if test.dry is not None else reference if reference.dry is not None else None
    rows = []
    for ref_sound, test_sound in zip(reference.sounds, test.sounds):
        if ref_sound["label"] != test_sound["label"] or ref_sound["spot"] != test_sound["spot"]:
            raise SystemExit(f"sound {ref_sound['index']} is {ref_sound['spot']}/{ref_sound['label']} in one "
                             f"and {test_sound['spot']}/{test_sound['label']} in the other: not the same probe")
        if not (ref_sound["started"] and test_sound["started"]):
            continue
        dry_sound = test_sound if dry_source is test else ref_sound
        rows.append({
            "spot": ref_sound["spot"], "label": ref_sound["label"],
            "environment": ref_sound["environment"] or test_sound["environment"],
            "reference": measure(reference, ref_sound, dry_source, dry_sound),
            "test": measure(test, test_sound, dry_source, dry_sound),
        })
    return reference, test, rows


def print_report(reference, test, rows):
    print(f"reference: {reference.prefix} ({reference.platform})")
    print(f"test:      {test.prefix} ({test.platform})")
    print()
    spots = []
    for row in rows:
        if row["spot"] not in spots:
            spots.append(row["spot"])
    for spot in spots:
        spot_rows = [row for row in rows if row["spot"] == spot]
        environment = spot_rows[0]["environment"]
        print(f"== {spot}")
        if environment:
            print("   environment: " + " ".join(f"{k}={v}" for k, v in environment.items() if k not in ("kind",
                                                                                                    "frame")))
        print(f"   {'sound':<14}{'':>4}{'level':>8}{'D/R':>8}{'early':>8}{'late':>8}{'EDT':>7}{'T20':>7}"
              f"{'HFrat':>7}{'bright':>8}{'L/R':>6}{'refl ms':>18}")
        for row in spot_rows:
            for side in ("reference", "test"):
                measures = row[side]
                signal = measures.get("signal") or {}
                response = measures.get("response") or {}
                reflections = ",".join(f"{r['ms']:g}" for r in response.get("reflections", [])[:3])
                print(f"   {row['label'] if side == 'reference' else '':<14}{'ref' if side == 'reference' else 'test':>4}"
                      f"{fmt(signal.get('rms_db'), '', 1):>8}{fmt(response.get('direct_to_reverb_db'), '', 1):>8}"
                      f"{fmt(response.get('early_db'), '', 1):>8}{fmt(response.get('late_db'), '', 1):>8}"
                      f"{fmt(response.get('edt')):>7}{fmt(response.get('t20')):>7}{fmt(response.get('hf_ratio')):>7}"
                      f"{fmt(response.get('tail_brightness_db') if response else signal.get('tail_brightness_db'), '', 1):>8}"
                      f"{fmt(signal.get('tail_correlation')):>6}{reflections:>18}")
            bus = row["test"].get("bus_reverb_to_dry_db")
            if bus is not None:
                print(f"   {'':<14}{'':>4}  test's reverb bus against its dry bus: {bus:.1f} dB")
        print("   suggestions:")
        for line in suggestions(spot, spot_rows, environment):
            print(f"    - {line}")
        print()
    print("level: RMS dBFS of the first 0.5 s; D/R: the direct sound against everything after it, dB; early and")
    print("late: the first 80 ms after the direct sound and the rest, each against the direct sound, dB; EDT and")
    print("T20 in seconds; HFrat: T20 at 5 kHz over T20 at 1 kHz; bright: the tail above 4 kHz against 0.5-2 kHz,")
    print("dB; L/R: the tail's left-right correlation; refl: the response's first peaks after the direct sound.")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("reference", help="the recording to match, e.g. retail_maps-bloodgulch-xbox")
    parser.add_argument("test", help="the recording to tune, e.g. retail_maps-bloodgulch-pc")
    parser.add_argument("--json", help="also write every measure to this file")
    args = parser.parse_args()
    reference, test, rows = compare(args.reference, args.test)
    if not rows:
        raise SystemExit("no sound started in both recordings")
    print_report(reference, test, rows)
    if args.json:
        Path(args.json).write_text(json.dumps(rows, indent=1, default=str))


if __name__ == "__main__":
    sys.exit(main())
