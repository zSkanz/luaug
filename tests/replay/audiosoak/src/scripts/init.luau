--!strict
--- M6's audio gate: sixty seconds of continuous mixing, and a buffer underrun
--- counter that has to still be zero at the end.
---
--- **The counter is this engine's own.** The roadmap asks for "buffer underrun
--- counter zero in a 60 s soak"; the string `underrun` appears six times in
--- ninety-five thousand lines of miniaudio and every one of them is a comment or
--- an ALSA log line. So `engine/audio` defines one and defines what it counts: a
--- callback that ran and found the main thread had not produced a frame for it.
---
--- **This half runs headless with the null backend, which is the CI half.** No
--- device opens, so no callback runs, so the counter is trivially zero — and
--- that is worth saying out loud rather than pretending otherwise. What this run
--- really gates is the other, harder half: **the SIM timeline.** A sound's
--- `TimePosition` advances by `FixedTimestep * PlaybackSpeed` per tick and
--- `Ended` fires from that timeline rather than from the mixer, so an hour of
--- ticks has to produce an exact, predictable number of endings — on a machine
--- with speakers, on one without, and in a replay. Getting that wrong is what a
--- mixer-driven timeline looks like, and this is the run that would catch it.
---
--- The real-device half is a windowed run of this same scene, recorded in the
--- gate record by hand: only a machine with an audio device can say the device
--- opened.

local AudioService = game:GetService("AudioService")
local PhysicsService = game:GetService("PhysicsService")
local RunService = game:GetService("RunService")

--- One second at the default rate, and the number every expectation below is
--- derived from rather than a literal: the suite stays correct at 30 Hz or
--- 240 Hz (§3.2).
local TicksPerSecond = 1 / PhysicsService.FixedTimestep

--- A sound that ends and is replayed by its own handler. Sixty of these a minute
--- is the churn the soak is really about: a voice allocated, mixed, retired and
--- allocated again, sixty times, without the pool leaking or the counter moving.
local function beeper(name: string, seconds: number, volume: number): Sound
    local sound = Instance.new("Sound")
    sound.Name = name
    sound.Volume = volume
    -- `Content` is stored and not read in v1 (M7), so this plays a generated
    -- tone of its declared length. The LENGTH is what the timeline uses, and the
    -- timeline is what this gate is about.
    sound.PlaybackSpeed = 1 / seconds
    sound.Parent = AudioService
    return sound
end

local short = beeper("Short", 0.5, 0.2)
local medium = beeper("Medium", 1.0, 0.2)
local long = beeper("Long", 2.0, 0.2)

--- A looped one, which never ends and therefore never retires its voice: the
--- other half of the churn is a voice that is held for the whole hour.
local held = beeper("Held", 4.0, 0.15)
held.Looped = true

--- A positional sound, so the 3D path is mixed too rather than only the 2D one.
local emitter = Instance.new("Part")
emitter.Name = "Emitter"
emitter.Anchored = true
emitter.Position = vector.create(6, 0, -4)
emitter.Parent = workspace

local positional = Instance.new("Sound")
positional.Name = "Positional"
positional.Volume = 0.2
positional.PlaybackSpeed = 1 / 1.5
positional.Looped = true
positional.Parent = emitter

local endings = 0

for _, sound in { short, medium, long } do
    sound.Ended:Connect(function()
        endings += 1
        -- Straight back on, and REWOUND first. `Play` resumes from wherever
        -- `TimePosition` is -- that is what makes it the counterpart of `Pause`
        -- -- so a sound replayed from its own `Ended` handler without a rewind
        -- resumes at the end and ends again on the very next tick, forever. A
        -- voice retired and immediately re-allocated is the pattern that finds a
        -- pool which does not release, and this is how a real game writes it.
        sound.TimePosition = 0
        sound:Play()
    end)
end

short:Play()
medium:Play()
long:Play()
held:Play()
positional:Play()

local elapsedTicks = 0

RunService.Heartbeat:Connect(function()
    elapsedTicks += 1
    if elapsedTicks ~= 3595 then
        return
    end

    -- Sixty seconds of a half-second sound is 120 endings, of a one-second sound
    -- 60, and of a two-second one 30. Written as arithmetic rather than as three
    -- literals, because the number the gate asserts should say WHY it is that
    -- number.
    local seconds = elapsedTicks / TicksPerSecond
    local expected = math.floor(seconds / 0.5) + math.floor(seconds / 1.0) + math.floor(seconds / 2.0)

    -- Five per cent, and the slack is a known quantity rather than a shrug:
    -- `Ended` is a DEFERRED signal, so the handler that rewinds and replays runs
    -- a tick after the sound finished, and every cycle costs that tick. Over a
    -- minute the half-second sound loses about four of its hundred and twenty
    -- endings to it. What this still catches is a timeline that is wrong by a
    -- factor -- driven by the mixer, by the wall clock, or not scaled by
    -- `PlaybackSpeed` at all -- which is what the gate is for.
    local underruns = game:GetService("DebugService"):GetStat("AudioUnderruns")
    local clean = underruns == 0 and math.abs(endings - expected) <= expected * 0.05

    -- Printed either way. The windowed half of this gate is a human running the
    -- same scene on a machine with speakers, and the number it has to report is
    -- this line.
    local voices = game:GetService("DebugService"):GetStat("AudioVoices")
    local line = `audio soak: {seconds}s, {endings} endings against {expected} expected, `
        .. `{underruns} underruns, {voices} voices`
    if clean then
        print(line)
    else
        warn(line)
    end
    -- The fact the scenario requires. Set once, on the tick the soak is over,
    -- rather than on the way out -- a run that never got there cannot claim it.
    game:SetAttribute("AudioSoakClean", clean)
end)
