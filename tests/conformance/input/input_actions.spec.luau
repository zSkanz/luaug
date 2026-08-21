--!strict
--- The Input Action System's script-facing surface, from api-design.md §2.4 and
--- ADR 0029.
---
--- What these cases can and cannot see: a headless conformance run has no
--- device, so nothing here presses a key. What it CAN check is the whole of the
--- surface a game writes against -- the tree shape, the property domains, the
--- refusals, and the two methods -- plus the one behavioural fact that needs no
--- input at all: an action nothing drives reads as its type's resting value.
---
--- The behaviour under real input is covered by the seam tests, which can
--- synthesize device events, and end to end by the recorded input replay.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local InputService = game:GetService("InputService")

describe("InputContext", function()
    local context: InputContext = nil :: any

    beforeEach(function()
        context = Instance.new("InputContext")
    end)

    afterEach(function()
        context:Destroy()
    end)

    it("starts enabled, unsinking, at priority zero and on the simulation clock", function()
        -- The default is the safe one: an action nobody thought about fires on
        -- the sim tick, where the replay can see it (ADR 0039).
        expect(context.Enabled):ToBe(true)
        expect(context.Sink):ToBe(false)
        expect(context.Priority):ToBe(0)
        expect(context.Rate):ToBe(Enum.InputRate.Simulation)
    end)

    it("round-trips its properties", function()
        context.Enabled = false
        context.Sink = true
        context.Priority = 10
        context.Rate = Enum.InputRate.Render
        expect(context.Enabled):ToBe(false)
        expect(context.Sink):ToBe(true)
        expect(context.Priority):ToBe(10)
        expect(context.Rate):ToBe(Enum.InputRate.Render)
    end)

    it("refuses an item from another enum for Rate", function()
        expect(function()
            (context :: any).Rate = Enum.InputActionType.Bool
        end):ToThrow()
    end)
end)

describe("InputAction", function()
    local context: InputContext = nil :: any
    local action: InputAction = nil :: any

    beforeEach(function()
        context = Instance.new("InputContext")
        action = Instance.new("InputAction")
        action.Parent = context
    end)

    afterEach(function()
        context:Destroy()
    end)

    it("is a Bool action by default", function()
        expect(action.Type):ToBe(Enum.InputActionType.Bool)
        expect(action.Enabled):ToBe(true)
    end)

    it("reports the resting value of its own type", function()
        -- Nothing is pressed in a headless run, which is what makes this the
        -- one behavioural case a conformance spec can make: each type has to
        -- answer in its own currency even when nothing is driving it.
        expect(action:GetState()):ToBe(false)

        action.Type = Enum.InputActionType.Direction1D
        expect(action:GetState()):ToBe(0)

        action.Type = Enum.InputActionType.Direction2D
        expect(action:GetState()):ToBe(Vector2.zero)

        action.Type = Enum.InputActionType.ViewportPosition
        expect(typeof(action:GetState())):ToBe("Vector2")

        action.Type = Enum.InputActionType.Direction3D
        -- Declared and not driveable in v1: no binding in §2.4's list names
        -- three axes. Zero rather than an error, so that code written today
        -- keeps meaning the same thing when it becomes driveable.
        expect(action:GetState()):ToBe(Vector3.zero)
    end)

    it("resets its value when its type changes", function()
        -- There is no honest conversion between a press and a vector, so the
        -- value goes back to rest rather than being reinterpreted.
        action.Type = Enum.InputActionType.Direction2D
        expect(action:GetState()):ToBe(Vector2.zero)
        action.Type = Enum.InputActionType.Bool
        expect(action:GetState()):ToBe(false)
    end)

    it("carries Pressed, Released and StateChanged", function()
        expect(typeof(action.Pressed)):ToBe("Signal")
        expect(typeof(action.Released)):ToBe("Signal")
        expect(typeof(action.StateChanged)):ToBe("Signal")
    end)

    it("answers GetPreferredBinding with nil when it has no binding", function()
        expect(action:GetPreferredBinding()):ToBeNil()
        expect(action:GetPreferredBinding(Enum.InputDeviceType.Gamepad)):ToBeNil()
    end)

    it("picks the first binding of the device family asked for, in child order", function()
        local keyboard = Instance.new("InputBinding")
        keyboard.KeyCode = Enum.KeyCode.E
        keyboard.Parent = action

        local pad = Instance.new("InputBinding")
        pad.KeyCode = Enum.KeyCode.ButtonSouth
        pad.Parent = action

        local secondPad = Instance.new("InputBinding")
        secondPad.KeyCode = Enum.KeyCode.ButtonNorth
        secondPad.Parent = action

        expect(action:GetPreferredBinding(Enum.InputDeviceType.KeyboardMouse)):ToBe(keyboard)
        -- First in child order, not last: a prompt glyph that changed between
        -- two frames nothing touched would be worse than a wrong one.
        expect(action:GetPreferredBinding(Enum.InputDeviceType.Gamepad)):ToBe(pad)
    end)

    it("refuses a GetPreferredBinding argument from another enum", function()
        expect(function()
            return (action :: any):GetPreferredBinding(Enum.InputRate.Render)
        end):ToThrow()
    end)
end)

