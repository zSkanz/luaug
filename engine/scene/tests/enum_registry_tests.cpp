#include <doctest/doctest.h>

#include <array>
#include <string>

#include "class_descriptors.gen.h"
#include "luaug/core/name_atom.h"
#include "luaug/scene/enum_registry.h"

using namespace luaug::scene;
using luaug::core::AtomTable;

namespace
{

// Two items with non-contiguous values, because the registry must never be
// tempted to treat a value as an index -- `enums.api.luau` calls the value
// contract, and nothing promises it counts from zero.
struct Fixture
{
    AtomTable atoms;
    EnumRegistry enums;
    std::array<EnumItemDesc, 3> items;
    EnumId id = InvalidEnum;

    Fixture()
    {
        items = {{
            EnumItemDesc{atoms.intern("Low"), 0, {}},
            EnumItemDesc{atoms.intern("High"), 7, {}},
            EnumItemDesc{atoms.intern("Middle"), 3, {}},
        }};

        EnumDescriptor descriptor;
        descriptor.name = atoms.intern("Severity");
        descriptor.items = items;
        id = enums.registerEnum(descriptor);
    }
};

} // namespace

TEST_CASE("id 0 is no enum, and registration starts after it")
{
    Fixture fixture;

    CHECK(fixture.id != InvalidEnum);
    CHECK(fixture.enums.find(InvalidEnum) == nullptr);
    // A zero-initialised `EnumValue` has to be detectably absent rather than
    // accidentally the first enum registered.
    CHECK(fixture.enums.find(EnumValue{}.enumId) == nullptr);
}

TEST_CASE("an enum is found by name and by id, and neither invents one")
{
    Fixture fixture;

    CHECK(fixture.enums.findId(fixture.atoms.intern("Severity")) == fixture.id);
    CHECK(fixture.enums.findId(fixture.atoms.intern("Nonexistent")) == InvalidEnum);
    CHECK(fixture.enums.find(fixture.id) != nullptr);
    CHECK(fixture.enums.find(static_cast<EnumId>(fixture.id + 1)) == nullptr);
}

TEST_CASE("items keep declaration order, which is GetEnumItems' documented order")
{
    Fixture fixture;
    const EnumDescriptor* descriptor = fixture.enums.find(fixture.id);
    REQUIRE(descriptor != nullptr);

    REQUIRE(descriptor->items.size() == 3);
    CHECK(std::string(fixture.atoms.text(descriptor->items[0].name)) == "Low");
    CHECK(std::string(fixture.atoms.text(descriptor->items[1].name)) == "High");
    CHECK(std::string(fixture.atoms.text(descriptor->items[2].name)) == "Middle");
}

TEST_CASE("an item is found by name and by value, and a value no item carries is not one")
{
    Fixture fixture;

    const EnumItemDesc* byName = fixture.enums.findItem(fixture.id, fixture.atoms.intern("High"));
    REQUIRE(byName != nullptr);
    CHECK(byName->value == 7);

    // Looked up by value rather than by position: 7 is the second item.
    const EnumItemDesc* byValue = fixture.enums.findValue(fixture.id, 7);
    REQUIRE(byValue != nullptr);
    CHECK(std::string(fixture.atoms.text(byValue->name)) == "High");

    CHECK(fixture.enums.findItem(fixture.id, fixture.atoms.intern("Nope")) == nullptr);
    // 1 and 2 sit between the declared values and belong to nothing. A registry
    // that answered here would let a property write store a number the enum has
    // no name for, which is the failure `setPartShape` validates against.
    CHECK(fixture.enums.findValue(fixture.id, 1) == nullptr);
    CHECK(fixture.enums.findValue(fixture.id, 2) == nullptr);
    CHECK(fixture.enums.findValue(InvalidEnum, 0) == nullptr);
}

TEST_CASE("a duplicate name is refused rather than shadowing the first")
{
    Fixture fixture;

    EnumDescriptor duplicate;
    duplicate.name = fixture.atoms.intern("Severity");
    duplicate.items = fixture.items;

    CHECK(fixture.enums.registerEnum(duplicate) == InvalidEnum);
    // And the original still resolves, rather than the second having replaced it
    // halfway.
    CHECK(fixture.enums.findId(fixture.atoms.intern("Severity")) == fixture.id);
}

TEST_CASE("the generated enums register in declaration order, matching the header constants")
{
    AtomTable atoms;
    EnumRegistry enums;
    generated::registerEnums(enums, atoms);

    // The ids are constants in a generated header and are read by hand-written
    // accessors, so "the constant equals the registration order" is the whole
    // contract between the two halves.
    CHECK(enums.findId(atoms.intern("PartShape")) == generated::PartShapeEnumId);
    CHECK(enums.findId(atoms.intern("RotationOrder")) == generated::RotationOrderEnumId);
    CHECK(enums.findId(atoms.intern("LogLevel")) == generated::LogLevelEnumId);
    CHECK(enums.findId(atoms.intern("RunContext")) == generated::RunContextEnumId);

    // LogLevel's values order the levels -- a handler filters on them
    // (api-design.md §2.1) -- so they are asserted rather than assumed.
    const EnumItemDesc* warning = enums.findItem(generated::LogLevelEnumId, atoms.intern("Warning"));
    const EnumItemDesc* info = enums.findItem(generated::LogLevelEnumId, atoms.intern("Info"));
    REQUIRE(warning != nullptr);
    REQUIRE(info != nullptr);
    CHECK(warning->value > info->value);

    // All six rotation orders, because `CFrame.fromEuler` takes any of them.
    const EnumDescriptor* rotationOrder = enums.find(generated::RotationOrderEnumId);
    REQUIRE(rotationOrder != nullptr);
    CHECK(rotationOrder->items.size() == 6);
}
