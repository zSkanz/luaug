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
