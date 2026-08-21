#include <doctest/doctest.h>
#include <ostream>

#include "script_fixture.h"

using luaug::script::testing::Fixture;

TEST_CASE("the VM boots with the generated surface installed")
{
    Fixture fixture;
    REQUIRE(fixture.booted);
    // Every `raises` assertion below depends on it, so a missing catalog has to
    // fail here rather than turn each of them into a match against nothing.
    REQUIRE(fixture.catalogLoaded);

    CHECK(fixture.failure(R"(
        assert(type(Instance) == "table")
        assert(type(Instance.new) == "function")
        assert(type(CFrame) == "table")
        assert(type(Color3) == "table")
        assert(type(Random) == "table")
        assert(type(Vector3) == "table")
    )") == "");
}

TEST_CASE("Instance.new builds a real instance and refuses what it should")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        assert(part.ClassName == "Part")
        assert(part.Name == "Part")
        assert(part.Parent == nil)
        assert(typeof(part) == "Instance")
    )") == "");

    // Every class is `"Instance"` to `typeof`, whatever its class -- the class
    // is `ClassName` (api-design.md §2.3).
    CHECK(fixture.failure(R"(
        assert(typeof(Instance.new("Folder")) == typeof(Instance.new("Part")))
    )") == "");

    CHECK(fixture.raises(R"(Instance.new("Nonexistent"))", "scene.err.unknown_class"));
    // Abstract, service and not-creatable are three reasons and one key.
    CHECK(fixture.raises(R"(Instance.new("BasePart"))", "scene.err.not_creatable"));
    CHECK(fixture.raises(R"(Instance.new("Workspace"))", "scene.err.not_creatable"));
    CHECK(fixture.raises(R"(Instance.new("Script"))", "scene.err.not_creatable"));
}

TEST_CASE("two handles to the same instance are the same value")
{
    Fixture fixture;

    // The whole point of the weak-valued cache. Scripts rely on this without
    // ever thinking about it -- a table keyed by instance stops working the
    // moment it is false.
    CHECK(fixture.failure(R"(
        local parent = Instance.new("Folder")
        local child = Instance.new("Part")
        child.Parent = parent

        assert(parent:GetChildren()[1] == child)
        assert(child.Parent == parent)
        assert(parent:FindFirstChild("Part") == child)

        local seen = {}
        seen[child] = true
        assert(seen[parent:GetChildren()[1]] == true)
    )") == "");

    // And two different instances are never equal, even with the same name.
    CHECK(fixture.failure(R"(
        local a = Instance.new("Part")
        local b = Instance.new("Part")
        assert(a ~= b)
        assert(a == a)
    )") == "");
}

TEST_CASE("properties round-trip through their real storage")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")

        part.Name = "Ground"
        assert(part.Name == "Ground")

        part.Size = Vector3.new(4, 1, 8)
        assert(part.Size == Vector3.new(4, 1, 8))

        part.Transparency = 0.25
        assert(part.Transparency == 0.25)

        part.Color = Color3.new(0.25, 0.5, 0.75)
        assert(part.Color == Color3.new(0.25, 0.5, 0.75))

        part.CFrame = CFrame.new(1, 2, 3)
        assert(part.CFrame.Position == Vector3.new(1, 2, 3))
        -- Position is the f32 rounding of the CFrame translation, not storage
        -- of its own: writing one moves the other.
        assert(part.Position == Vector3.new(1, 2, 3))

        part.Position = Vector3.new(9, 9, 9)
        assert(part.CFrame.Position == Vector3.new(9, 9, 9))
    )") == "");
}

TEST_CASE("Orientation is degrees, YXZ, and survives a round trip")
{
    Fixture fixture;

    // The pair has to be exact inverses or reading a part's orientation right
    // after setting it would return something else.
    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        part.Orientation = Vector3.new(10, 20, 30)

        local back = part.Orientation
        assert(math.abs(back.x - 10) < 1e-3, "x " .. tostring(back.x))
        assert(math.abs(back.y - 20) < 1e-3, "y " .. tostring(back.y))
        assert(math.abs(back.z - 30) < 1e-3, "z " .. tostring(back.z))
    )") == "");
}

