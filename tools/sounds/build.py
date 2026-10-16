#!/usr/bin/env python3
"""Builds the sounds embedded in src/index.html.

Everything comes from CC0 (public domain) recordings, see SOURCES.md:
  * organ  - the opening organ cue, played on a real pipe organ (VSCO 2 CE samples)
             using the chord progression of the original screensaver
  * creak  - a squeaky door hinge
  * owl    - an eastern screech-owl trill
  * howl   - a wolf howl, pitched down to the original's range

Usage:  python3 tools/sounds/build.py      (needs numpy and ffmpeg on PATH)
Downloads are cached in tools/sounds/.cache/.
"""
import base64
import io
import os
import re
import subprocess
import urllib.request
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CACHE = os.path.join(ROOT, 'tools', 'sounds', '.cache')
PAGE = os.path.join(ROOT, 'src', 'index.html')
SR = 44100

VSCO = 'https://raw.githubusercontent.com/sgossner/VSCO-2-CE/master/Keys/Organ/Loud/'
FREESOUND = {
    'creak': 'https://cdn.freesound.org/previews/346/346267_470543-hq.mp3',  # stib, "Door Creak"
    'owl': 'https://cdn.freesound.org/previews/573/573771_8655650-hq.mp3',   # mael46, "screech_owl.wav"
    'howl': 'https://cdn.freesound.org/previews/378/378334_4804147-hq.mp3',  # Kingcornz, "Howling wolves"
}
# Sampled keys: manual file N plays MIDI note 35 + N (C2..C7), pedal file N plays 23 + N (C1..F#3).
MANUAL = {35 + n: f'Rode_Man3Open_{n:02d}.wav' for n in range(1, 62, 3)}
PEDAL = {23 + n: f'Rode_Pedal_{n:02d}.wav' for n in range(1, 32, 3)}

# The original's organ cue: D minor, D7, D9, A9, E7 and a long F#9 (seconds, pedal note, manual notes).
CHORDS = [
    (0.00, 0.47, 'D2', 'D3 A3 D4 F4 A4 D5 F5'),
    (0.47, 0.86, 'A2', 'D3 A3 C4 F#4 A4 D5 F#5'),
    (0.86, 1.72, 'D2', 'D3 A3 C4 E4 F#4 A4 E5 F#5'),
    (1.72, 2.52, 'A1', 'E3 A3 C#4 E4 G4 B4 C#5 E5'),
    (2.52, 3.38, 'E2', 'E3 B3 E4 G#4 B4 D5 G#5'),
    (3.38, 7.00, 'F#1', 'F#2 C#3 F#3 A#3 C#4 E4 G#4 A#4 C#5 F#5'),
]
SWELL = (5.4, 7.0, 0.1)   # the last chord fades from 5.4 s to 10 % at 7 s, then is released
LENGTH = 8.4

NOTE = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}


def midi(name):
    m = re.fullmatch(r'([A-G]#?)(\d)', name)
    return NOTE[m.group(1)] + 12 * (int(m.group(2)) + 1)


def fetch(url, name):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, name)
    if not os.path.exists(path):
        print('downloading', url)
        with urllib.request.urlopen(url) as r, open(path + '.part', 'wb') as f:
            f.write(r.read())
        os.replace(path + '.part', path)
    return path


def read_wav(path):
    with wave.open(path) as w:
        assert w.getsampwidth() == 2 and w.getframerate() == SR, path
        a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float32) / 32768
        return a.reshape(-1, w.getnchannels())


def decode(path, channels):
    out = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', str(channels), '-ar', str(SR), '-'],
                         check=True, capture_output=True).stdout
    return np.frombuffer(out, dtype='<f4').reshape(-1, channels).copy()


