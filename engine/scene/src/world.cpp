#include "luaug/scene/world.h"

#include <algorithm>

namespace luaug::scene
{
namespace
{

// api-design.md §2.2: attributes hold the value datatypes and nothing else. A
// table, a function or an Instance reference is rejected, and the caller raises
// `scene.err.attribute_type`. Instances are excluded deliberately: an attribute
// holding one would be a second kind of tree edge, invisible to `Destroy` and
// to serialization, and nothing in the design maintains it.
[[nodiscard]] bool isAttributeValue(const Value& value) noexcept
{
    switch (valueType(value))
    {
    case ValueType::Nil:
    case ValueType::Bool:
    case ValueType::Number:
    case ValueType::String:
    case ValueType::Vector3:
    case ValueType::CFrame:
    case ValueType::Color3:
        return true;
    case ValueType::Instance:
    case ValueType::EnumItem:
        return false;
    }
    return false;
}

} // namespace

World::World(ClassRegistry& classes, EnumRegistry& enums, core::AtomTable& atoms, u64 seed)
    : m_classes(classes)
    , m_enums(enums)
    , m_atoms(atoms)
    , m_rng(seed)
    , m_parentProperty(atoms.intern("Parent"))
{
}

// --- Lifetime ---------------------------------------------------------------

core::InstanceId World::create(ClassId classId)
{
    const ClassDescriptor* descriptor = m_classes.find(classId);
    if (descriptor == nullptr || hasFlag(descriptor->flags, ClassFlags::Abstract))
        return {};

    // `Service` and `NotCreatable` are NOT checked here. The engine has to be
    // able to build a service and the DataModel itself; what those tags forbid
    // is `Instance.new`, which is a rule about the *script-facing* constructor
    // and is enforced in `script` where the error can be raised (api-design.md
    // §2.2).
    InstanceRecord record;
    record.classId = classId;
    record.name = descriptor->defaultName.valid() ? descriptor->defaultName : descriptor->name;

    const core::InstanceId id = m_instances.insert(record);

    // Root-first, so a subclass's hook runs after the base's and can rely on
    // the base component existing.
    const ClassDescriptor* current = descriptor;
    std::vector<const ClassDescriptor*> chain;
    while (current != nullptr)
    {
        chain.push_back(current);
        current = m_classes.find(current->super);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        if ((*it)->attachComponents != nullptr)
            (*it)->attachComponents(*this, id);
    }

    return id;
}

bool World::destroy(core::InstanceId id)
{
    InstanceRecord* record = m_instances.find(id);
    if (record == nullptr || record->destroyed)
        return false;

    // Detached first, so the tree is consistent before anything is told about
    // it: the `ChildRemoved` a handler eventually sees describes a tree the
    // handler can already observe. Doing it the other way round is how
    // `part.Parent` and `parent:GetChildren()` end up disagreeing inside a
    // handler.
    setParent(id, core::InstanceId{});

    std::vector<core::InstanceId> subtree;
    subtree.push_back(id);
    collectDescendants(id, subtree);

    // Parent before descendants, in document order. api-design.md §3.1 leaves
    // the order within one operation unstated for `Destroying` specifically;
    // this is the order the tree reads in, and it is written down here so that
    // it is a decision rather than an artefact of the traversal.
    for (const core::InstanceId member : subtree)
    {
        InstanceRecord* memberRecord = m_instances.find(member);
        if (memberRecord == nullptr || memberRecord->destroyed)
            continue;

        // Untagged before the destroy is announced, so a
        // `GetInstanceRemovedSignal` handler and a `Destroying` handler see the
        // same world (ruling G74).
        if (const TagSet* tags = m_tags.find(member); tags != nullptr)
        {
            const TagSet owned = *tags;
            for (const core::NameAtom tag : owned)
                removeTag(member, tag);
        }

        // Detached before it is marked, so the whole subtree ends unparented
        // rather than only its root: "its children have been destroyed
        // recursively" (api-design.md §3.1), and a destroyed child is a child
        // whose Parent is nil. Done before `destroyed` is set, because
        // `setParent` refuses a destroyed instance.
        if (member != id)
            (void)setParent(member, core::InstanceId{});

        memberRecord = m_instances.find(member);
        memberRecord->destroyed = true;
        m_changes.push({ChangeKind::Destroying, member, core::InstanceId{}, core::NameAtom{}});
        m_pendingRetire.push_back(member);
    }

    return true;
}

void World::retireDestroyed()
{
    for (const core::InstanceId id : m_pendingRetire)
    {
        const InstanceRecord* record = m_instances.find(id);
        if (record == nullptr)
            continue;

        if (const ClassDescriptor* descriptor = m_classes.find(record->classId); descriptor != nullptr)
        {
            const ClassDescriptor* current = descriptor;
            while (current != nullptr)
            {
                if (current->detachComponents != nullptr)
                    current->detachComponents(*this, id);
                current = m_classes.find(current->super);
            }
        }

        m_attributes.remove(id);
        m_tags.remove(id);
        m_nameIndices.remove(id);
        // The generation bump lives here and nowhere else: it is the single
        // point at which a handle stops resolving (divergence #25).
        m_instances.erase(id);
    }
    m_pendingRetire.clear();
}

bool World::alive(core::InstanceId id) const noexcept
{
    return m_instances.find(id) != nullptr;
}

ClassId World::classOf(core::InstanceId id) const noexcept
{
    const InstanceRecord* record = m_instances.find(id);
    return record == nullptr ? InvalidClass : record->classId;
}

bool World::isA(core::InstanceId id, ClassId base) const noexcept
{
    return m_classes.isA(classOf(id), base);
}

// --- Naming -----------------------------------------------------------------

core::NameAtom World::name(core::InstanceId id) const noexcept
{
    const InstanceRecord* record = m_instances.find(id);
    return record == nullptr ? core::NameAtom{} : record->name;
}

void World::setName(core::InstanceId id, core::NameAtom newName)
{
    InstanceRecord* record = m_instances.find(id);
    if (record == nullptr || record->name == newName)
        return;

    const core::InstanceId parent = record->parent;
    // Unindexed under the old name before the field changes, because the
    // unlink walks the chain the old name owns.
    if (parent.valid())
        unindexName(parent, id);

    record->name = newName;

    if (parent.valid())
        indexNameInChildOrder(parent, id);
}

// --- Hierarchy --------------------------------------------------------------

core::InstanceId World::parentOf(core::InstanceId id) const noexcept
{
    const InstanceRecord* record = m_instances.find(id);
    return record == nullptr ? core::InstanceId{} : record->parent;
}

core::InstanceId World::firstChild(core::InstanceId id) const noexcept
{
    const InstanceRecord* record = m_instances.find(id);
    return record == nullptr ? core::InstanceId{} : record->firstChild;
}

core::InstanceId World::nextSibling(core::InstanceId id) const noexcept
{
    const InstanceRecord* record = m_instances.find(id);
    return record == nullptr ? core::InstanceId{} : record->nextSibling;
}

u32 World::childCount(core::InstanceId id) const noexcept
{
    const InstanceRecord* record = m_instances.find(id);
    return record == nullptr ? 0u : record->childCount;
}

bool World::isAncestorOf(core::InstanceId id, core::InstanceId descendant) const noexcept
{
    if (!id.valid() || !descendant.valid() || id == descendant)
        return false;

    core::InstanceId walk = parentOf(descendant);
    while (walk.valid())
    {
        if (walk == id)
            return true;
        walk = parentOf(walk);
    }
    return false;
}

std::optional<core::TextKey> World::setParent(core::InstanceId id, core::InstanceId newParent)
{
    InstanceRecord* record = m_instances.find(id);
    if (record == nullptr)
        return LUAUG_TR("script.err.instance_dead");

    // A destroyed instance's Parent is locked at nil, and the lock outlasts the
    // handle: it holds for the whole window in which `Destroying` handlers can
    // still reach the instance.
    if (record->destroyed && newParent.valid())
        return LUAUG_TR("scene.err.parent_locked");

    const core::InstanceId oldParent = record->parent;
    if (oldParent == newParent)
        return std::nullopt; // No reorder, no events (api-design.md §2.2).

    if (newParent.valid())
    {
        if (newParent == id || isAncestorOf(id, newParent))
            return LUAUG_TR("scene.err.parent_cycle");
        if (m_instances.find(newParent) == nullptr)
            return LUAUG_TR("script.err.instance_dead");
    }

    // The subtree is captured once and reused for both the removing and the
    // added fan-outs, so the two describe exactly the same set even though the
    // tree changes in between.
    std::vector<core::InstanceId> subtree;
    subtree.push_back(id);
    collectDescendants(id, subtree);

    if (oldParent.valid())
    {
        unindexName(oldParent, id);
        unlinkChild(id);
        m_changes.push({ChangeKind::ChildRemoved, oldParent, id, core::NameAtom{}});
        // Nearest ancestor first: a handler that walks upward sees the same
        // order the tree does.
        for (core::InstanceId ancestor = oldParent; ancestor.valid(); ancestor = parentOf(ancestor))
        {
            for (const core::InstanceId member : subtree)
                m_changes.push({ChangeKind::DescendantRemoving, ancestor, member, core::NameAtom{}});
        }
    }

    if (newParent.valid())
    {
        InstanceRecord* parentRecord = m_instances.find(newParent);
        linkChild(*parentRecord, newParent, id);
        indexName(newParent, id);
        m_changes.push({ChangeKind::ChildAdded, newParent, id, core::NameAtom{}});
        for (core::InstanceId ancestor = newParent; ancestor.valid(); ancestor = parentOf(ancestor))
        {
            for (const core::InstanceId member : subtree)
                m_changes.push({ChangeKind::DescendantAdded, ancestor, member, core::NameAtom{}});
        }
    }
    else
    {
        record->parent = core::InstanceId{};
    }

    // Every member's ancestry changed, not just the moved instance's, and each
    // is told about its OWN parent rather than about the moved one's.
    for (const core::InstanceId member : subtree)
        m_changes.push({ChangeKind::AncestryChanged, member, parentOf(member), core::NameAtom{}});

    return std::nullopt;
}

core::InstanceId World::findFirstChild(core::InstanceId parent, core::NameAtom childName) const noexcept
{
    const NameIndex* index = m_nameIndices.find(parent);
    if (index == nullptr)
        return {};
    const auto it = index->firstByName.find(childName.id);
    return it == index->firstByName.end() ? core::InstanceId{} : it->second;
}

core::InstanceId World::findFirstChildOfClass(core::InstanceId parent, ClassId classId) const noexcept
{
    for (core::InstanceId child = firstChild(parent); child.valid(); child = nextSibling(child))
    {
        if (classOf(child) == classId)
            return child;
    }
    return {};
}

core::InstanceId World::findFirstChildWhichIsA(core::InstanceId parent, ClassId base) const noexcept
{
    for (core::InstanceId child = firstChild(parent); child.valid(); child = nextSibling(child))
    {
        if (m_classes.isA(classOf(child), base))
            return child;
    }
    return {};
}

core::InstanceId World::findFirstAncestor(core::InstanceId id, core::NameAtom ancestorName) const noexcept
{
    for (core::InstanceId walk = parentOf(id); walk.valid(); walk = parentOf(walk))
    {
        if (name(walk) == ancestorName)
            return walk;
    }
    return {};
}

core::InstanceId World::findFirstAncestorOfClass(core::InstanceId id, ClassId classId) const noexcept
{
    for (core::InstanceId walk = parentOf(id); walk.valid(); walk = parentOf(walk))
    {
        if (classOf(walk) == classId)
            return walk;
    }
    return {};
}

void World::collectChildren(core::InstanceId id, std::vector<core::InstanceId>& out) const
{
    for (core::InstanceId child = firstChild(id); child.valid(); child = nextSibling(child))
        out.push_back(child);
}

void World::collectDescendants(core::InstanceId id, std::vector<core::InstanceId>& out) const
{
    // Depth-first preorder: each child immediately followed by its own subtree,
    // which is the document order `FindFirstChild` tie-breaks on
    // (api-design.md §2.2). Iterative because a deep tree is a script's to
    // build, and a recursive walk would put the stack depth in its hands.
    std::vector<core::InstanceId> stack;
    for (core::InstanceId child = firstChild(id); child.valid(); child = nextSibling(child))
        stack.push_back(child);
    std::reverse(stack.begin(), stack.end());

    while (!stack.empty())
    {
        const core::InstanceId current = stack.back();
        stack.pop_back();
        out.push_back(current);

        const usize mark = stack.size();
        for (core::InstanceId child = firstChild(current); child.valid(); child = nextSibling(child))
            stack.push_back(child);
        std::reverse(stack.begin() + static_cast<std::ptrdiff_t>(mark), stack.end());
    }
}

core::InstanceId World::clone(core::InstanceId id)
{
    const InstanceRecord* source = m_instances.find(id);
    if (source == nullptr || source->destroyed)
        return {};

    std::vector<core::InstanceId> sources;
    sources.push_back(id);
    collectDescendants(id, sources);

    // Built before any property is copied, because a reference into the subtree
    // has to resolve to the clone of its target even when that target is copied
    // later (api-design.md §2.6).
    std::unordered_map<u64, core::InstanceId> mapping;
    const auto key = [](core::InstanceId value) noexcept {
        return (static_cast<u64>(value.index) << 32) | value.generation;
    };

    for (const core::InstanceId original : sources)
    {
        const InstanceRecord* record = m_instances.find(original);
        const core::InstanceId copy = create(record->classId);
        if (!copy.valid())
            return {};
        setName(copy, record->name);
        mapping[key(original)] = copy;
    }

    for (const core::InstanceId original : sources)
    {
        const core::InstanceId copy = mapping[key(original)];

        // Parenting inside the copy, which is also what gives the clone its
        // child order: `sources` is in document order, so each child is
        // appended after its earlier siblings.
        if (original != id)
        {
            const core::InstanceId originalParent = parentOf(original);
            setParent(copy, mapping[key(originalParent)]);
        }

        const ClassDescriptor* descriptor = m_classes.find(classOf(original));
        for (const ClassDescriptor* current = descriptor; current != nullptr;
             current = m_classes.find(current->super))
        {
            for (const PropertyDesc& property : current->properties)
            {
                if (property.readOnly || property.get == nullptr || property.set == nullptr)
                    continue;
                // `Parent` is structure rather than a value, and the loop above
                // has already set it. Copying it here would parent the clone
                // where the original sits -- which is the opposite of
                // api-design.md §2.6's "deep-copies this subtree unparented",
                // and it is the property whose setter happens to accept it.
                if (property.name == m_parentProperty)
                    continue;
                Value value = property.get(*this, original);
                // An Instance-valued property pointing inside the subtree is
                // rewired to the copy; one pointing outside keeps the original,
                // which is the difference between cloning a model and cloning
                // the world it sits in.
                if (const auto* reference = std::get_if<core::InstanceId>(&value); reference != nullptr)
                {
                    if (const auto found = mapping.find(key(*reference)); found != mapping.end())
                        value = found->second;
                }
                property.set(*this, copy, value);
            }
        }

        if (const AttributeMap* attributes = m_attributes.find(original); attributes != nullptr)
            m_attributes.add(copy, *attributes);
        if (const TagSet* tags = m_tags.find(original); tags != nullptr)
        {
            const TagSet owned = *tags;
            for (const core::NameAtom tag : owned)
                addTag(copy, tag);
        }
    }

    return mapping[key(id)];
}

// --- Properties -------------------------------------------------------------

std::optional<Value> World::getProperty(core::InstanceId id, core::NameAtom property) const
{
    const InstanceRecord* record = m_instances.find(id);
    if (record == nullptr)
        return std::nullopt;

    const PropertyDesc* descriptor = m_classes.findProperty(record->classId, property);
    if (descriptor == nullptr || descriptor->get == nullptr)
        return std::nullopt;
    return descriptor->get(*this, id);
}

World::SetResult World::setProperty(core::InstanceId id, core::NameAtom property, const Value& value)
{
    InstanceRecord* record = m_instances.find(id);
    if (record == nullptr)
        return SetResult::UnknownProperty;

    const PropertyDesc* descriptor = m_classes.findProperty(record->classId, property);
    if (descriptor == nullptr)
        return SetResult::UnknownProperty;
    if (descriptor->readOnly)
        return SetResult::ReadOnly;
    if (descriptor->get == nullptr || descriptor->set == nullptr)
        return SetResult::UnknownProperty;

    // Read-compare-write, because api-design.md §3.1 makes the change signal a
    // past-tense fact about a *change*. The comparison also carries the quiet
    // path: 10k parts moving every tick enqueue nothing while nobody is
    // listening, and the subscription mask is the second half of that.
    const Value previous = descriptor->get(*this, id);
    if (previous == value)
        return SetResult::Unchanged;

    if (!descriptor->set(*this, id, value))
        return SetResult::InvalidValue;

    const u16 slot = m_classes.propertySlot(record->classId, property);
    // Past 64 properties the mask cannot say, so the write is loud. Correct,
    // and slower, for a class nothing in v1 has.
    const bool subscribed = slot >= 64 || (record->subscribedProperties & (u64{1} << slot)) != 0;
    if (subscribed)
        m_changes.push({ChangeKind::PropertyChanged, id, core::InstanceId{}, property});

    return SetResult::Changed;
}

void World::setPropertySubscribed(core::InstanceId id, core::NameAtom property, bool subscribed)
{
    InstanceRecord* record = m_instances.find(id);
    if (record == nullptr)
        return;

    const u16 slot = m_classes.propertySlot(record->classId, property);
    // Past 64 the mask cannot represent it, and `setProperty` treats that as
    // always-subscribed, so there is nothing to record.
    if (slot >= 64)
        return;

    if (subscribed)
        record->subscribedProperties |= (u64{1} << slot);
    else
        record->subscribedProperties &= ~(u64{1} << slot);
}

// --- Attributes and tags ----------------------------------------------------

Value World::getAttribute(core::InstanceId id, core::NameAtom attribute) const
{
    const AttributeMap* attributes = m_attributes.find(id);
    if (attributes == nullptr)
        return Value{};
    for (const auto& entry : *attributes)
    {
        if (entry.first == attribute)
            return entry.second;
    }
    return Value{};
}

bool World::setAttribute(core::InstanceId id, core::NameAtom attribute, const Value& value)
{
    if (m_instances.find(id) == nullptr || !attribute.valid())
        return false;
    if (!isAttributeValue(value))
        return false;

    AttributeMap* attributes = m_attributes.find(id);
    if (attributes == nullptr)
        attributes = &m_attributes.add(id, AttributeMap{});

    const auto found = std::find_if(attributes->begin(), attributes->end(), [&](const auto& entry) {
        return entry.first == attribute;
    });
    const bool removing = valueType(value) == ValueType::Nil;

    if (found == attributes->end())
    {
        if (removing)
            return true; // Setting an absent attribute to nil changes nothing.
        attributes->emplace_back(attribute, value);
    }
    else
    {
        if (found->second == value)
            return true; // Equality-filtered like a property (api-design.md §3.1).
        if (removing)
            attributes->erase(found);
        else
            found->second = value;
    }

    m_changes.push({ChangeKind::AttributeChanged, id, core::InstanceId{}, attribute});
    return true;
}

void World::collectAttributes(core::InstanceId id, AttributeMap& out) const
{
    if (const AttributeMap* attributes = m_attributes.find(id); attributes != nullptr)
        out.insert(out.end(), attributes->begin(), attributes->end());
}

bool World::addTag(core::InstanceId id, core::NameAtom tag)
{
    if (m_instances.find(id) == nullptr || !tag.valid())
        return false;

    TagSet* tags = m_tags.find(id);
    if (tags == nullptr)
        tags = &m_tags.add(id, TagSet{});
    if (std::find(tags->begin(), tags->end(), tag) != tags->end())
        return true; // Idempotent, and enqueues nothing the second time.

    tags->push_back(tag);
    m_tagged[tag.id].push_back(id);
    m_changes.push({ChangeKind::TagAdded, id, core::InstanceId{}, tag});
    return true;
}

bool World::removeTag(core::InstanceId id, core::NameAtom tag)
{
    TagSet* tags = m_tags.find(id);
    if (tags == nullptr)
        return false;
    const auto found = std::find(tags->begin(), tags->end(), tag);
    if (found == tags->end())
        return false;

    tags->erase(found);
    if (const auto bucket = m_tagged.find(tag.id); bucket != m_tagged.end())
    {
        auto& members = bucket->second;
        members.erase(std::remove(members.begin(), members.end(), id), members.end());
        // A tag with no carriers stops existing, which is what makes
        // `GetAllTags` mean "currently carried" (ruling G75).
        if (members.empty())
            m_tagged.erase(bucket);
    }

    m_changes.push({ChangeKind::TagRemoved, id, core::InstanceId{}, tag});
    return true;
}

bool World::hasTag(core::InstanceId id, core::NameAtom tag) const noexcept
{
    const TagSet* tags = m_tags.find(id);
    if (tags == nullptr)
        return false;
    return std::find(tags->begin(), tags->end(), tag) != tags->end();
}

void World::collectTags(core::InstanceId id, TagSet& out) const
{
    if (const TagSet* tags = m_tags.find(id); tags != nullptr)
        out.insert(out.end(), tags->begin(), tags->end());
}

void World::collectTagged(core::NameAtom tag, std::vector<core::InstanceId>& out) const
{
    if (const auto bucket = m_tagged.find(tag.id); bucket != m_tagged.end())
        out.insert(out.end(), bucket->second.begin(), bucket->second.end());
}

void World::collectAllTags(TagSet& out) const
{
    for (const auto& bucket : m_tagged)
        out.push_back(core::NameAtom{bucket.first});
    // The map's own order is a hash order, and R10 forbids one reaching
    // observable state. Sorted by the atom's TEXT rather than by its numeric
    // value, because an atom's number depends on intern order -- which depends
    // on what the world happened to build first.
    std::sort(out.begin(), out.end(), [this](core::NameAtom a, core::NameAtom b) {
        return m_atoms.text(a) < m_atoms.text(b);
    });
}

// --- Private links ----------------------------------------------------------

void World::linkChild(InstanceRecord& parentRecord, core::InstanceId parentId, core::InstanceId childId)
{
    InstanceRecord* child = m_instances.find(childId);
    child->parent = parentId;
    child->nextSibling = core::InstanceId{};
    child->prevSibling = parentRecord.lastChild;

    // Appended last: child order is parenting order, and a re-parented child
    // goes to the end (api-design.md §2.2).
    if (parentRecord.lastChild.valid())
        m_instances.find(parentRecord.lastChild)->nextSibling = childId;
    else
        parentRecord.firstChild = childId;

    parentRecord.lastChild = childId;
    ++parentRecord.childCount;
}

void World::unlinkChild(core::InstanceId childId)
{
    InstanceRecord* child = m_instances.find(childId);
    InstanceRecord* parent = m_instances.find(child->parent);
    if (parent == nullptr)
        return;

    if (child->prevSibling.valid())
        m_instances.find(child->prevSibling)->nextSibling = child->nextSibling;
    else
        parent->firstChild = child->nextSibling;

    if (child->nextSibling.valid())
        m_instances.find(child->nextSibling)->prevSibling = child->prevSibling;
    else
        parent->lastChild = child->prevSibling;

    --parent->childCount;
    child->parent = core::InstanceId{};
    child->prevSibling = core::InstanceId{};
    child->nextSibling = core::InstanceId{};
}

void World::indexName(core::InstanceId parentId, core::InstanceId childId)
{
    const core::NameAtom childName = name(childId);
    if (!childName.valid())
        return;

    NameIndex* index = m_nameIndices.find(parentId);
    if (index == nullptr)
        index = &m_nameIndices.add(parentId, NameIndex{});

    const auto found = index->firstByName.find(childName.id);
    if (found == index->firstByName.end())
    {
        index->firstByName.emplace(childName.id, childId);
        return;
    }

    // Appended to the end of the same-name chain, so the chain stays in child
    // order and `FindFirstChild` keeps returning the first in document order
    // (ADR 0026). O(chain), which is the cost that ADR accepted.
    core::InstanceId walk = found->second;
    while (m_instances.find(walk)->nextSameName.valid())
        walk = m_instances.find(walk)->nextSameName;
    m_instances.find(walk)->nextSameName = childId;
}

void World::indexNameInChildOrder(core::InstanceId parentId, core::InstanceId childId)
{
    const core::NameAtom childName = name(childId);
    if (!childName.valid())
        return;

    NameIndex* index = m_nameIndices.find(parentId);
    if (index == nullptr)
        index = &m_nameIndices.add(parentId, NameIndex{});

    // A rename can put a child back in the MIDDLE of a chain, which appending
    // cannot express: rename the first of three "Tree"s away and back, and an
    // append leaves it last while child order still puts it first. So this walks
    // the parent's children and inserts after the last same-name sibling that
    // precedes `childId`.
    //
    // Deliberately separate from `indexName` rather than replacing it. Parenting
    // appends a child at the end, where an append IS child order, and parenting
    // is the path the 10k-parts benchmark runs down -- making it O(children)
    // would make building a world O(n squared).
    core::InstanceId previous;
    for (core::InstanceId cursor = firstChild(parentId); cursor.valid(); cursor = nextSibling(cursor))
    {
        if (cursor == childId)
            break;
        if (name(cursor) == childName)
            previous = cursor;
    }

    InstanceRecord* record = m_instances.find(childId);
    if (!previous.valid())
    {
        const auto head = index->firstByName.find(childName.id);
        record->nextSameName = head == index->firstByName.end() ? core::InstanceId{} : head->second;
        index->firstByName[childName.id] = childId;
        return;
    }

    InstanceRecord* previousRecord = m_instances.find(previous);
    record->nextSameName = previousRecord->nextSameName;
    previousRecord->nextSameName = childId;
}

void World::unindexName(core::InstanceId parentId, core::InstanceId childId)
{
    const core::NameAtom childName = name(childId);
    NameIndex* index = m_nameIndices.find(parentId);
    if (index == nullptr || !childName.valid())
        return;

    const auto found = index->firstByName.find(childName.id);
    if (found == index->firstByName.end())
        return;

    InstanceRecord* child = m_instances.find(childId);
    if (found->second == childId)
    {
        if (child->nextSameName.valid())
            found->second = child->nextSameName;
        else
            index->firstByName.erase(found);
    }
    else
    {
        core::InstanceId walk = found->second;
        while (walk.valid() && m_instances.find(walk)->nextSameName != childId)
            walk = m_instances.find(walk)->nextSameName;
        if (walk.valid())
            m_instances.find(walk)->nextSameName = child->nextSameName;
    }

    child->nextSameName = core::InstanceId{};
}

} // namespace luaug::scene