TEST_CASE("a property write is type-checked, and the refusal names the type it wanted")
{
    Fixture fixture;

    // Strict rather than truthy or coercing: `part.Name = 3` would otherwise
    // store the coercion's answer rather than the caller's intent.
    CHECK(fixture.raises("Instance.new('Part').Name = 3", "scene.err.expected_string"));
    CHECK(fixture.raises("Instance.new('Part').Transparency = 'half'", "scene.err.expected_number"));
    CHECK(fixture.raises("Instance.new('Part').Size = 4", "scene.err.expected_vector"));
    CHECK(fixture.raises("Instance.new('Part').Color = Vector3.new(1, 1, 1)", "scene.err.expected_color3"));
    CHECK(fixture.raises("Instance.new('Part').CFrame = Vector3.new(1, 1, 1)", "scene.err.expected_cframe"));
    CHECK(fixture.raises("Instance.new('Model').PrimaryPart = 1", "scene.err.expected_instance"));
    // `scene.err.expected_boolean` has no creatable class to be raised from in
    // the M2 surface: the only boolean properties are `Script.Enabled`, whose
    // class is NotCreatable, and `DebugService.OverlayVisible`, which is a
    // service. Both arrive with the service wiring, and so does this assertion.

    CHECK(fixture.raises("Instance.new('Part').ClassName = 'Folder'", "scene.err.read_only_property"));
    CHECK(fixture.raises("Instance.new('Part').Nonexistent = 1", "scene.err.unknown_member"));
    // Reading a member a class does not have is an error and not nil, which is
    // what makes a rename enforceable rather than silently returning nothing.
    CHECK(fixture.raises("return Instance.new('Part').Nonexistent", "scene.err.unknown_member"));
}

TEST_CASE("the tree is walked in the order the document promises")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        local names = {"a", "b", "c"}
        for _, name in names do
            local child = Instance.new("Folder")
            child.Name = name
            child.Parent = root
        end

        local children = root:GetChildren()
        assert(#children == 3)
        -- Child order is parenting order.
        assert(children[1].Name == "a")
        assert(children[2].Name == "b")
        assert(children[3].Name == "c")

        local grandchild = Instance.new("Part")
        grandchild.Name = "deep"
        grandchild.Parent = children[1]

        -- Document order: child order taken depth-first, preorder.
        local descendants = root:GetDescendants()
        assert(#descendants == 4)
        assert(descendants[1].Name == "a")
        assert(descendants[2].Name == "deep")
        assert(descendants[3].Name == "b")
        assert(descendants[4].Name == "c")
    )") == "");
}

TEST_CASE("FindFirstChild handles duplicate names and finds the first in document order")
{
    Fixture fixture;

    // ADR 0026: siblings may share a name, and a plain name map would silently
    // clobber all but one of them.
    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        local first, second
        for index = 1, 2 do
            local tree = Instance.new("Folder")
            tree.Name = "Tree"
            tree.Parent = root
            if index == 1 then first = tree else second = tree end
        end

        assert(#root:GetChildren() == 2)
        assert(root:FindFirstChild("Tree") == first)

        first:Destroy()
        assert(root:FindFirstChild("Tree") == second)
        assert(root:FindFirstChild("Nothing") == nil)
    )") == "");
}

TEST_CASE("the Find family distinguishes exact class from IsA")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        local part = Instance.new("Part")
        part.Parent = root

        -- Exact match, so asking for BasePart never finds a Part.
        assert(root:FindFirstChildOfClass("Part") == part)
        assert(root:FindFirstChildOfClass("BasePart") == nil)
        -- Matched through the hierarchy, so an abstract base name is accepted.
        assert(root:FindFirstChildWhichIsA("BasePart") == part)

        assert(part:FindFirstAncestor("Folder") == root)
        assert(part:FindFirstAncestorOfClass("Folder") == root)
        -- The instance itself is never a candidate.
        assert(part:FindFirstAncestorOfClass("Part") == nil)
    )") == "");
}

TEST_CASE("IsA answers false for a name that is not a class, rather than raising")
{
    Fixture fixture;

    // The point of `IsA` is to test names you do not trust.
    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        assert(part:IsA("Part"))
        assert(part:IsA("BasePart"))
        assert(part:IsA("Instance"))
        assert(not part:IsA("Folder"))
        assert(not part:IsA("NotAClassAtAll"))
    )") == "");
}

TEST_CASE("ancestry is read strictly in both directions")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        local middle = Instance.new("Folder")
        local leaf = Instance.new("Part")
        middle.Parent = root
        leaf.Parent = middle

        assert(root:IsAncestorOf(leaf))
        assert(leaf:IsDescendantOf(root))
        -- Strict: an instance is neither its own ancestor nor its own descendant.
        assert(not root:IsAncestorOf(root))
        assert(not root:IsDescendantOf(root))
        assert(not leaf:IsAncestorOf(root))
    )") == "");
}