def ffmpeg_filter(x, filters):
    """Runs float audio through an ffmpeg filter chain."""
    ch = x.shape[1]
    out = subprocess.run(['ffmpeg', '-v', 'error', '-f', 'f32le', '-ac', str(ch), '-ar', str(SR), '-i', '-',
                          '-af', filters, '-f', 'f32le', '-ac', str(ch), '-ar', str(SR), '-'],
                         input=x.astype('<f4').tobytes(), check=True, capture_output=True).stdout
    return np.frombuffer(out, dtype='<f4').reshape(-1, ch).copy()


def mp3(x, bitrate):
    ch = x.shape[1]
    return subprocess.run(['ffmpeg', '-v', 'error', '-f', 'f32le', '-ac', str(ch), '-ar', str(SR), '-i', '-',
                           '-c:a', 'libmp3lame', '-b:a', bitrate, '-f', 'mp3', '-'],
                          input=x.astype('<f4').tobytes(), check=True, capture_output=True).stdout


def envelope(x, win=0.02):
    n = int(SR * win)
    e = np.sqrt(np.convolve((x ** 2).mean(1), np.ones(n) / n, mode='same'))
    return e


def resample(x, ratio, start, count):
    """Reads `count` frames from x starting at `start`, stepping `ratio` source frames per output frame."""
    pos = start + np.arange(count) * ratio
    pos = pos[pos < len(x) - 1]
    i = pos.astype(int)
    f = (pos - i)[:, None]
    return x[i] * (1 - f) + x[i + 1] * f


_samples = {}


def sample(table, key):
    if key not in _samples:
        x = read_wav(fetch(VSCO + table[key], table[key]))
        e = envelope(x)
        sustain = np.median(e[int(SR * 1):int(SR * 8)])
        release = int(SR * 8) + int(np.argmax(e[int(SR * 8):] < sustain * 0.7)) - int(SR * 0.04)
        _samples[key] = (x, release)
    return _samples[key]


def play_note(note, start, end, pedal, out):
    """Adds one held organ note: the sampled attack and sustain, then the recorded release and room tail."""
    table = PEDAL if pedal else MANUAL
    key = min(table, key=lambda k: abs(k - note))
    x, release = sample(table, key)
    ratio = 2 ** ((note - key) / 12)
    hold = int((end - start) * SR)
    body = resample(x, ratio, 0, hold)
    tail = resample(x, ratio, release, int((len(x) - release) / ratio))
    fade = int(SR * 0.06)                          # crossfade from the held sound into the release
    if len(body) > fade:
        w = np.linspace(1, 0, fade)[:, None]
        body[-fade:] = body[-fade:] * w + tail[:fade] * (1 - w)
        tail = tail[fade:]
    sound = np.concatenate([body, tail])
    at = int(start * SR)
    n = min(len(sound), len(out) - at)
    gain = 2.2 if pedal else 2.4
    out[at:at + n] += sound[:n] * gain


def room(length, seed=7):
    """A soft hall response: band-limited noise whose highs die away faster than its lows."""
    rng = np.random.default_rng(seed)
    n = int(SR * length)
    t = np.arange(n) / SR
    ir = np.zeros((n, 2))
    freqs = np.fft.rfftfreq(n, 1 / SR)
    for lo, hi, rt in [(0, 400, 2.4), (400, 1500, 2.0), (1500, 4000, 1.4), (4000, 22050, 0.7)]:
        for c in range(2):
            spec = np.fft.rfft(rng.standard_normal(n))
            spec[(freqs < lo) | (freqs >= hi)] = 0
            ir[:, c] += np.fft.irfft(spec, n) * np.exp(-6.9 * t / rt)
    ir[: int(SR * 0.015)] = 0                      # pre-delay
    return ir / np.abs(ir).max()


def convolve(x, ir):
    n = len(x) + len(ir) - 1
    size = 1 << (n - 1).bit_length()
    out = np.zeros((n, x.shape[1]))
    for c in range(x.shape[1]):
        out[:, c] = np.fft.irfft(np.fft.rfft(x[:, c], size) * np.fft.rfft(ir[:, c], size), size)[:n]
    return out


