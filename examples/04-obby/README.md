# `examples/04-obby` — the M6 deliverable

A game. Not a demonstration of a system: a course with a start and a finish, a
menu that tweens in front of it, a HUD that says where you are, platforms that
move because a tween drives them, sounds that mark what happened, and a character
with something animated riding on it.

All five of M6's systems are load-bearing here, and that is the point — each of
them is easy to certify against itself, and this is what makes them certify
against each other.

Run it:

```
luaug-host examples/04-obby                     # windowed
luaug-host examples/04-obby --headless --frames=200 --exit --screenshot=out.png
```

## Controls

| Key | What it does |
|---|---|
| `Return` or `Space` | dismiss the menu (or click **PLAY**) |
| `W` `A` `S` `D` | walk, relative to where the camera is looking |
| `Space` | jump, when grounded |
| `Left` `Right` | turn the camera |
| `Up` `Down` | raise and lower it |
| the HUD's **JUMP** button | the same jump, through the virtual input seam |

## The course

Three hops with growing gaps, a hazard beside the path rather than across it, a
lift, a ferry, and a finish pad. Two checkpoints. Falling below −18 m puts you
back at the last one.

The lift and the ferry end where the ledges they serve begin, so boarding either
is a step rather than a jump: a moving platform you have to time a leap onto is a
different game from the one this example is trying to be.

## What to read it for

**The course is `Part`s, and they are solid.** That is new at M6 — the same scene
a milestone ago was a wireframe, because `Part` had no solid render path at all.

**The platforms are tweened `CFrame`s on anchored parts**, which is exactly the
kinematic case: the solver sees a body it does not integrate, moved between
ticks, and a character standing on one is carried by the contact rather than by a
script reparenting it. The roadmap called this a deliberate integration stressor
and it is. `RepeatCount = -1` with `Reverses = true` is what makes a platform
shuttle forever without a handler, and `Sine`/`InOut` is what makes it ridable —
a platform that reversed at full speed would shear a character off it.

**The menu arrives with `Back`/`Out`**, which overshoots. That is the reason the
easing families are checked against a reference table rather than approximated:
an overshoot that is nearly right looks like a bug.

**The HUD's jump button drives a real `InputAction`.** It writes
`Enum.KeyCode.Virtual1` through `InputService:SetVirtualState`, and an ordinary
`InputBinding` on the same action names that key. Same context, same clock, same
sinking, and in the recorded input stream — which is why the gate below can see
it. It is the roadmap's proving caller for "an action must be drivable by
something that is not a physical device", and it is useful on a desktop on its
own.

**The sounds are on the SimClock**, so `Ended` lands on the same tick in a replay
as it does live. `Sound.Content` is stored and not yet read (M7 is the asset
pipeline), so each plays a generated tone of its declared length — enough to hear
that the right thing happened at the right moment, and the honest state of the
audio path in v1.

**The animation is a skinned glTF welded to the character.** `AnimationPlayer`
under the `MeshPart`, one `AnimationTrack`, loaded once and kept — and nothing
per frame. The rig is two joints and a one-second clip, which is small enough
that you can read the pose in the capture golden.

**Checkpoints are `Touched` handlers on pads you stand on**, and the finish is a
pad rather than a pole. `Touched` fires for the surface under a character's feet
and not for a wall it presses against — D028 carries the remainder — so a finish
line has to be something you step on. The pole behind it is what you aim at.

## The gate

`tests/replay/obby` drives this example from a recorded input stream, headless,
and requires the run to end having set `ObbyFinished`. It is M6's end-to-end gate
and the whole stack is in the loop: input dispatch, UI layout and hit-testing,
tweens driving kinematic platforms, physics, audio and animation.

A determinism hash alone could not gate this — three runs that all fall in the
same hole agree perfectly — so the scenario names the fact and the example sets
it.

The recording is authored rather than captured, and its timings are arithmetic
rather than taste: the character walks 5 m/s, which is one metre every twelve
ticks, so each jump is taken exactly one metre before a platform's edge. The file
says so in its own comments.
