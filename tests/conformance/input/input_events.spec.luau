--!strict
--- `InputService`'s raw event surface: `InputBegan`, `InputChanged`,
--- `InputEnded`, `IsKeyDown` and the `InputObject` they carry (api-design.md
--- §2.4, ADR 0041).
---
--- **The conformance run has no device**, so no event here ever fires and
--- `IsKeyDown` is false for everything. That is the honest split: what the
--- surface DOES with a real press is eleven cases in `luaug_input_tests`, which
--- can drive a device snapshot directly; what a script can see and connect to is
--- here, and it is what a game breaks against.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

local InputService = game:GetService("InputService")
local PhysicsService = game:GetService("PhysicsService")

local function step(ticks: number)
    task.wait(ticks * PhysicsService.FixedTimestep)
end

describe("InputService:IsKeyDown", function()
    it("answers false for a key nobody is holding", function()
        expect(InputService:IsKeyDown(Enum.KeyCode.Space)):ToBe(false)
        expect(InputService:IsKeyDown(Enum.KeyCode.W)):ToBe(false)
        expect(InputService:IsKeyDown(Enum.KeyCode.ButtonSouth)):ToBe(false)
    end)

    it("answers false rather than raising for Unknown", function()
        -- `Unknown` is a real item precisely so an unset binding is expressible
        -- (ADR 0039), and asking about it is a question with an answer.
        expect(InputService:IsKeyDown(Enum.KeyCode.Unknown)):ToBe(false)
    end)

    it("refuses an item from another enum", function()
        expect(function()
            return InputService:IsKeyDown(Enum.PartShape.Ball :: any)
        end):ToThrow()
        expect(function()
            return (InputService :: any):IsKeyDown("Space")
        end):ToThrow()
    end)

    it("is a snapshot, so two reads inside one tick agree", function()
        local first = InputService:IsKeyDown(Enum.KeyCode.Space)
        local second = InputService:IsKeyDown(Enum.KeyCode.Space)
        expect(first):ToBe(second)
    end)
end)

describe("InputService raw events", function()
    it("exposes all three as ordinary signals", function()
        expect(typeof(InputService.InputBegan)):ToBe("Signal")
        expect(typeof(InputService.InputChanged)):ToBe("Signal")
        expect(typeof(InputService.InputEnded)):ToBe("Signal")
    end)

    it("is the same signal object on every read", function()
        -- A script that connects in one place and disconnects in another depends
        -- on it, and this is the property that makes that work.
        expect(InputService.InputBegan):ToBe(InputService.InputBegan)
    end)

    it("connects and disconnects like any other signal", function()
        local fired = 0
        local connection = InputService.InputBegan:Connect(function()
            fired += 1
        end)
        expect(typeof(connection)):ToBe("Connection")

        step(3)
        -- Nothing fired, because nothing was pressed. A run with no device is
        -- silent rather than synthetic, which is what lets a headless gate mean
        -- something.
        expect(fired):ToBe(0)

        connection:Disconnect()
        expect(connection.Connected):ToBe(false)
    end)

    it("refuses a write, because an event is not a property", function()
        expect(function()
            (InputService :: any).InputBegan = 1
        end):ToThrow()
    end)
end)

