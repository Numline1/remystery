# Sound sources

Every sound in ReMystery comes from a recording dedicated to the public domain under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). CC0 needs no credit, but the authors deserve it.

| Sound | Recording | Author | License |
|---|---|---|---|
| Organ cue | [VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE), `Keys/Organ/Loud` pipe organ samples | Simon Dalzell (Ivy Audio), Versilian Studios | CC0 1.0 |
| Door creak | [Door Creak](https://freesound.org/people/stib/sounds/346267/) | stib | CC0 1.0 |
| Owl | [screech_owl.wav](https://freesound.org/people/mael46/sounds/573771/) | mael46 | CC0 1.0 |
| Howl | [Howling wolves](https://freesound.org/people/Kingcornz/sounds/378334/) | Kingcornz | CC0 1.0 |

## How the sounds are made

`build.py` downloads the recordings, processes them and embeds the results as MP3 data in `src/index.html`.

- **Organ cue.** The original screensaver's chord progression is played on the sampled pipe organ: D minor, D7, D9, A9, E7, then a long F♯9 that fades out. Each note uses the nearest sampled pipe, shifted by at most a semitone, and ends on the pipe's own recorded release. Notes shared by neighbouring chords are held, a light hall reverb is added, and the pedal is brought forward.
- **Creak, owl and howl.** Each is trimmed to roughly the original's length, filtered and normalized. The howl is pitched down by a third into the original's range.

To rebuild them you need Python 3 with numpy, and ffmpeg built with libmp3lame:

```bash
python3 tools/sounds/build.py
```

Downloads are cached in `tools/sounds/.cache/`.