describe("InputBinding", function()
    local action: InputAction = nil :: any
    local binding: InputBinding = nil :: any

    beforeEach(function()
        action = Instance.new("InputAction")
        binding = Instance.new("InputBinding")
        binding.Parent = action
    end)

    afterEach(function()
        action:Destroy()
    end)

    it("starts unbound, at scale one, with no name and no glyph", function()
        -- `Unknown` IS unbound: the value domain is a closed set and a nullable
        -- enum property is not in it, so the enum carries the item instead
        -- (ADR 0039).
        expect(binding.KeyCode):ToBe(Enum.KeyCode.Unknown)
        expect(binding.Up):ToBe(Enum.KeyCode.Unknown)
        expect(binding.Down):ToBe(Enum.KeyCode.Unknown)
        expect(binding.Left):ToBe(Enum.KeyCode.Unknown)
        expect(binding.Right):ToBe(Enum.KeyCode.Unknown)
        expect(binding.Scale):ToBe(1)
        expect(binding.DisplayName):ToBe("")
        expect(binding.Image):ToBe("")
    end)

    it("round-trips a key, the four composites and a scale", function()
        binding.KeyCode = Enum.KeyCode.Space
        binding.Up = Enum.KeyCode.W
        binding.Down = Enum.KeyCode.S
        binding.Left = Enum.KeyCode.A
        binding.Right = Enum.KeyCode.D
        binding.Scale = -2.5
        binding.DisplayName = "ui.prompt.jump"
        binding.Image = "asset://ui/glyph-space.png"

        expect(binding.KeyCode):ToBe(Enum.KeyCode.Space)
        expect(binding.Up):ToBe(Enum.KeyCode.W)
        expect(binding.Down):ToBe(Enum.KeyCode.S)
        expect(binding.Left):ToBe(Enum.KeyCode.A)
        expect(binding.Right):ToBe(Enum.KeyCode.D)
        -- Negative is legal and is the point: "invert Y" is a settings screen
        -- writing a negative scale.
        expect(binding.Scale):ToBe(-2.5)
        expect(binding.DisplayName):ToBe("ui.prompt.jump")
        expect(binding.Image):ToBe("asset://ui/glyph-space.png")
    end)

    it("derives DeviceType from KeyCode and refuses to be told otherwise", function()
        expect(binding.DeviceType):ToBe(Enum.InputDeviceType.KeyboardMouse)

        binding.KeyCode = Enum.KeyCode.ButtonSouth
        expect(binding.DeviceType):ToBe(Enum.InputDeviceType.Gamepad)

        binding.KeyCode = Enum.KeyCode.LeftThumbstick
        expect(binding.DeviceType):ToBe(Enum.InputDeviceType.Gamepad)

        binding.KeyCode = Enum.KeyCode.MouseLeft
        expect(binding.DeviceType):ToBe(Enum.InputDeviceType.KeyboardMouse)

        -- Read-only, because a settable one could disagree with the key beside
        -- it and the engine would have to choose which to believe (ADR 0039).
        expect(function()
            (binding :: any).DeviceType = Enum.InputDeviceType.Gamepad
        end):ToThrow()
    end)

    it("refuses a KeyCode that is not one", function()
        expect(function()
            (binding :: any).KeyCode = "Space"
        end):ToThrow()
        expect(function()
            (binding :: any).KeyCode = Enum.InputDeviceType.Gamepad
        end):ToThrow()
        expect(binding.KeyCode):ToBe(Enum.KeyCode.Unknown)
    end)
end)