TEST_CASE("Parent refuses a cycle and stays locked after Destroy")
{
    Fixture fixture;

    CHECK(fixture.raises(R"(
        local root = Instance.new("Folder")
        local child = Instance.new("Folder")
        child.Parent = root
        root.Parent = child
    )",
                         "scene.err.parent_cycle"));

    // A destroyed instance's Parent is locked to nil (divergence #25). The
    // handle still resolves until the drain that carries its `Destroying`
    // finishes, which is exactly what makes this raise rather than crash.
    CHECK(fixture.raises(R"(
        local root = Instance.new("Folder")
        local child = Instance.new("Folder")
        child:Destroy()
        child.Parent = root
    )",
                         "scene.err.parent_locked"));

    // Re-assigning the current parent changes nothing and does not reorder.
    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        local a = Instance.new("Folder")
        local b = Instance.new("Folder")
        a.Name, b.Name = "a", "b"
        a.Parent, b.Parent = root, root

        a.Parent = root
        assert(root:GetChildren()[1] == a)
        assert(root:GetChildren()[2] == b)
    )") == "");
}

TEST_CASE("destroying while iterating is safe, because the array is the caller's")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        for index = 1, 5 do
            local child = Instance.new("Part")
            child.Name = "child" .. index
            child.Parent = root
        end

        local children = root:GetChildren()
        for _, child in children do
            child:Destroy()
        end

        assert(#children == 5)
        assert(#root:GetChildren() == 0)
    )") == "");
}

TEST_CASE("Clone deep-copies unparented and rewires references inside the copy")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local model = Instance.new("Model")
        local part = Instance.new("Part")
        part.Name = "Body"
        part.Size = Vector3.new(2, 3, 4)
        part.Parent = model
        model.PrimaryPart = part
        model:SetAttribute("hp", 12)

        local copy = model:Clone()
        assert(copy ~= model)
        assert(copy.Parent == nil)
        assert(copy:GetAttribute("hp") == 12)

        local copiedPart = copy:FindFirstChild("Body")
        assert(copiedPart ~= nil and copiedPart ~= part)
        assert(copiedPart.Size == Vector3.new(2, 3, 4))
        -- A reference that points inside the copied subtree is rewired to the
        -- copy; this is the one that catches people.
        assert(copy.PrimaryPart == copiedPart)
    )") == "");
}

TEST_CASE("a destroyed handle keeps resolving until its Destroying has been carried")
{
    Fixture fixture;

    // Divergence #25: `Destroy` removes the subtree synchronously, and the
    // handle stays usable until the end of the drain that carries `Destroying`
    // -- which is the whole reason that signal can be given a live instance.
    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        part.Name = "Doomed"
        part:Destroy()

        assert(part.Name == "Doomed")
        assert(part.Parent == nil)

        local same = part
        assert(part == same)
        assert(tostring(part) == "Doomed")
    )") == "");

    // And it stops resolving when the drain retires it. Checked from C++
    // because a Luau handle cannot outlive its chunk yet -- per-script
    // sandboxing gives each one its own globals, and the scheduler that would
    // drain between them arrives with `task`. `script.err.instance_dead` gets
    // its Luau-level test then.
    const luaug::core::InstanceId id = fixture.world->create(fixture.classes.findId(fixture.atoms.lookup("Part")));
    REQUIRE(fixture.world->alive(id));
    fixture.world->destroy(id);
    CHECK(fixture.world->alive(id));
    fixture.world->retireDestroyed();
    CHECK_FALSE(fixture.world->alive(id));
}

