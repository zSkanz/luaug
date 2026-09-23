# Sounds

A `Sound` is one sound, playing or not.

```luau
--!strict
local sound = Instance.new("Sound")
sound.Content = "asset://audio/engine.ogg"
sound.Volume = 0.4
sound.Looped = true
sound.Parent = part          -- a BasePart: this makes it positional
sound:Play()
```

## Where you parent it is the 3D switch

> **Parent it to a `BasePart` and it is positional. Parent it anywhere else and
> it is 2D.**

That is the whole of it, and it is a property of where the instance sits rather
than a flag that could disagree with it. The check is the **direct parent**, not
an ancestor walk — a `Sound` under a `Folder` under a part is 2D.

For a 2D sound, `AudioService` is the natural parent.

## Playing and stopping

| Method | Does |
|---|---|
| `Sound.Play` | Starts from `Sound.TimePosition`. |
| `Sound.Pause` | Stops the timeline where it is; `Play` resumes from there. |
| `Sound.Stop` | Stops **and rewinds to the start**. |

`Play` on a sound that is already playing is a **no-op, not a restart**.
Restarting is `TimePosition = 0` then `Play()`.

`Sound.Stop` does **not** fire `Sound.Ended`: `Ended` is a past-tense fact about
reaching the end, and code that awards something when a jingle finishes must not
be fooled by one that was cut off.

`Sound.Playing` is a property, and writing it is the same as calling `Play` or
`Stop`.

## The properties

| Property | Default | Notes |
|---|---|---|
| `Sound.Content` | `""` | An `asset://` URI. |
| `Sound.Volume` | 0.5 | Half rather than full, so a game with several sounds does not clip. |
| `Sound.Looped` | `false` | A looped sound never fires `Ended`. |
| `Sound.PlaybackSpeed` | 1 | 2 is an octave up and half the duration. **Zero is refused**, not treated as pause. |
| `Sound.TimePosition` | 0 | Writable — this is how a script seeks. |
| `Sound.RollOffMinDistance` | 8 | Metres. Full volume inside it. Ignored for a 2D sound. |
| `Sound.RollOffMaxDistance` | 80 | Metres. Silent beyond. |
| `Sound.Group` | `nil` | An `AudioGroup` to mix through. |

## Attenuation

Linear between the two distances: full volume inside the minimum, silent past
the maximum, straight line in between.

Linear rather than inverse-square because a game's audible range is a design
decision rather than a physical one — an inverse square makes the far half of a
range inaudible.

**There is no panning.** Spatialization in this release is distance attenuation
only: no stereo image, no Doppler, no occlusion. The listener's position is used
and its orientation is not.

## The timeline is the simulation's

`Sound.TimePosition` advances by the fixed timestep times `PlaybackSpeed`, once
per tick, and `Sound.Ended` fires from that timeline. So a headless run with no
audio device produces the same `Ended` on the same tick as a run with speakers,
and a replay reproduces both exactly.

What the speakers do is downstream of the simulation and never an input to it.

### How long a sound is

**A sound is as long as its file.** `Sound.Ended` fires when `TimePosition`
reaches the file's length, and a `Looped` sound wraps there.

The length is read from the file's header, not from decoding it, so the tick
that plays a sound never waits for a decode. The header is part of the file's
bytes, so every machine reads the same length and a replay is exact. A format
that declares no length is decoded once to count it.

## Formats, memory, and the placeholder tone

WAV, MP3, FLAC and Ogg Vorbis. A sound does not re-read its file sixty times a
second:

- **Up to ten seconds, a file is decoded once and held.** That covers effects
  and short ambiences, and playing one is a copy.
- **Longer than that, a file streams.** Its encoded bytes are kept, and each
  voice playing it decodes a few milliseconds ahead of the speakers. Three
  minutes of music costs the size of the file rather than about 70 MB.
  Seeking with `TimePosition` and looping both work the same way on a stream.

Sounds authored in a scene are fetched in the background from the first frame
the scene is alive, so they are ready before anything plays them.

**A URI that names nothing still plays**, as a generated tone whose pitch comes
from a hash of the id — deliberately, so that a missing asset is audible rather
than silent. The same happens for a file that exists and cannot be decoded, with
a warning.

Since a person listening on laptop speakers often cannot tell, there is a
number that can:

```luau
--!strict
local DebugService = game:GetService("DebugService")
print(DebugService:GetStat("AudioClipsLoaded"), DebugService:GetStat("AudioClipsMissing"))
print(DebugService:GetStat("AudioClipsStreamed")) -- of the loaded ones, how many stream
```

## Voices

Sixty-four at once. Past that the **quietest** are dropped rather than the
newest — a footstep lost under an explosion is the right thing to lose.

`Sound.Loaded` fires on the first tick after creation, unconditionally. It
means "this sound exists and names its content", not "the bytes have arrived":
waiting for the bytes would make the tick it fires on depend on the disk, and a
replay has to reproduce it exactly.

## Where to look next

- [Groups, mixing and the listener](manual:audio/mixing)
- [`Sound`](api:Sound)
