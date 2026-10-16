# The original scene

This page describes how the Windows 98 Mystery screensaver behaves and how ReMystery reproduces it. The behaviour was studied from the original `Mystery.dll` scene data, which holds object tables, animation tracks and timing scripts. None of the original files, images or sounds are part of this repository.

## Layout

The original draws its sprites at their native size and anchors each object to an edge or the centre of the screen. The house hangs from the bottom centre, the trees from the bottom corners, and the moon from the top left. Its scene was composed for a 1152 × 864 screen.

ReMystery keeps those anchors and offsets in the original's units. It scales one unit so that the 1152 × 864 composition fits the screen. On wider screens the trees move out to the edges and more sky shows. On portrait screens the house stays at the bottom.

Objects are drawn in this order, as in the original:

1. Stars, then the moon.
2. Leaf litter, then the house.
3. Lit windows, the door, the study and the cat.
4. The trees and the owl, then the bats.

## Timing

The original engine runs on a 55 ms tick. Every object follows a script of steps such as "show this animation for N frames" or "wait a random time". The table lists the resulting behaviour.

| Object | Behaviour |
|---|---|
| Moon | Moves one unit every 385 ms, left to right. After leaving the screen it waits 2 s and re-enters on the left. |
| Organ | Plays when the moon starts each pass, so at start-up and then every 8 to 10 minutes. |
| Front door | Closed for 11 s. Then it creaks, swings open and shuts again over 2 s, and stays closed for 50 to 70 s before repeating. |
| Study | Dark for 27.5 s. The candle lights up over 0.7 s and flickers for 1.1 s, then the room fades back to dark. |
| Owl | Blinks, watches for 27.5 s, blinks again and calls. One cycle takes about 33 s. |
| Bats | The first bat appears after 15 s and flies left to right. The second appears after 20 to 40 s and flies right to left. Each waits 20 to 40 s off-screen between passes. |
| Cat | First walks along the bottom after 90 s. It crosses in about 9 s, then waits up to 90 s. |
| Stars | Each star blinks off and on, on its own cycle of 5 to 40 s. |
| Howl | Tied to one star's 11-minute cycle. |

Each window waits for a start delay, then alternates between lit and dark periods.

| Window | Start delay | Lit for | Dark for |
|---|---|---|---|
| Lower right, triple | lit at start | 5–10 s | 110–220 s |
| Upper left | 10 s | 55–110 s | 55–110 s |
| Upper right | 15–20 s | 55–82 s | 82–165 s |
| Left wing | 20–30 s | 55–82 s | 27–55 s |
| Right wing | 2–8 s | 110–220 s | 55–110 s |
| Left dormer | 15 s | 27–55 s | 82–137 s |
| Right dormer | 5 s | 55–137 s | 110–137 s |

## Sound

The original has four sound cues.

| Cue | Length | Description |
|---|---|---|
| Organ | 7.4 s | A pipe-organ progression of D minor, D7, D9, A9 and E7, ending on a long F♯9 chord that fades out |
| Creak | 1.9 s | A squeaky hinge that swells and fades |
| Owl | 0.9 s | Quick rising chirps, a held whistle and a falling squeal |
| Howl | 2.3 s | A smooth tone falling from about 490 Hz to 400 Hz |

ReMystery synthesizes new versions of all four, using the same notes, pitch contours and lengths.