TEST_CASE("attributes hold their documented domain and reject everything else")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        part:SetAttribute("name", "hello")
        part:SetAttribute("count", 3)
        part:SetAttribute("flag", true)
        part:SetAttribute("offset", Vector3.new(1, 2, 3))
        part:SetAttribute("tint", Color3.new(1, 0, 0))
        part:SetAttribute("frame", CFrame.new(4, 5, 6))

        assert(part:GetAttribute("name") == "hello")
        assert(part:GetAttribute("count") == 3)
        assert(part:GetAttribute("flag") == true)
        assert(part:GetAttribute("offset") == Vector3.new(1, 2, 3))
        assert(part:GetAttribute("tint") == Color3.new(1, 0, 0))
        assert(part:GetAttribute("frame") == CFrame.new(4, 5, 6))

        -- Never set is nil, and names are case-sensitive.
        assert(part:GetAttribute("Name") == nil)
        assert(part:GetAttribute("missing") == nil)

        local all = part:GetAttributes()
        assert(all.name == "hello")
        assert(all.count == 3)

        -- nil removes it.
        part:SetAttribute("count", nil)
        assert(part:GetAttribute("count") == nil)
    )") == "");

    CHECK(fixture.raises("Instance.new('Part'):SetAttribute('bad', {})", "scene.err.attribute_type"));
    CHECK(fixture.raises("local p = Instance.new('Part') p:SetAttribute('bad', p)", "scene.err.attribute_type"));
    CHECK(fixture.raises("Instance.new('Part'):SetAttribute('', 1)", "scene.err.invalid_name"));

    // A rejected write leaves any previous value in place.
    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        part:SetAttribute("hp", 5)
        pcall(function() part:SetAttribute("hp", {}) end)
        assert(part:GetAttribute("hp") == 5)
    )") == "");
}

TEST_CASE("tags are instance state and survive being unparented")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        part:AddTag("Pickup")
        assert(part:HasTag("Pickup"))
        assert(#part:GetTags() == 1)
        assert(part:GetTags()[1] == "Pickup")

        -- A no-op when already present, and when absent.
        part:AddTag("Pickup")
        assert(#part:GetTags() == 1)
        part:RemoveTag("Nothing")

        part:RemoveTag("Pickup")
        assert(not part:HasTag("Pickup"))
        assert(#part:GetTags() == 0)
    )") == "");
}

TEST_CASE("a method reached without calling it is still the method")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        local isA = part.IsA
        assert(type(isA) == "function")
        assert(isA(part, "BasePart"))
    )") == "");
}

TEST_CASE("Model pivots move every descendant part by the same transform")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local model = Instance.new("Model")
        local a = Instance.new("Part")
        local b = Instance.new("Part")
        a.Position = Vector3.new(0, 0, 0)
        b.Position = Vector3.new(2, 0, 0)
        a.Parent = model
        b.Parent = model
        model.PrimaryPart = a

        assert(model:GetPivot().Position == Vector3.new(0, 0, 0))

        model:PivotTo(CFrame.new(10, 0, 0))
        assert(a.Position == Vector3.new(10, 0, 0))
        -- Relative layout is preserved: b was two metres along, and still is.
        assert(b.Position == Vector3.new(12, 0, 0))
    )") == "");

    CHECK(fixture.failure(R"(
        local model = Instance.new("Model")
        local part = Instance.new("Part")
        part.Size = Vector3.new(2, 4, 6)
        part.Parent = model
        assert(model:GetExtentsSize() == Vector3.new(2, 4, 6))

        -- No parts is no size, rather than an inverted box.
        assert(Instance.new("Model"):GetExtentsSize() == Vector3.new(0, 0, 0))
    )") == "");
}

TEST_CASE("every method the IDL declares is implemented")
{
    Fixture fixture;
    const luaug::script::MethodCoverage coverage = fixture.runtime->methodCoverage();

    // `script.err.not_implemented` now has no subject on the method surface,
    // and this is what keeps it that way: a method added to `api/defs` without
    // a binding fails here rather than the first time a script calls it.
    CHECK(coverage.declaredWithoutBinding == 0);
}

TEST_CASE("the boot-time method cross-check reports both directions")
{
    Fixture fixture;
    const luaug::script::MethodCoverage coverage = fixture.runtime->methodCoverage();

    // A binding for a method no definition declares is a stale hand-written
    // entry: nothing generated that surface, so nothing else knows about it.
    CHECK(coverage.boundWithoutDeclaration == 0);
    // Pinned rather than merely compared, so that a class added to the IDL
    // shows up here as a number that changed. 43 at the M2 gate; 46 once
    // `HotReloadService` brought `SaveState`, `LoadState` and `IsReload`; 56
    // once M5 brought `ApplyImpulse`, `CharacterBody:Move`/`Jump`, the three
    // `PhysicsService` collision-group calls, the three `Workspace` queries and
    // `KeyboardService:IsKeyDown`; 58 at M6, which added
    // `InputAction:GetState`, `InputAction:GetPreferredBinding` and
    // `InputService:GetPointerPosition` and DELETED `KeyboardService` -- the
    // scaffold M5 tagged `DevOnly` so that its removal would be structural
    // rather than a promise. That number going DOWN by one is the removal; 60
    // once `TweenService:Create` and `:GetValue` landed beside it, and 64 once
    // `Sound:Play`/`Pause`/`Stop` and `AudioService:PlayLocal` did; 65 once
    // `AnimationPlayer:LoadAnimation` brought the animation runtime; 66 once
    // `InputService:IsKeyDown` arrived with ADR 0041's raw event surface, and 67
    // once `InputService:SetVirtualState` opened the non-device seam; and 70 at
    // M7, which added `StreamingService:AddFocus`, `:RemoveFocus` and
    // `:LoadAreaAsync`. This number is what makes a DECLARED-but-unbound method
    // impossible to ship: the IDL would count it and the binding table would
    // not, which is `Inert` for a method.
    CHECK(coverage.declared == 70);
    CHECK(coverage.bound == 70);
    CHECK(coverage.declaredWithoutBinding == 0);
}

