# Tweens

A tween moves a property to a goal over time, with an easing curve. It exists so
that "move this over half a second, easing out" is one call rather than a
`Heartbeat` handler with a timer in it — and so the engine, not the game, owns
the arithmetic that makes two such animations agree.

```luau
--!strict
local TweenService = game:GetService("TweenService")

local info = TweenInfo.new(0.6, Enum.EasingStyle.Back, Enum.EasingDirection.Out)
local slideIn = TweenService:Create(menu, info, {
    Position = UDim2.new(0.5, 0, 0.5, 0),
})

slideIn:Play()
```

## Created stopped

`TweenService.Create` returns a `Tween` that has not started. `Tween.Play`
starts it.

**Every goal is validated at `Create`, not at the first write.** A property the
class does not have, a read-only one, or a value of the wrong type raises
*there*, where the caller is. The alternative is a tween that plays for half a
second and then reports a typo.

**Only interpolable properties are accepted**: numbers, vectors, `CFrame`s,
`Color3`s, `Vector2`s, `UDim`s, `UDim2`s and `Rect`s. A boolean or a string goal
raises, because there is no halfway between two of either.

`CFrame` goals interpolate through `CFrame:Lerp` rather than component-wise, so
a rotation takes the short way round.

## TweenInfo

```luau
TweenInfo.new(time, easingStyle, easingDirection, repeatCount, reverses, delayTime)
```

| Field | Default | Means |
|---|---|---|
| `TweenInfo.Time` | 1 | Seconds for **one** traversal, not counting delay, repeats or the return half of a reverse. |
| `TweenInfo.EasingStyle` | `Quad` | The curve. |
| `TweenInfo.EasingDirection` | `Out` | Which end eases. |
| `TweenInfo.RepeatCount` | 0 | How many **extra** traversals follow the first. |
| `TweenInfo.Reverses` | `false` | Whether each traversal is followed by itself backwards. |
| `TweenInfo.DelayTime` | 0 | Seconds before the first traversal, **and before each repeat**. |

The defaults are a one-second Quad-Out with no repeat, no reverse and no delay,
which is the tween most UI wants.

A negative `Time` or `DelayTime` raises. A negative `RepeatCount` means **repeat
forever** — the one negative number here that means something.

`RepeatCount = 2` plays three times. A reversing tween takes twice `Time` per
repeat and ends where it started. Nothing is written during a delay: the
property keeps whatever it had.

The clock is the simulation clock, the same seconds `task.wait` counts, so a
tween is a whole number of ticks and a replay reproduces it exactly.

## Easing

`Enum.EasingStyle`: `Linear`, `Sine`, `Quad`, `Cubic`, `Quart`, `Quint`,
`Exponential`, `Circular`, `Back`, `Bounce`, `Elastic`.

`Enum.EasingDirection`: `In` eases the start, `Out` eases the end — **the usual
choice** — and `InOut` eases both, the `In` curve over the first half mirrored
over the second. `Linear` is the only style whose direction changes nothing.

Two styles leave the 0-to-1 range on purpose. `Back` overshoots the far end and
comes back; `Elastic` oscillates past both ends before settling. A property
tweened with either has to tolerate values outside the range you named.

`TweenService.GetValue` exposes the same arithmetic, so a game can ease
something the tween system does not own. Its `alpha` is not clamped.

## Playing, pausing, cancelling

There are exactly three methods and there is no `Stop`.

`Tween.Play` starts it, or resumes a paused one from where it stopped. **Playing
a tween that is already playing is a no-op, not a restart** — a `Play` that
silently rewound would make a hover effect stutter every frame the pointer
moved. Playing a tween that has *finished* or been cancelled does start it over
from the property's current value.

`Tween.Pause` stops it where it is, leaving the property at its current value.

`Tween.Cancel` ends it early. **The property keeps whatever value it had
reached: cancelling is not undoing**, and a tween that snapped back would be
impossible to interrupt gracefully.

## Knowing it finished

```luau
slideIn.Completed:Connect(function(state: EnumPlaybackState)
    if state == Enum.PlaybackState.Completed then
        menu.Visible = false
    end
end)
```

`Tween.Completed` is a **property holding a signal**, not an event, because a
`Tween` is a value type and only classes declare events. It behaves like any
other signal.

The distinction it carries matters more than it looks: `Completed` and
`Cancelled` are different facts, and code that awards a checkpoint on a finished
animation must not award it on an interrupted one.

`Enum.PlaybackState`: `Begin` (created, never played), `Delayed` (playing,
inside its delay, writing nothing), `Playing`, `Paused`, `Completed`,
`Cancelled`.

**`Completed` means the goal values were written exactly, not approached.** A
tween that stopped at 0.9999 of the way would leave a property somebody has to
explain.

## Things worth knowing

- **Start values are captured at the first tick that writes**, not at `Create`.
  A tween created now and played in three seconds moves from where the property
  *is* then. They are re-captured at every repeat.
- **A tween writes through the same property setter a script writes through**,
  so a tweened value fires `Instance.GetPropertyChangedSignal`, is filtered when
  it does not change, and is refused when it is out of range — exactly as an
  assignment is.
- **A zero-length tween is legal** and lands on its goal in one tick. That is
  "set this, but through the tween system so `Completed` fires".
- **A tween on a destroyed instance stops and reports `Cancelled`** rather than
  raising.
- Two tweens writing the same property do not fight predictably in your favour —
  the later one wins that tick, deterministically, but that is a design smell
  rather than a feature.

## Where to look next

- [Skeletal animation](manual:animation/skeletal)
- [`TweenService`](api:TweenService) · [`TweenInfo`](api:TweenInfo) ·
  [`Tween`](api:Tween)
