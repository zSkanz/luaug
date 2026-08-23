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

### The limit to know about

**Every sound's simulation timeline is one second long, whatever the file
actually is.**

That is a real constraint of this release, and it has three visible effects:

- `Sound.Ended` fires one second after `Play` at normal speed, whatever the
  clip.
- A `Looped` sound wraps at one second, so **only the first second of a longer
  file is ever heard on loop**.
- A file shorter than a second goes quiet at its real end but `Ended` still
  waits for the full second.

Decoding is real — the file is read and played — but the timeline the engine
counts against is not yet taken from it.

## Formats, and the placeholder tone

WAV, MP3, FLAC and Ogg Vorbis, decoded once and cached: a sound does not re-read
its file sixty times a second.

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
```

## Voices

Sixty-four at once. Past that the **quietest** are dropped rather than the
newest — a footstep lost under an explosion is the right thing to lose.

`Sound.Loaded` fires on the first tick after creation, unconditionally, whether
or not the file decoded. It is declared now so that code written today does not
change when it becomes meaningful.

## Where to look next

- [Groups, mixing and the listener](manual:audio/mixing)
- [`Sound`](api:Sound)
