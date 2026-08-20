#include "scene_fixture.h"

namespace luaug::scene::testing {
namespace {

// The accessors are plain function pointers and cannot capture anything, so
// each reaches the atom table through the `World` it is handed. That is exactly
// the constraint the generated accessors work under, which is why the fixture
// inherits it rather than working around it.

Value getName(const World& world, core::InstanceId id)
{
    return std::string(world.atoms().text(world.name(id)));
}

bool setName(World& world, core::InstanceId id, const Value& value)
{
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr)
        return false;
    world.setName(id, world.atoms().intern(*text));
    return true;
}

Value getTransparency(const World& world, core::InstanceId id)
{
    const PartComponent* part = world.parts().find(id);
    return part == nullptr ? Value{} : Value{static_cast<f64>(part->transparency)};
}

bool setTransparency(World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    PartComponent* part = world.parts().find(id);
    if (number == nullptr || part == nullptr)
        return false;
    part->transparency = static_cast<f32>(*number);
    return true;
}

Value getSize(const World& world, core::InstanceId id)
{
    const PartComponent* part = world.parts().find(id);
    return part == nullptr ? Value{} : Value{part->size};
}

bool setSize(World& world, core::InstanceId id, const Value& value)
{
    const auto* vector = std::get_if<core::Vec3>(&value);
    PartComponent* part = world.parts().find(id);
    if (vector == nullptr || part == nullptr)
        return false;
    part->size = *vector;
    return true;
}

Value getShape(const World& world, core::InstanceId id)
{
    const PartComponent* part = world.parts().find(id);
    return part == nullptr ? Value{} : Value{static_cast<f64>(part->shape)};
}

bool setShape(World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    PartComponent* part = world.parts().find(id);
    if (number == nullptr || part == nullptr)
        return false;
    part->shape = static_cast<i32>(*number);
    return true;
}

Value getPrimaryPart(const World& world, core::InstanceId id)
{
    const ModelComponent* model = world.models().find(id);
    return model == nullptr ? Value{} : Value{model->primaryPart};
}

bool setPrimaryPart(World& world, core::InstanceId id, const Value& value)
{
    const auto* reference = std::get_if<core::InstanceId>(&value);
    ModelComponent* model = world.models().find(id);
    if (reference == nullptr || model == nullptr)
        return false;
    model->primaryPart = *reference;
    return true;
}

void attachPart(World& world, core::InstanceId id)
{
    world.parts().add(id, PartComponent{});
}

void detachPart(World& world, core::InstanceId id)
{
    world.parts().remove(id);
}

void attachModel(World& world, core::InstanceId id)
{
    world.models().add(id, ModelComponent{});
}

void detachModel(World& world, core::InstanceId id)
{
    world.models().remove(id);
}

} // namespace

Hierarchy::Hierarchy()
{
    nameProperty = atoms.intern("Name");
    transparencyProperty = atoms.intern("Transparency");
    sizeProperty = atoms.intern("Size");
    shapeProperty = atoms.intern("Shape");
    primaryPartProperty = atoms.intern("PrimaryPart");

    // Owned by this `Hierarchy`; the registry holds spans into them, which is
    // why the type is non-movable.
    //
    // Designated rather than positional. These were positional until M4.5 added
    // a field to `PropertyDesc`, at which point six fixtures stopped compiling
    // and one of them would have silently kept compiling with the new flag
    // reading an accessor pointer if the types had happened to line up.
    m_instanceProperties = {
        PropertyDesc{.name = nameProperty, .type = ValueType::String, .get = getName, .set = setName},
    };
    m_basePartProperties = {
        PropertyDesc{
            .name = transparencyProperty, .type = ValueType::Number, .get = getTransparency, .set = setTransparency},
        PropertyDesc{.name = sizeProperty, .type = ValueType::Vector3, .get = getSize, .set = setSize},
    };
    m_partProperties = {
        PropertyDesc{.name = shapeProperty, .type = ValueType::Number, .get = getShape, .set = setShape},
    };
    m_modelProperties = {
        PropertyDesc{
            .name = primaryPartProperty, .type = ValueType::Instance, .get = getPrimaryPart, .set = setPrimaryPart},
    };

    ClassDescriptor instanceClassDesc;
    instanceClassDesc.name = atoms.intern("Instance");
    instanceClassDesc.flags = ClassFlags::Abstract | ClassFlags::NotCreatable;
    instanceClassDesc.defaultName = atoms.intern("Instance");
    instanceClassDesc.properties = m_instanceProperties;
    instanceClass = classes.registerClass(instanceClassDesc);

    ClassDescriptor folderDesc;
    folderDesc.name = atoms.intern("Folder");
    folderDesc.super = instanceClass;
    folderDesc.defaultName = atoms.intern("Folder");
    folderClass = classes.registerClass(folderDesc);

    ClassDescriptor basePartDesc;
    basePartDesc.name = atoms.intern("BasePart");
    basePartDesc.super = instanceClass;
    basePartDesc.flags = ClassFlags::Abstract;
    basePartDesc.defaultName = atoms.intern("BasePart");
    basePartDesc.properties = m_basePartProperties;
    basePartDesc.attachComponents = attachPart;
    basePartDesc.detachComponents = detachPart;
    basePartClass = classes.registerClass(basePartDesc);

    ClassDescriptor partDesc;
    partDesc.name = atoms.intern("Part");
    partDesc.super = basePartClass;
    partDesc.defaultName = atoms.intern("Part");
    partDesc.properties = m_partProperties;
    partClass = classes.registerClass(partDesc);

    ClassDescriptor modelDesc;
    modelDesc.name = atoms.intern("Model");
    modelDesc.super = instanceClass;
    modelDesc.defaultName = atoms.intern("Model");
    modelDesc.properties = m_modelProperties;
    modelDesc.attachComponents = attachModel;
    modelDesc.detachComponents = detachModel;
    modelClass = classes.registerClass(modelDesc);
}

} // namespace luaug::scene::testing
