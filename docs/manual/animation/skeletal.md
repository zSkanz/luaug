# Skeletal animation

An `AnimationPlayer` plays a skinned mesh's clips.

```luau
--!strict
local player = Instance.new("AnimationPlayer")
player.Parent = character      -- the MeshPart whose skeleton the clips belong to

local walk = player:LoadAnimation("Walk")
walk.Looped = true
walk:Play(0.2)                 -- fade the weight in over 0.2 s
```

**Parent it to the `MeshPart` whose skeleton the clips belong to.** That is
where it looks for them, and an `AnimationPlayer` parented anywhere else finds
nothing rather than guessing.

It stores nothing of its own: **the tracks are the state**, and each one is a
handle a script holds rather than a child in the tree.

## Naming a clip

`AnimationPlayer.LoadAnimation` accepts three spellings, and the common case is
the first:

```luau
player:LoadAnimation("Walk")                        -- a clip in this player's own mesh
player:LoadAnimation("#Walk")                       -- the same thing
player:LoadAnimation("asset://models/hero.glb#Walk") -- explicit, same file
```

Everything after a `#` is the clip's **name inside the file**. A path before the
`#` must be the mesh this player is under: a clip is not addressable on its own
in this release, so it exists only inside the file its skeleton came from, and a
URN naming a different file loads nothing rather than playing the wrong rig.

**Load a track once and keep it.** `LoadAnimation` always returns a track, even
for a clip that is not there — a mesh that has not finished loading would
otherwise make an ordinary frame a nil index.

The readiness check is `AnimationTrack.Length`, which is zero for a clip that
was not found:

```luau
--!strict
local RunService = game:GetService("RunService")

local started = false
RunService.Heartbeat:Connect(function()
    if started then
        return
    end
    local track = player:LoadAnimation("Bend")
    if track.Length > 0 then
        track.Looped = true
        track:Play(0.2)
        started = true
    end
end)
```

## The track

`AnimationTrack` is a handle, like `Tween` — nothing parents one and nothing
finds one in the tree. It is also **the one mutable datatype**, and for a
reason: `track.Looped = true` names the one track every holder of that handle
can see, where writing to a `CFrame` would write to a copy you are about to
drop.

| Member | Access | Notes |
|---|---|---|
| `AnimationTrack.Playing` | read-only | Whether the clock is advancing. |
| `AnimationTrack.Looped` | read/write | A looped track never fires `Ended`. |
| `AnimationTrack.Speed` | read/write | 2 is twice the speed and half the duration; **0 holds the pose**. |
| `AnimationTrack.Weight` | read/write | How much it contributes when several drive one skeleton. |
| `AnimationTrack.Length` | read-only | Seconds, or zero for a clip that was not found. |
| `AnimationTrack.TimePosition` | read-only | Where the clock is. |
| `AnimationTrack.Ended` | read-only | A property holding a signal. |

`Speed = 0` is the honest way to freeze a frame. `TimePosition` is read-only in
this release: seeking a blended pose is a different feature from playing one,
and a writable seek that ignored the blend would be the wrong half of it.

## Play and Stop

`AnimationTrack.Play` starts the clip **from the beginning, every time** — the
opposite of `Tween.Play`, and deliberately: a jump animation triggered twice
should play twice.

Its optional `fadeTime` fades the weight in from zero over that many seconds,
which is what makes a walk blend into an idle rather than snapping. It fades in
**to this track's own `Weight`**, which is why one argument is enough.

`AnimationTrack.Stop` ends the track, optionally fading its weight out first.
**`Ended` does not fire either way** — it is a past-tense fact about reaching the
end, and code that chains the next animation on it must not be fooled by one
that was interrupted.

A track that reaches the end of a non-looped clip **holds its last frame** until
it is stopped or replayed. `Ended` is deferred, and a character that returned to
its rest pose for the one tick before the handler ran would be a visible pop.

## Blending

Set `Weight` on several tracks and they blend. Assigning it is **immediate** —
the faded form is `Play`'s and `Stop`'s argument, and a property that took a
fade time would not be a property.

**Blending is a weighted average per joint and per component.** A joint no clip
drives keeps its rest transform rather than being dragged towards the origin by
tracks that say nothing about it.

The blend order is the order tracks were loaded, which is stated rather than
left to a container: two tracks at weight 0.5 have to blend the same way on
every run.

Two `AnimationPlayer`s under one mesh blend into one pose.

## The clock

Sampling happens at `RunService.PreAnimation`, on the simulation clock, so a
clip's position at a given tick is the same in a replay as it was live. A looped
track wraps keeping the fraction of a tick it overshot by, so a loop does not
drift against everything else in the scene.

## What is not here

Clip playback and linear blending, and that is the whole feature. No state
machines, no inverse kinematics, no root motion, no additive or masked blending,
no retargeting, and no physics on a skinned mesh.

## Where to look next

- [Tweens](manual:animation/tweens) — for animating a property rather than a rig
- [Meshes and models](manual:world/meshes)
- [`AnimationPlayer`](api:AnimationPlayer) · [`AnimationTrack`](api:AnimationTrack)
