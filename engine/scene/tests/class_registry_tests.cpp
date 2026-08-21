#include <doctest/doctest.h>

// doctest stringifies whatever a CHECK compares, and that needs the stream
// operators for std::string and std::string_view to be visible here.
#include <ostream>
#include <string>
#include <vector>

#include "scene_fixture.h"

using luaug::scene::ClassDescriptor;
using luaug::scene::ClassFlags;
using luaug::scene::hasFlag;
using luaug::scene::InvalidClass;
using luaug::scene::testing::Hierarchy;

TEST_CASE("the fixture hierarchy registers")
{
    Hierarchy schema;

    CHECK(schema.instanceClass != InvalidClass);
    CHECK(schema.folderClass != InvalidClass);
    CHECK(schema.basePartClass != InvalidClass);
    CHECK(schema.partClass != InvalidClass);
    CHECK(schema.modelClass != InvalidClass);

    // Slot 0 is the reserved "no class", so five classes means six entries.
    CHECK(schema.classes.classCount() == 7);
    CHECK(schema.classes.findId(schema.atoms.intern("Part")) == schema.partClass);
    CHECK(schema.classes.findId(schema.atoms.intern("Nothing")) == InvalidClass);
}

TEST_CASE("registration refuses an unknown super and a duplicate name")
{
    Hierarchy schema;

    ClassDescriptor orphan;
    orphan.name = schema.atoms.intern("Orphan");
    orphan.super = 4242;
    CHECK(schema.classes.registerClass(orphan) == InvalidClass);

    ClassDescriptor duplicate;
    duplicate.name = schema.atoms.intern("Part");
    duplicate.super = schema.instanceClass;
    CHECK(schema.classes.registerClass(duplicate) == InvalidClass);
}

TEST_CASE("isA walks the hierarchy and stops at siblings")
{
    Hierarchy schema;
    const auto& classes = schema.classes;

    CHECK(classes.isA(schema.partClass, schema.partClass));
    CHECK(classes.isA(schema.partClass, schema.basePartClass));
    CHECK(classes.isA(schema.partClass, schema.instanceClass));

    // Downward is not an ancestry relation, and neither is sideways.
    CHECK_FALSE(classes.isA(schema.basePartClass, schema.partClass));
    CHECK_FALSE(classes.isA(schema.folderClass, schema.basePartClass));
    CHECK_FALSE(classes.isA(schema.partClass, InvalidClass));
    CHECK_FALSE(classes.isA(InvalidClass, schema.instanceClass));
}

TEST_CASE("member lookup resolves through the hierarchy")
{
    Hierarchy schema;
    const auto& classes = schema.classes;

    // Declared on Instance, found on a grandchild -- the memoised walk is what
    // makes an inherited property cost the same as a declared one.
    REQUIRE(classes.findProperty(schema.partClass, schema.nameProperty) != nullptr);
    CHECK(classes.findProperty(schema.partClass, schema.nameProperty)->name == schema.nameProperty);

    CHECK(classes.findProperty(schema.partClass, schema.transparencyProperty) != nullptr);
    CHECK(classes.findProperty(schema.partClass, schema.shapeProperty) != nullptr);

    // Declared on Part, so BasePart must not see it.
    CHECK(classes.findProperty(schema.basePartClass, schema.shapeProperty) == nullptr);
    // Declared on Model, so a Part must not see it.
    CHECK(classes.findProperty(schema.partClass, schema.primaryPartProperty) == nullptr);

    CHECK(classes.findProperty(schema.partClass, schema.atoms.intern("NoSuchThing")) == nullptr);
    CHECK(classes.findProperty(InvalidClass, schema.nameProperty) == nullptr);
}

TEST_CASE("a redeclared member shadows the inherited one")
{
    Hierarchy schema;

    // Registered against the same atom as Instance's `Name`, so the lookup on
    // the derived class must return this descriptor and not the base's.
    static std::vector<luaug::scene::PropertyDesc> shadowing;
    shadowing = {luaug::scene::PropertyDesc{.name = schema.nameProperty,
                                            .type = luaug::scene::ValueType::String,
                                            .threadSafety = luaug::scene::ThreadSafety::Safe,
                                            .readOnly = true}};

    ClassDescriptor shadowed;
    shadowed.name = schema.atoms.intern("Shadowed");
    shadowed.super = schema.folderClass;
    shadowed.properties = shadowing;
    const auto shadowedClass = schema.classes.registerClass(shadowed);
    REQUIRE(shadowedClass != InvalidClass);

    const auto* base = schema.classes.findProperty(schema.folderClass, schema.nameProperty);
    const auto* derived = schema.classes.findProperty(shadowedClass, schema.nameProperty);
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);
    CHECK(base != derived);
    CHECK(derived->readOnly);
    CHECK_FALSE(base->readOnly);
}

TEST_CASE("descriptors carry their flags and default names")
{
    Hierarchy schema;

    const ClassDescriptor* instance = schema.classes.find(schema.instanceClass);
    REQUIRE(instance != nullptr);
    CHECK(hasFlag(instance->flags, ClassFlags::Abstract));
    CHECK(hasFlag(instance->flags, ClassFlags::NotCreatable));

    const ClassDescriptor* part = schema.classes.find(schema.partClass);
    REQUIRE(part != nullptr);
    CHECK_FALSE(hasFlag(part->flags, ClassFlags::Abstract));
    CHECK(std::string(schema.atoms.text(part->defaultName)) == "Part");
    CHECK(part->super == schema.basePartClass);

    CHECK(schema.classes.find(InvalidClass) == nullptr);
    CHECK(schema.classes.find(9999) == nullptr);
}
