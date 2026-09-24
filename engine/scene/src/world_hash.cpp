// WorldHash (architecture.md §9, ADR 0025).
//
// One number that says whether two runs produced the same world. The gate it
// serves is level B: same engine build, same platform, same seed, same inputs
// and same tick configuration produce the same hash -- so what is hashed has to
// be a pure function of the operation sequence and nothing else.
//
// Two rules follow from that and neither is optional:
//
//   * **Never hash a `NameAtom`'s number.** It depends on the order strings
//     were interned, which depends on the order things were built. The text is
//     hashed instead. This is the single easiest way to write a hash that
//     reproduces perfectly on one machine and disagrees on another.
//   * **Never iterate an unordered container into the hash.** Instances are
//     walked in slot order and children in sibling order; the tag map is
//     reached per instance rather than swept.
//   * **Never hash a struct's bytes.** Padding is not initialised by anything,
//     and its contents are whatever the allocation held before -- which is
//     stable within one process and different in the next. `CFrameD` carries
//     four such bytes and `EnumValue` two, and hashing them made this gate fail
//     across processes while passing twice in a row inside one. `pod()` below
//     now refuses any type that could have padding, so the next struct to grow
//     some fails to compile instead of failing at midnight.
//
// Instance ids ARE hashed, including their slot index. That is deliberate:
// slot allocation is itself a pure function of the operation sequence (the
// SlotMap's free list is LIFO and nothing else touches it), so two runs of the
// same script assign the same slots. It is also the cheap answer -- the
// alternative is assigning stable ordinals by traversal, which costs a pass and
// buys nothing at level B.
#include "luaug/scene/world.h"

#include <algorithm>
#include <array>

// Header-only: everything xxHash needs is inlined into this translation unit,
// so there is no library to build, link or explain in a preset.
#define XXH_INLINE_ALL
#include "xxhash.h"

namespace luaug::scene {
namespace {

class Hasher
{
public:
    Hasher() { XXH3_64bits_reset(&m_state); }

    void bytes(const void* data, usize size) noexcept { XXH3_64bits_update(&m_state, data, size); }

    template <class T>
    void pod(const T& value) noexcept
    {
        // `has_unique_object_representations` is exactly "every bit of this
        // object participates in its value", which is the precondition for
        // hashing bytes at all. It excludes anything with padding, and it
        // excludes floating point -- which is why floats have their own path.
        static_assert(std::has_unique_object_representations_v<T>,
                      "hashing a type with padding hashes uninitialised memory; hash its fields instead");
        bytes(&value, sizeof(T));
    }

    // Floats go in as their bit pattern, deliberately. -0.0 and 0.0 are the same
    // number and NOT the same state -- they divide differently -- so a
    // determinism hash that conflated them would call two worlds identical when
    // the next tick will tell them apart.
    void number(f32 value) noexcept { bytes(&value, sizeof(value)); }
    void number(f64 value) noexcept { bytes(&value, sizeof(value)); }

    void flag(bool value) noexcept { pod(static_cast<u8>(value ? 1 : 0)); }

    void vec3(const core::Vec3& value) noexcept
    {
        number(value.x);
        number(value.y);
        number(value.z);
    }

    void color3(const core::Color3& value) noexcept
    {
        number(value.r);
        number(value.g);
        number(value.b);
    }

    void vec2(const core::Vec2& value) noexcept
    {
        number(value.x);
        number(value.y);
    }

    void udim(const core::UDim& value) noexcept
    {
        number(value.scale);
        number(value.offset);
    }

    void cframe(const core::CFrameD& value) noexcept
    {
        number(value.position.x);
        number(value.position.y);
        number(value.position.z);
        for (const auto& row : value.rotation.m) {
            for (const f32 element : row)
                number(element);
        }
    }

    void text(std::string_view value) noexcept
    {
        // The length goes in as well, so that ("ab", "c") cannot hash the same
        // as ("a", "bc") -- the classic way a concatenating hash loses a
        // boundary it was meant to keep.
        const u64 size = value.size();
        pod(size);
        bytes(value.data(), value.size());
    }

