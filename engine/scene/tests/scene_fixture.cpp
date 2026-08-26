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

// Added when the scene format needed it. A fixture that could not express a
// `CFrame` could not test the one property type an authored world is mostly
// made of -- and `CFrame` is also the only f64 one, so it is where a
// serializer that narrows on the way through would show it.
Value getCFrame(const World& world, core::InstanceId id)
{
    const PartComponent* part = world.parts().find(id);
    return part == nullptr ? Value{} : Value{part->cframe};
}

bool setCFrame(World& world, core::InstanceId id, const Value& value)
{
    const auto* frame = std::get_if<core::CFrameD>(&value);
    PartComponent* part = world.parts().find(id);
    if (frame == nullptr || part == nullptr)
        return false;
    part->cframe = *frame;
    return true;
}

// --- Attachment and Constraint -----------------------------------------------
//
// One class stands in for `Attachment` and `Bone` here, and one for the three
// constraints, exactly as the real hierarchy shares one component between them.

Value getAttachmentCFrame(const World& world, core::InstanceId id)
{
    const AttachmentComponent* attachment = world.attachments().find(id);
    return attachment == nullptr ? Value{} : Value{attachment->cframe};
}

bool setAttachmentCFrame(World& world, core::InstanceId id, const Value& value)
{
    const auto* frame = std::get_if<core::CFrameD>(&value);
    AttachmentComponent* attachment = world.attachments().find(id);
    if (frame == nullptr || attachment == nullptr)
        return false;
    attachment->cframe = *frame;
    return true;
}

Value getJointName(const World& world, core::InstanceId id)
{
    const AttachmentComponent* attachment = world.attachments().find(id);
    return attachment == nullptr ? Value{} : Value{std::string(world.atoms().text(attachment->jointName))};
}

bool setJointName(World& world, core::InstanceId id, const Value& value)
{
    const auto* text = std::get_if<std::string>(&value);
    AttachmentComponent* attachment = world.attachments().find(id);
    if (text == nullptr || attachment == nullptr)
        return false;
    attachment->jointName = world.atoms().intern(*text);
    // The index is re-resolved against the rig, so a rename must not keep the
    // old answer. -1 is what "not looked up yet" means.
    attachment->jointIndex = -1;
    return true;
}

Value getAttachment0(const World& world, core::InstanceId id)
{
    const ConstraintComponent* constraint = world.constraints().find(id);
    return constraint == nullptr ? Value{} : Value{constraint->attachment0};
}

bool setAttachment0(World& world, core::InstanceId id, const Value& value)
{
    const auto* other = std::get_if<core::InstanceId>(&value);
    ConstraintComponent* constraint = world.constraints().find(id);
    if (other == nullptr || constraint == nullptr)
        return false;
    constraint->attachment0 = *other;
    return true;
}

Value getAttachment1(const World& world, core::InstanceId id)
{
    const ConstraintComponent* constraint = world.constraints().find(id);
    return constraint == nullptr ? Value{} : Value{constraint->attachment1};
}

bool setAttachment1(World& world, core::InstanceId id, const Value& value)
{
    const auto* other = std::get_if<core::InstanceId>(&value);
    ConstraintComponent* constraint = world.constraints().find(id);
    if (other == nullptr || constraint == nullptr)
        return false;
    constraint->attachment1 = *other;
    return true;
}

Value getCollideConnected(const World& world, core::InstanceId id)
{
    const ConstraintComponent* constraint = world.constraints().find(id);
    return constraint == nullptr ? Value{} : Value{constraint->collideConnected};
}