TEST_CASE("the pivot is a PVInstance concept, and PivotOffset is what gives it meaning")
{
    Fixture fixture;

    SUBCASE("every positional class answers IsA(\"PVInstance\")")
    {
        // The question generic code wants to ask, and it can only be asked of a
        // class that exists -- which is why `PVInstance` is one rather than two
        // copies of the same two methods.
        //
        // `MeshPart` and `Camera` are the render module's and this fixture
        // registers scene's classes alone; they are covered at the host, where
        // both modules have registered.
        CHECK(fixture.failure(R"(
            assert(Instance.new("Part"):IsA("PVInstance"))
            assert(Instance.new("Model"):IsA("PVInstance"))
            assert(not Instance.new("Folder"):IsA("PVInstance"))
        )") == "");
    }

    SUBCASE("PVInstance itself cannot be constructed")
    {
        CHECK(fixture.failure(R"(
            Instance.new("PVInstance")
        )") != "");
    }

    SUBCASE("a part with no offset pivots about its own centre")
    {
        CHECK(fixture.failure(R"(
            local part = Instance.new("Part")
            part.CFrame = CFrame.new(3, 0, 0)
            assert(part:GetPivot().Position == Vector3.new(3, 0, 0))

            part:PivotTo(CFrame.new(10, 0, 0))
            assert(part.CFrame.Position == Vector3.new(10, 0, 0))
        )") == "");
    }

    SUBCASE("PivotOffset hinges a part about an edge")
    {
        // The case the whole API exists for, and the one M4's `PivotTo` could
        // not do: with an offset, rotating about the pivot swings the object
        // rather than spinning it in place. Without `PivotOffset` this is
        // `CFrame = target` and both assertions below read (0, 0, 0).
        CHECK(fixture.failure(R"(
            local door = Instance.new("Part")
            door.Size = Vector3.new(2, 4, 0.2)
            door.CFrame = CFrame.new(0, 0, 0)
            -- The hinge is the left edge, one metre along -X from the centre.
            door.PivotOffset = CFrame.new(-1, 0, 0)

            assert(door:GetPivot().Position == Vector3.new(-1, 0, 0))

            -- Swing 90 degrees about the hinge, leaving the hinge where it is.
            door:PivotTo(CFrame.new(-1, 0, 0) * CFrame.fromEuler(0, math.pi / 2, 0))
            assert((door:GetPivot().Position - Vector3.new(-1, 0, 0)).Magnitude < 1e-6)

            -- The centre swung to the hinge's -Z side, which is what a door does
            -- and what setting CFrame directly cannot.
            local centre = door.CFrame.Position
            assert(math.abs(centre.X - (-1)) < 1e-6, tostring(centre))
            assert(math.abs(centre.Z - (-1)) < 1e-6, tostring(centre))
        )") == "");
    }

    SUBCASE("a model with no primary part pivots about its extents box, not its centroid")
    {
        // The two differ whenever parts differ in size, which is exactly the
        // case M4's comment claimed to handle and its code did not: it averaged
        // part POSITIONS. Here the average is x = 5 and the box centre is x = 6.
        CHECK(fixture.failure(R"(
            local model = Instance.new("Model")
            local small = Instance.new("Part")
            small.Size = Vector3.new(1, 1, 1)
            small.Position = Vector3.new(0, 0, 0)
            small.Parent = model
            local big = Instance.new("Part")
            big.Size = Vector3.new(5, 1, 1)
            big.Position = Vector3.new(10, 0, 0)
            big.Parent = model

            -- Box spans x in [-0.5, 12.5], so its centre is 6.
            assert(model:GetExtentsSize().X == 13)
            local pivot = model:GetPivot().Position
            assert(math.abs(pivot.X - 6) < 1e-9, tostring(pivot))
        )") == "");
    }

    SUBCASE("a model takes its primary part's own pivot, offset included")
    {
        CHECK(fixture.failure(R"(
            local model = Instance.new("Model")
            local part = Instance.new("Part")
            part.CFrame = CFrame.new(4, 0, 0)
            part.PivotOffset = CFrame.new(0, 2, 0)
            part.Parent = model
            model.PrimaryPart = part

            -- Assigning a primary part means more than picking a position: the
            -- part's own hinge becomes the model's.
            assert(model:GetPivot().Position == Vector3.new(4, 2, 0))
        )") == "");
    }

    SUBCASE("PivotTo on a model still moves every part and keeps their layout")
    {
        CHECK(fixture.failure(R"(
            local model = Instance.new("Model")
            local a = Instance.new("Part")
            local b = Instance.new("Part")
            a.Position = Vector3.new(0, 0, 0)
            b.Position = Vector3.new(2, 0, 0)
            a.Parent = model
            b.Parent = model
            model.PrimaryPart = a

            model:PivotTo(CFrame.new(10, 0, 0))
            assert(a.Position == Vector3.new(10, 0, 0))
            assert(b.Position == Vector3.new(12, 0, 0))
        )") == "");
    }
}

// --- The DebugShell's two missing panes (D017) --------------------------------
//
// `architecture.md` §app names a memory-category table and a log/REPL pane, and
// neither had ever been written. The panes themselves are ImGui and a windowed
// run; what is testable is the surface underneath them, and that is where the
// claims live.

TEST_CASE("the REPL runs a chunk in the VM the game is running in")
{
    Fixture fixture;
    REQUIRE(fixture.booted);

    // It is `runSource` with a name and a category, deliberately: a REPL that
    // took a different path into the VM would be a second path to keep honest.
    CHECK_FALSE(fixture.runtime->evaluate("local part = Instance.new('Part') part.Name = 'FromRepl'").has_value());

    const luaug::scene::World& world = *fixture.world;
    bool found = false;
    world.parts().forEach([&](luaug::core::InstanceId id, const luaug::scene::PartComponent&) {
        if (world.atoms().text(world.name(id)) == "FromRepl")
            found = true;
    });
    CHECK(found);
}

TEST_CASE("a REPL chunk that does not compile is an error rather than a crash")
{
    Fixture fixture;
    REQUIRE(fixture.booted);
    CHECK(fixture.runtime->evaluate("this is not luau").has_value());
    // And the VM survives it: the next line still runs.
    CHECK_FALSE(fixture.runtime->evaluate("local x = 1").has_value());
}

TEST_CASE("the memory table names a category per script and reports bytes")
{
    Fixture fixture;
    REQUIRE(fixture.booted);

    CHECK_FALSE(fixture.runtime->runSource("local held = table.create(4096, 'x')", "leaky.luau").has_value());

    const std::vector<luaug::script::ScriptRuntime::MemoryCategory> rows = fixture.runtime->memoryByCategory();
    REQUIRE_FALSE(rows.empty());

    bool sawScript = false;
    for (const auto& row : rows) {
        CHECK(row.bytes > 0);
        if (row.name == "leaky.luau") {
            sawScript = true;
            // The pool starts at 32; anything below it is one of the engine's
            // own eight (architecture.md §6).
            CHECK(row.category >= 32);
        }
    }
    CHECK(sawScript);
}

TEST_CASE("two scripts get two categories, and a reload reuses one")
{
    // Per chunk NAME rather than per call, so a hot reload re-uses the row and
    // the number stays comparable across one -- which is the whole point of a
    // per-script table for leak triage.
    Fixture fixture;
    REQUIRE(fixture.booted);

    CHECK_FALSE(fixture.runtime->runSource("local a = 1", "one.luau").has_value());
    CHECK_FALSE(fixture.runtime->runSource("local b = 2", "two.luau").has_value());

    luaug::core::u32 first = 0;
    luaug::core::u32 second = 0;
    for (const auto& row : fixture.runtime->memoryByCategory()) {
        if (row.name == "one.luau")
            first = row.category;
        if (row.name == "two.luau")
            second = row.category;
    }
    CHECK(first != 0);
    CHECK(second != 0);
    CHECK(first != second);

    CHECK_FALSE(fixture.runtime->runSource("local a = 3", "one.luau").has_value());
    for (const auto& row : fixture.runtime->memoryByCategory()) {
        if (row.name == "one.luau")
            CHECK(row.category == first);
    }
}