    [[nodiscard]] u64 digest() const noexcept { return XXH3_64bits_digest(&m_state); }

private:
    XXH3_state_t m_state{};
};

// The fields `set` names, each as its name and its value, in NAME order: the
// order a scene file writes them in and a reader sees them, rather than the
// order an enum happens to declare them.
void hashMaterialFields(Hasher& hasher, asset::MaterialFieldMask set, const asset::MaterialProperties& values)
{
    std::array<asset::MaterialField, asset::MaterialFieldCount> fields{};
    usize count = 0;
    for (usize index = 0; index < asset::MaterialFieldCount; ++index) {
        const auto field = static_cast<asset::MaterialField>(index);
        if ((set & asset::fieldBit(field)) != 0)
            fields[count++] = field;
    }
    std::sort(fields.begin(), fields.begin() + static_cast<std::ptrdiff_t>(count),
              [](asset::MaterialField a, asset::MaterialField b) {
                  return asset::materialFieldName(a) < asset::materialFieldName(b);
              });
    hasher.pod(static_cast<u64>(count));
    for (usize index = 0; index < count; ++index) {
        const asset::MaterialField field = fields[index];
        hasher.text(asset::materialFieldName(field));
        switch (field) {
        case asset::MaterialField::Color:
            hasher.color3(values.color);
            break;
        case asset::MaterialField::Emissive:
            hasher.color3(values.emissive);
            break;
        case asset::MaterialField::Transparency:
            hasher.number(values.transparency);
            break;
        case asset::MaterialField::Metalness:
            hasher.number(values.metalness);
            break;
        case asset::MaterialField::Roughness:
            hasher.number(values.roughness);
            break;
        case asset::MaterialField::NormalScale:
            hasher.number(values.normalScale);
            break;
        case asset::MaterialField::AlphaCutoff:
            hasher.number(values.alphaCutoff);
            break;
        case asset::MaterialField::ColorMap:
            hasher.text(values.colorMap);
            break;
        case asset::MaterialField::NormalMap:
            hasher.text(values.normalMap);
            break;
        case asset::MaterialField::MetallicRoughnessMap:
            hasher.text(values.metallicRoughnessMap);
            break;
        case asset::MaterialField::EmissiveMap:
            hasher.text(values.emissiveMap);
            break;
        case asset::MaterialField::AlphaMode:
            hasher.pod(values.alphaMode);
            break;
        case asset::MaterialField::DoubleSided:
            hasher.flag(values.doubleSided);
            break;
        case asset::MaterialField::Count:
            break;
        }
    }
}

void hashValue(Hasher& hasher, const Value& value, const World& world)
{
    const auto tag = static_cast<u8>(valueType(value));
    hasher.pod(tag);

    switch (valueType(value)) {
    case ValueType::Nil:
        break;
    case ValueType::Bool:
        hasher.flag(std::get<bool>(value));
        break;
    case ValueType::Number:
        hasher.number(std::get<f64>(value));
        break;
    case ValueType::String:
        hasher.text(std::get<std::string>(value));
        break;
    case ValueType::Vector3:
        hasher.vec3(std::get<core::Vec3>(value));
        break;
    case ValueType::CFrame:
        hasher.cframe(std::get<core::CFrameD>(value));
        break;
    case ValueType::Color3:
        hasher.color3(std::get<core::Color3>(value));
        break;
    case ValueType::Instance:
        hasher.pod(std::get<core::InstanceId>(value));
        break;
    case ValueType::EnumItem: {
        // Field by field: `EnumValue` is a u16 followed by an i32, and the two
        // bytes between them belong to no one.
        const EnumValue& item = std::get<EnumValue>(value);
        hasher.pod(item.enumId);
        hasher.pod(item.value);
        break;
    }
    case ValueType::Vector2:
        hasher.vec2(std::get<core::Vec2>(value));
        break;
    case ValueType::UDim:
        hasher.udim(std::get<core::UDim>(value));
        break;
    case ValueType::UDim2: {
        const core::UDim2& udim2 = std::get<core::UDim2>(value);
        hasher.udim(udim2.x);
        hasher.udim(udim2.y);
        break;
    }
    case ValueType::Rect: {
        const core::Rect& rect = std::get<core::Rect>(value);
        hasher.vec2(rect.min);
        hasher.vec2(rect.max);
        break;
    }
    case ValueType::Material: {
        // **The asset as its URN, and never its contents** (ADR 0090): a
        // material file is an input the run was given, as a script's source
        // is, not state the run produced. A clone IS state the run produced --
        // its creation order and what it changed, which a script can read back
        // and branch on.
        const MaterialRef& material = std::get<MaterialRef>(value);
        hasher.text(material.source);
        hasher.pod(material.clone);
        if (const MaterialClone* clone = material.clone != 0 ? world.materialClone(material.clone) : nullptr;
            clone != nullptr) {
            hashMaterialFields(hasher, clone->set, clone->values);
        }
        break;
    }
    case ValueType::MaterialParameters: {
        const asset::MaterialOverrides& overrides = std::get<asset::MaterialOverrides>(value);
        hashMaterialFields(hasher, overrides.set, asset::overrideValues(overrides));
        break;
    }
    }
}

} // namespace

u64 World::worldHash() const
{
    Hasher hasher;

    // Slot order, which is `SlotMap::forEach`'s documented order and a pure
    // function of the operation sequence.
    m_instances.forEach([&](core::InstanceId id, const InstanceRecord& record) {
        hasher.pod(id);
        hasher.flag(record.destroyed);

        // The class is hashed by NAME, not by `ClassId`: an id depends on
        // registration order, and registration order is a property of the
        // engine build rather than of the world.
        if (const ClassDescriptor* descriptor = m_classes.find(record.classId); descriptor != nullptr)
            hasher.text(m_atoms.text(descriptor->name));
        else
            hasher.text({});

        hasher.text(m_atoms.text(record.name));
        hasher.pod(record.parent);
        hasher.pod(record.childCount);

        // Sibling order is observable through `GetChildren`, so it is part of
        // the state a replay has to reproduce.
        for (core::InstanceId child = record.firstChild; child.valid(); child = nextSibling(child))
            hasher.pod(child);

        // Insertion-ordered by construction (see `AttributeMap`), so this walk
        // is stable without sorting.
        if (const AttributeMap* attributes = m_attributes.find(id); attributes != nullptr) {
            hasher.pod(static_cast<u64>(attributes->size()));
            for (const auto& entry : *attributes) {
                hasher.text(m_atoms.text(entry.first));
                hashValue(hasher, entry.second, *this);
            }
        }
        else {
            hasher.pod(u64{0});
        }

        if (const TagSet* tags = m_tags.find(id); tags != nullptr) {
            hasher.pod(static_cast<u64>(tags->size()));
            for (const core::NameAtom tag : *tags)
                hasher.text(m_atoms.text(tag));
        }
        else {
            hasher.pod(u64{0});
        }

        // Simulation state that is NOT a property, and therefore not covered by
        // the walk below (architecture.md §9: the hash is over sim-relevant
        // components AND physics state).
        //
        // Every field here is something the next tick's evolution depends on
        // and no script can read: an impulse queued for a tick that has not run,
        // the command a character was given, the vertical velocity a controller
        // carries, and which bodies the solver has put to sleep. Two runs
        // agreeing on every visible number while disagreeing on one of these are
        // one tick from disagreeing on all of them.
        if (const RigidBodyComponent* body = m_rigidBodies.find(id); body != nullptr) {
            hasher.vec3(body->pendingImpulse);
            hasher.flag(body->active);
        }
        if (const CharacterBodyComponent* character = m_characterBodies.find(id); character != nullptr) {
            hasher.vec3(character->moveDirection);
            hasher.flag(character->jumpRequested);
            hasher.number(character->verticalVelocity);
            hasher.pod(character->groundPart);
        }
        // An action's resolved value is simulation state and no property
        // exposes it -- `GetState` is a METHOD, so the walk below cannot see it.
        // Leaving it out would let two runs agree on every hashed number while
        // one of them had a key held, which is one tick from disagreeing about
        // everything the player did next.
        if (const InputActionComponent* action = m_inputActions.find(id); action != nullptr) {
            hasher.vec3(action->axis);
            hasher.flag(action->pressed);
        }
        // How many particles `Emit` has asked for: what a replica is sent so it
        // bursts when the authority did, and what `Emit` writes -- so two runs
        // whose scripts emitted differently hashed equal until this was here.
        // Found by `wire_hash_tests.cpp`, which changes every field the wire
        // carries that is not a property and requires this hash to notice.
        if (const ParticleEmitterComponent* emitter = m_particleEmitters.find(id); emitter != nullptr)
            hasher.pod(emitter->emitted);
        // The 2D layer: a push asked for and not yet applied, and a tilemap's
        // cells, which decide what is solid and which no property carries.
        if (const Part2DComponent* part = m_parts2d.find(id); part != nullptr) {
            hasher.vec2(part->pendingImpulse);
        }
        if (const Tilemap2DComponent* tilemap = m_tilemaps2d.find(id); tilemap != nullptr) {
            for (const auto& [key, chunk] : tilemap->chunks) {
                hasher.pod(key.x);
                hasher.pod(key.y);
                for (const core::u16 tile : chunk)
                    hasher.pod(tile);
            }
        }

        // **The terrain field, which no property can carry** (ADR 0082). It is
        // megabytes of samples, so it is not in the walk below and has to be
        // here -- and three rules decide how, each of which is a way to get it
        // silently wrong.
        //
        // **Per-chunk digests, not bytes**, computed lazily and kept until a
        // write, so this is O(chunks) instead of O(voxels).
        //
        // **The key goes in beside the digest**, because two fields holding the
        // same ground in different places are different fields.
        //
        // **Nothing about the SHARING is hashed** -- no pointer, no refcount, no
        // address -- which is what lets copy-on-write be an implementation
        // detail rather than part of the world. Every write leaves its chunks
        // canonical, so equal voxels are equal bytes and equal digests; an
        // authored world carries `.lterrain` base64'd in the scene's own
        // `terrain` key, and a save and a reload hash the same.
        if (const TerrainComponent* terrain = m_terrains.find(id); terrain != nullptr) {
            hasher.number(static_cast<f64>(terrain->field.settings().voxelSize));
            hasher.number(static_cast<f64>(terrain->minHeight));
            hasher.number(static_cast<f64>(terrain->maxHeight));
            // Where the field sits, which is world state: the same tiles at two
            // origins are two different worlds, and everything that stands on
            // them ends up somewhere else.
            hasher.number(terrain->origin.x);
            hasher.number(terrain->origin.y);
            hasher.number(terrain->origin.z);
            // Each chunk's key beside its digest, in key order: O(chunks),
            // never O(voxels). `digest()`, which computes a stale one, never
            // a cached field -- a lazy digest read raw made an edit invisible
            // to the hash once.
            for (const asset::TerrainField::Entry& entry : terrain->field.chunks()) {
                hasher.pod(entry.first.x);
                hasher.pod(entry.first.y);
                hasher.pod(entry.first.z);
                hasher.pod(entry.second->digest());
            }
        }

        // **The block world (V1)**, on the terrain's rules: the block size and
        // the registry, then each chunk's key beside its digest, in key order.
        // A player's number and this tick's intents are what the simulation
        // reads, so they are hashed like any input the tick consumes.
        if (const PlayerComponent* player = m_players.find(id); player != nullptr) {
            hasher.pod(static_cast<core::u64>(player->userId));
            hasher.pod(static_cast<core::u64>(player->local ? 1 : 0));
            hasher.pod(static_cast<core::u64>(player->intents.size()));
            for (const PlayerIntent& intent : player->intents) {
                hasher.pod(static_cast<core::u64>(intent.action.id));
                hasher.pod(static_cast<core::u64>(static_cast<core::u32>(intent.type)));
                hasher.number(static_cast<f64>(intent.axis.x));
                hasher.number(static_cast<f64>(intent.axis.y));
                hasher.number(static_cast<f64>(intent.axis.z));
                hasher.pod(static_cast<core::u64>(intent.pressed ? 1 : 0));
            }
        }
        if (const VoxelComponent* voxels = m_voxels.find(id); voxels != nullptr) {
            hasher.number(static_cast<f64>(voxels->blockSize));
            hasher.pod(static_cast<core::u64>(voxels->types.size()));
            for (const VoxelBlockType& type : voxels->types) {
                hasher.text(m_atoms.text(type.name));
                hasher.number(static_cast<f64>(type.color.r));
                hasher.number(static_cast<f64>(type.color.g));
                hasher.number(static_cast<f64>(type.color.b));
                for (const core::Color3& face : {type.side, type.bottom}) {
                    hasher.number(static_cast<f64>(face.r));
                    hasher.number(static_cast<f64>(face.g));
                    hasher.number(static_cast<f64>(face.b));
                }
                // By text, like every other name the hash reads: an atom's
                // number is a fact about this process's table.
                for (const core::NameAtom image : {type.texture, type.sideTexture, type.bottomTexture})
                    hasher.text(m_atoms.text(image));
                hasher.pod(static_cast<core::u64>(static_cast<core::u32>(type.opacity)));
                hasher.number(static_cast<f64>(type.transparency));
                // Only for a fluid, so every world hashed before fluids existed
                // hashes as it did.
                if (type.fluidReach > 0) {
                    hasher.pod(static_cast<core::u64>(type.fluidReach));
                    hasher.pod(static_cast<core::u64>(type.fluidTicks));
                }
            }
            for (const VoxelComponent::FluidReaction& reaction : voxels->fluidReactions) {
                hasher.pod(static_cast<core::u64>(reaction.from));
                hasher.pod(static_cast<core::u64>(reaction.touching));
                hasher.pod(static_cast<core::u64>(reaction.result));
            }
            // The steps water is still due to take are part of what the world
            // will become, so they are part of what it is.
            if (!voxels->fluidWakes.empty()) {
                hasher.pod(static_cast<core::u64>(voxels->fluidWakes.size()));
                for (const auto& [at, due] : voxels->fluidWakes) {
                    hasher.pod(at[0]);
                    hasher.pod(at[1]);
                    hasher.pod(at[2]);
                    hasher.pod(due);
                }
            }
            for (const asset::VoxelChunkKey key : voxels->grid.chunkKeys()) {
                hasher.pod(key.x);
                hasher.pod(key.y);
                hasher.pod(key.z);
                if (const asset::VoxelChunk* chunk = voxels->grid.findChunk(key); chunk != nullptr)
                    hasher.pod(asset::digestOf(*chunk));
            }
        }

        // Class-specific state, reached through the same generated accessors a
        // script would use. Hashing the components directly would be faster and
        // would silently stop covering a property whose storage moved.
        if (const ClassDescriptor* descriptor = m_classes.find(record.classId); descriptor != nullptr) {
            for (const ClassDescriptor* current = descriptor; current != nullptr;
                 current = m_classes.find(current->super)) {
                for (const PropertyDesc& property : current->properties) {
                    if (property.get == nullptr)
                        continue;
                    // A fact about the host, not about the world -- the engine
                    // version, the Luau version, the display's scale. Excluded
                    // for the same reason a `ClassId` is above: it is a property
                    // of the build or the machine rather than of the operation
                    // sequence R10 says this hash is a function of.
                    //
                    // Found by bumping the engine to 1.1.0, which moved every
                    // determinism trace at tick zero. The churn is not the
                    // problem; what it HIDES is. A release expected to move
                    // every trace is a release in which a real determinism
                    // regression looks exactly like the expected movement.
                    if (property.hostFact)
                        continue;
                    hasher.text(m_atoms.text(property.name));
                    hashValue(hasher, property.get(*this, id), *this);
                }
            }
        }
    });

    return hasher.digest();
}

} // namespace luaug::scene