def normalize(x, peak_db):
    return x * (10 ** (peak_db / 20) / np.abs(x).max())


def fades(x, fade_in, fade_out):
    x = x.copy()
    a, b = int(SR * fade_in), int(SR * fade_out)
    if a: x[:a] *= np.linspace(0, 1, a)[:, None]
    if b: x[-b:] *= np.linspace(1, 0, b)[:, None] ** 2
    return x


def build_organ():
    out = np.zeros((int(SR * LENGTH), 2))
    # Notes shared by consecutive chords are held rather than played again.
    events = {}
    for i, (t0, t1, pedal, manual) in enumerate(CHORDS):
        for name, is_pedal in [(pedal, True)] + [(n, False) for n in manual.split()]:
            key = (midi(name), is_pedal)
            if key in events and events[key][-1][1] == t0:
                events[key][-1][1] = t1
            else:
                events.setdefault(key, []).append([t0, t1])
    for (note, is_pedal), spans in events.items():
        for t0, t1 in spans:
            note_out = np.zeros_like(out)
            play_note(note, t0, t1, is_pedal, note_out)
            if t1 == CHORDS[-1][1]:                # the swell closes on the final chord
                s0, s1, floor = SWELL
                t = np.arange(len(out)) / SR
                g = np.clip(1 - (t - s0) / (s1 - s0) * (1 - floor), floor, 1)[:, None]
                note_out *= g
            out += note_out
    wet = convolve(out, room(2.6))[:len(out)]
    mix = normalize(out + wet * 0.12, -6)
    # Full, booming pedal and an even level from the first chord on, like the original cue.
    mix = ffmpeg_filter(mix, 'bass=g=8:f=110:w=0.8,acompressor=threshold=-20dB:ratio=3:attack=8:release=250:makeup=2')
    return fades(normalize(mix, -1.0), 0, 0.3)


def build_effect(name, start, length, filters, fade_in, fade_out, peak_db):
    x = decode(fetch(FREESOUND[name], name + '.mp3'), 1)
    x = x[int(start * SR):int((start + length) * SR)]
    x = ffmpeg_filter(x, filters) if filters else x
    return fades(normalize(x, peak_db), fade_in, fade_out)


def main():
    sounds = {
        'organ': (build_organ(), '128k'),
        'creak': (build_effect('creak', 0.0, 2.2, 'highpass=f=150', 0.005, 0.35, -2), '64k'),
        'owl': (build_effect('owl', 0.3, 1.6, 'highpass=f=400,afftdn=nr=18:nf=-50', 0.05, 0.4, -3), '64k'),
        # The howl is pitched down by a third, into the original's 490-400 Hz range.
        'howl': (build_effect('howl', 0.35, 3.1, f'highpass=f=150,asetrate={SR * 0.73:.0f},aresample={SR}',
                              0.03, 0.6, -3), '64k'),
    }
    lines = []
    for name, (x, rate) in sounds.items():
        data = mp3(x, rate)
        print(f'{name}: {len(x) / SR:.2f} s, {len(data) // 1024} KB')
        lines.append(f"  {name}: '{base64.b64encode(data).decode()}',")
    block = '\n'.join(['// SOUNDS:BEGIN generated by tools/sounds/build.py from CC0 recordings (tools/sounds/SOURCES.md)',
                       'const SOUNDS = {', *lines, '};', '// SOUNDS:END'])
    page = open(PAGE, encoding='utf-8').read()
    page, count = re.subn(r'// SOUNDS:BEGIN.*?// SOUNDS:END', lambda _: block, page, flags=re.S)
    if count != 1:
        raise SystemExit('could not find the SOUNDS block in src/index.html')
    open(PAGE, 'w', encoding='utf-8').write(page)
    print('updated', os.path.relpath(PAGE, ROOT))


if __name__ == '__main__':
    main()