describe("Enum.UserInputType", function()
    it("carries the kinds a raw handler switches on", function()
        expect(Enum.UserInputType.Keyboard.Value):ToBe(1)
        expect(Enum.UserInputType.MouseButton1.Value):ToBe(2)
        expect(Enum.UserInputType.MouseMovement.Value):ToBe(5)
        expect(Enum.UserInputType.MouseWheel.Value):ToBe(6)
        expect(Enum.UserInputType.Gamepad.Value):ToBe(7)
    end)

    it("carries None as the value nothing produces", function()
        -- What a default-constructed `InputObject` reports. An event that fired
        -- came from something, so seeing this in a handler means something is
        -- wrong upstream rather than that a key has no kind.
        expect(Enum.UserInputType.None.Value):ToBe(0)
    end)

    it("declares Touch without producing it, like Enum.InputDeviceType does", function()
        expect(Enum.UserInputType.Touch.Value):ToBe(8)
        expect(Enum.InputDeviceType.Touch.Value):ToBe(2)
    end)

    it("is a closed set that GetEnumItems enumerates", function()
        expect(#Enum.UserInputType:GetEnumItems()):ToBe(9)
    end)
end)

describe("InputService:SetVirtualState", function()
    it("refuses a key code that is not virtual", function()
        -- Writing to `Space` would be a script pretending to be a keyboard, and
        -- nothing downstream could tell the two apart afterwards.
        expect(function()
            InputService:SetVirtualState(Enum.KeyCode.Space, 1)
        end):ToThrow()
        expect(function()
            InputService:SetVirtualState(Enum.KeyCode.LeftThumbstick, 1)
        end):ToThrow()
        expect(function()
            InputService:SetVirtualState(Enum.PartShape.Ball :: any, 1)
        end):ToThrow()
    end)

    it("accepts the four channels and reads back through IsKeyDown", function()
        InputService:SetVirtualState(Enum.KeyCode.Virtual1, 1)
        expect(InputService:IsKeyDown(Enum.KeyCode.Virtual1)):ToBe(true)

        -- A value, not a press: below half deflection is not down, which is the
        -- rule every analogue source here follows.
        InputService:SetVirtualState(Enum.KeyCode.Virtual1, 0.4)
        expect(InputService:IsKeyDown(Enum.KeyCode.Virtual1)):ToBe(false)

        InputService:SetVirtualState(Enum.KeyCode.Virtual1, 0)
        expect(InputService:IsKeyDown(Enum.KeyCode.Virtual1)):ToBe(false)
    end)

    it("drives a real action through an ordinary binding", function()
        -- The proving caller, in miniature: a context, an action and a binding a
        -- HUD button writes into. Nothing here is special-cased.
        local context = Instance.new("InputContext")
        context.Parent = InputService
        local action = Instance.new("InputAction")
        action.Name = "VirtualJump"
        action.Parent = context
        local binding = Instance.new("InputBinding")
        binding.KeyCode = Enum.KeyCode.Virtual2
        binding.Parent = action

        expect(action:GetState()):ToBe(false)
        InputService:SetVirtualState(Enum.KeyCode.Virtual2, 1)
        step(2)
        expect(action:GetState()):ToBe(true)

        InputService:SetVirtualState(Enum.KeyCode.Virtual2, 0)
        step(2)
        expect(action:GetState()):ToBe(false)
        context:Destroy()
    end)

    it("drives a Direction2D action through a virtual stick", function()
        -- Why the seam carries a value rather than a press: an on-screen
        -- thumbstick is the other half of any touch scheme, and a virtual BUTTON
        -- would not have fitted it.
        local context = Instance.new("InputContext")
        context.Parent = InputService
        local action = Instance.new("InputAction")
        action.Type = Enum.InputActionType.Direction2D
        action.Parent = context
        local binding = Instance.new("InputBinding")
        binding.KeyCode = Enum.KeyCode.VirtualStick1
        binding.Parent = action

        InputService:SetVirtualState(Enum.KeyCode.Virtual1, 0.5)
        InputService:SetVirtualState(Enum.KeyCode.Virtual2, -1)
        step(2)
        expect(action:GetState()):ToBe(Vector2.new(0.5, -1))

        InputService:SetVirtualState(Enum.KeyCode.Virtual1, 0)
        InputService:SetVirtualState(Enum.KeyCode.Virtual2, 0)
        step(2)
        context:Destroy()
    end)

    it("marks the binding as Touch, which is what a prompt reads", function()
        local context = Instance.new("InputContext")
        context.Parent = InputService
        local action = Instance.new("InputAction")
        action.Parent = context
        local binding = Instance.new("InputBinding")
        binding.KeyCode = Enum.KeyCode.Virtual3
        binding.Parent = action

        -- `DeviceType` is derived from the key and read-only (ADR 0039), so this
        -- is the enum's `Touch` item finally having a producer.
        expect(binding.DeviceType):ToBe(Enum.InputDeviceType.Touch)
        context:Destroy()
    end)
end)