bool setCollideConnected(World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    ConstraintComponent* constraint = world.constraints().find(id);
    if (flag == nullptr || constraint == nullptr)
        return false;
    constraint->collideConnected = *flag;
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

void attachMeshPart(World& world, core::InstanceId id)
{
    world.meshParts().add(id, MeshPartComponent{});
}

void detachMeshPart(World& world, core::InstanceId id)
{
    world.meshParts().remove(id);
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
    cframeProperty = atoms.intern("CFrame");
    shapeProperty = atoms.intern("Shape");
    primaryPartProperty = atoms.intern("PrimaryPart");
    jointNameProperty = atoms.intern("JointName");
    attachment0Property = atoms.intern("Attachment0");
    attachment1Property = atoms.intern("Attachment1");
    collideConnectedProperty = atoms.intern("CollideConnected");

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
        PropertyDesc{.name = cframeProperty, .type = ValueType::CFrame, .get = getCFrame, .set = setCFrame},
    };
    m_partProperties = {
        PropertyDesc{.name = shapeProperty, .type = ValueType::Number, .get = getShape, .set = setShape},
    };
    m_modelProperties = {
        PropertyDesc{
            .name = primaryPartProperty, .type = ValueType::Instance, .get = getPrimaryPart, .set = setPrimaryPart},
    };
    m_attachmentProperties = {
        PropertyDesc{
            .name = cframeProperty, .type = ValueType::CFrame, .get = getAttachmentCFrame, .set = setAttachmentCFrame},
        PropertyDesc{.name = jointNameProperty, .type = ValueType::String, .get = getJointName, .set = setJointName},
    };
    m_constraintProperties = {
        PropertyDesc{
            .name = attachment0Property, .type = ValueType::Instance, .get = getAttachment0, .set = setAttachment0},
        PropertyDesc{
            .name = attachment1Property, .type = ValueType::Instance, .get = getAttachment1, .set = setAttachment1},
        PropertyDesc{.name = collideConnectedProperty,
                     .type = ValueType::Bool,
                     .get = getCollideConnected,
                     .set = setCollideConnected},
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

    // Registered so the streaming glue's `MeshPart` path is exercised rather
    // than skipped. It carries no properties of its own here: what the glue
    // writes is the mesh URN, and the component is what holds that.
    ClassDescriptor meshPartDesc;
    meshPartDesc.name = atoms.intern("MeshPart");
    meshPartDesc.super = basePartClass;
    meshPartDesc.defaultName = atoms.intern("MeshPart");
    meshPartDesc.attachComponents = attachMeshPart;
    meshPartDesc.detachComponents = detachMeshPart;
    meshPartClass = classes.registerClass(meshPartDesc);

    ClassDescriptor attachmentDesc;
    attachmentDesc.name = atoms.intern("Attachment");
    attachmentDesc.super = instanceClass;
    attachmentDesc.defaultName = atoms.intern("Attachment");
    attachmentDesc.properties = m_attachmentProperties;
    attachmentDesc.attachComponents = [](World& world, core::InstanceId id) {
        world.attachments().add(id, AttachmentComponent{});
    };
    attachmentDesc.detachComponents = [](World& world, core::InstanceId id) { world.attachments().remove(id); };
    attachmentClass = classes.registerClass(attachmentDesc);

    ClassDescriptor weldDesc;
    weldDesc.name = atoms.intern("Weld");
    weldDesc.super = instanceClass;
    weldDesc.defaultName = atoms.intern("Weld");
    weldDesc.attachComponents = [](World& world, core::InstanceId id) { world.welds().add(id, WeldComponent{}); };
    weldDesc.detachComponents = [](World& world, core::InstanceId id) { world.welds().remove(id); };
    weldClass = classes.registerClass(weldDesc);

    ClassDescriptor constraintDesc;
    constraintDesc.name = atoms.intern("Constraint");
    constraintDesc.super = instanceClass;
    constraintDesc.defaultName = atoms.intern("Constraint");
    constraintDesc.properties = m_constraintProperties;
    constraintDesc.attachComponents = [](World& world, core::InstanceId id) {
        world.constraints().add(id, ConstraintComponent{});
    };
    constraintDesc.detachComponents = [](World& world, core::InstanceId id) { world.constraints().remove(id); };
    constraintClass = classes.registerClass(constraintDesc);

    ClassDescriptor ragdollDesc;
    ragdollDesc.name = atoms.intern("Ragdoll");
    ragdollDesc.super = instanceClass;
    ragdollDesc.defaultName = atoms.intern("Ragdoll");
    ragdollDesc.attachComponents = [](World& world, core::InstanceId id) {
        world.ragdolls().add(id, RagdollComponent{});
    };
    ragdollDesc.detachComponents = [](World& world, core::InstanceId id) { world.ragdolls().remove(id); };
    ragdollClass = classes.registerClass(ragdollDesc);

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