describe("Enum.KeyCode", function()
    it("spans the keyboard, the mouse and the standard gamepad", function()
        -- One enum rather than three, because a binding names ONE input. The
        -- items a game reaches for first are spot-checked here; the whole table
        -- is walked by the seam tests, against the device layer's own names.
        expect(Enum.KeyCode.W.EnumType):ToBe(Enum.KeyCode)
        expect(Enum.KeyCode.Digit1.Name):ToBe("Digit1")
        expect(Enum.KeyCode.MouseLeft.EnumType):ToBe(Enum.KeyCode)
        expect(Enum.KeyCode.MouseMovement.EnumType):ToBe(Enum.KeyCode)
        expect(Enum.KeyCode.ButtonSouth.EnumType):ToBe(Enum.KeyCode)
        expect(Enum.KeyCode.LeftTrigger.EnumType):ToBe(Enum.KeyCode)
        expect(Enum.KeyCode.LeftThumbstick.EnumType):ToBe(Enum.KeyCode)
    end)

    it("names its gamepad face buttons by position, not by legend", function()
        -- The bottom face button is A on an Xbox pad, B on a Nintendo one and
        -- Cross on a PlayStation one. A binding saved as "A" would move when
        -- the hardware did; what a button is CALLED belongs to the prompt.
        expect(Enum.KeyCode.ButtonSouth.Name):ToBe("ButtonSouth")
        expect(Enum.KeyCode.ButtonNorth.Name):ToBe("ButtonNorth")
        -- And the legend spelling is not quietly nil: an enum raises on a
        -- member it does not have, so a binding written from Xbox muscle memory
        -- fails where it was written rather than at the first press.
        expect(function()
            return (Enum.KeyCode :: any).ButtonA
        end):ToThrow("script.err.unknown_member")
    end)

    it("carries Unknown as a real item at value zero", function()
        expect(Enum.KeyCode.Unknown.Value):ToBe(0)
    end)
end)

describe("InputService", function()
    it("starts with a visible, unlocked pointer", function()
        expect(InputService.PointerVisible):ToBe(true)
        expect(InputService.PointerLocked):ToBe(false)
    end)

    it("round-trips the two pointer properties", function()
        InputService.PointerLocked = true
        InputService.PointerVisible = false
        expect(InputService.PointerLocked):ToBe(true)
        expect(InputService.PointerVisible):ToBe(false)
        InputService.PointerLocked = false
        InputService.PointerVisible = true
    end)

    it("reports a pointer position as a Vector2", function()
        expect(typeof(InputService:GetPointerPosition())):ToBe("Vector2")
    end)

    it("reports the last device family, read-only", function()
        expect(InputService.LastInputDeviceType.EnumType):ToBe(Enum.InputDeviceType)
        expect(function()
            (InputService :: any).LastInputDeviceType = Enum.InputDeviceType.Gamepad
        end):ToThrow()
    end)

    it("carries InputDeviceChanged and WindowFocusChanged", function()
        expect(typeof(InputService.InputDeviceChanged)):ToBe("Signal")
        expect(typeof(InputService.WindowFocusChanged)):ToBe("Signal")
    end)

    it("cannot be constructed", function()
        expect(function()
            return (Instance.new :: any)("InputService")
        end):ToThrow()
    end)
end)
