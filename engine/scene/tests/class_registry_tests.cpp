#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"

#include <doctest/doctest.h>

// doctest stringifies whatever a CHECK compares, and that needs the stream
// operators for std::string and std::string_view to be visible here.
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "class_descriptors.gen.h"
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

    // Slot 0 is the reserved "no class", so ten classes means eleven entries. A
    // number rather than a list, so adding one to the fixture is a one-line
    // change here and dropping one is a failure.
    CHECK(schema.classes.classCount() == 11);
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

TEST_CASE("the generated tables carry the enum a property accepts, and its documentation")
{
    // The REAL tables, not the fixture's: what a property grid reads is what
    // `gen_cpp.luau` emitted, and both fields are emitted or neither is.
    luaug::core::AtomTable atoms;
    luaug::scene::ClassRegistry classes;
    luaug::scene::EnumRegistry enums;
    luaug::scene::generated::registerClasses(classes, atoms);
    luaug::scene::generated::registerEnums(enums, atoms);

    const luaug::scene::ClassId part = classes.findId(atoms.intern("Part"));
    REQUIRE(part != InvalidClass);

    const luaug::scene::PropertyDesc* shape = classes.findProperty(part, atoms.intern("Shape"));
    REQUIRE(shape != nullptr);
    CHECK(shape->type == luaug::scene::ValueType::EnumItem);

    // The name resolves to the id `registerEnums` assigned, which is the whole
    // contract: the descriptor records a name precisely because the id is a fact
    // about one registry and this table is shared by all of them.
    CHECK(enums.findId(shape->enumName) == luaug::scene::generated::PartShapeEnumId);

    const luaug::scene::EnumDescriptor* shapes = enums.find(enums.findId(shape->enumName));
    REQUIRE(shapes != nullptr);
    // Five shapes, offered by an editor with no `Part` created and no value read.
    CHECK(shapes->items.size() == 5);

    // A read-only enum property answers the same way. Its domain is what it can
    // REPORT, and a panel still has to name the item it is showing.
    const luaug::scene::ClassId character = classes.findId(atoms.intern("CharacterBody"));
    REQUIRE(character != InvalidClass);
    const luaug::scene::PropertyDesc* state = classes.findProperty(character, atoms.intern("State"));
    REQUIRE(state != nullptr);
    CHECK(enums.findId(state->enumName) == luaug::scene::generated::CharacterStateEnumId);

    // A property that is not an enum names none, rather than naming whatever
    // atom happened to sit at zero.
    const luaug::scene::PropertyDesc* anchored = classes.findProperty(part, atoms.intern("Anchored"));
    REQUIRE(anchored != nullptr);
    CHECK_FALSE(anchored->enumName.valid());

    // The IDL's prose reaches the runtime rather than staying in a file nothing
    // at runtime reads. Asserted as a substring, because the sentence is the
    // IDL's to reword and the tooltip's job is only to carry it.
    CHECK(std::string_view(anchored->doc).find("Whether the simulation moves this part.") == 0);
    CHECK(std::string_view(shape->doc).find("Which primitive solid this part is.") == 0);

    // Every declared property on `Part`'s ancestry has some, because the IDL
    // makes `Doc` mandatory and a blank tooltip is how that stops being true.
    for (luaug::scene::ClassId id = part; id != InvalidClass;) {
        const luaug::scene::ClassDescriptor* descriptor = classes.find(id);
        REQUIRE(descriptor != nullptr);
        CHECK_FALSE(std::string_view(descriptor->doc).empty());
        for (const luaug::scene::PropertyDesc& property : descriptor->properties) {
            INFO("property ", atoms.text(property.name));
            CHECK_FALSE(std::string_view(property.doc).empty());
        }
        for (const luaug::scene::MethodDesc& method : descriptor->methods)
            CHECK_FALSE(std::string_view(method.doc).empty());
        for (const luaug::scene::EventDesc& event : descriptor->events)
            CHECK_FALSE(std::string_view(event.doc).empty());
        id = descriptor->super;
    }
}
