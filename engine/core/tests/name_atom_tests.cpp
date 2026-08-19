#include <doctest/doctest.h>

// doctest prints a failing comparison through operator<<, and the accessor here
// hands back std::string_view.
#include <ostream>
#include <string>
#include <vector>

#include "luaug/core/name_atom.h"

using luaug::core::AtomTable;
using luaug::core::NameAtom;
using luaug::core::usize;

namespace doctest
{

// Without this a failing atom comparison reads `{?} == {?}`, and the ids are
// the one thing worth seeing when it fails.
template <>
struct StringMaker<NameAtom>
{
    static String convert(const NameAtom& atom) { return String("atom ") + std::to_string(atom.id).c_str(); }
};

} // namespace doctest

TEST_CASE("a fresh table already holds the empty name")
{
    // Slot 0 is spoken for from construction, which is what makes atom 0 mean
    // "absent" rather than "whatever was interned first".
    const AtomTable table;

    CHECK(table.size() == 1);
    CHECK(table.text(NameAtom{}).empty());
    CHECK_FALSE(NameAtom{}.valid());
}

TEST_CASE("the empty string is atom 0 and interning it does not grow the table")
{
    AtomTable table;

    const NameAtom empty = table.intern("");

    CHECK(empty.id == 0);
    CHECK_FALSE(empty.valid());
    CHECK(table.size() == 1);
    CHECK(table.lookup("").id == 0);
}

TEST_CASE("interning the same text twice returns the same atom")
{
    AtomTable table;

    const NameAtom first = table.intern("Baseplate");
    const NameAtom again = table.intern("Baseplate");

    CHECK(first == again);
    CHECK(first.valid());
    // Idempotent means idempotent in storage too: a second intern that appended
    // would make atom equality a weaker test than string equality, and every
    // atom-keyed index in the engine assumes it is a stronger one.
    CHECK(table.size() == 2);
}

TEST_CASE("distinct texts get distinct atoms")
{
    AtomTable table;

    const NameAtom part = table.intern("Part");
    const NameAtom model = table.intern("Model");
    const NameAtom folder = table.intern("Folder");

    CHECK(part != model);
    CHECK(model != folder);
    CHECK(part != folder);
    CHECK(table.size() == 4);

    CHECK(table.text(part) == "Part");
    CHECK(table.text(model) == "Model");
    CHECK(table.text(folder) == "Folder");
}

TEST_CASE("interning is case- and byte-sensitive")
{
    AtomTable table;

    CHECK(table.intern("Part") != table.intern("part"));
    // A view carrying an embedded NUL is one string, not two -- the table keys
    // on bytes and length, never on C-string termination.
    const std::string withNul("a\0b", 3);
    CHECK(table.intern(withNul) != table.intern("a"));
    CHECK(table.text(table.intern(withNul)).size() == 3);
}

TEST_CASE("lookup answers a miss without growing the table")
{
    // FindFirstChild on a name nothing in the world has ever been called goes
    // through here. If a miss interned, a script polling for a name that does
    // not exist would grow the atom table without bound -- a leak with no
    // symptom until it is the whole heap.
    AtomTable table;
    const NameAtom known = table.intern("Known");
    const usize before = table.size();

    const NameAtom missing = table.lookup("NeverInterned");

    CHECK_FALSE(missing.valid());
    CHECK(missing.id == 0);
    CHECK(table.size() == before);
    CHECK(table.lookup("Known") == known);
}

TEST_CASE("text of an unknown or invalid atom is empty, not a dangling view")
{
    AtomTable table;
    (void)table.intern("Only");

    CHECK(table.text(NameAtom{}).empty());
    CHECK(table.text(NameAtom{99}).empty());
    // An atom from a different table is indistinguishable from a hand-built id,
    // so out-of-range has to read as empty rather than as a bounds violation.
    CHECK(table.text(NameAtom{0xFFFFFFFFu}).empty());
}

TEST_CASE("hundreds of interned names stay addressable after the storage grows")
{
    // The one that matters. `m_byText`'s keys are views into the strings the
    // table owns, and growing the storage moves those strings: a short string
    // carries its characters inside the string object, so every view into one is
    // left pointing at freed memory. The symptom is not a crash -- it is a
    // lookup miss, and a lookup miss makes `intern` append a *second* atom for a
    // name that already had one, silently breaking the equality that the
    // child-name index and the property dispatch table are both built on.
    //
    // Both lengths are interned on purpose: a table of only long names would
    // pass this test with the bug present.
    AtomTable table;
    std::vector<std::string> names;
    std::vector<NameAtom> atoms;

    for (int index = 0; index < 400; ++index)
    {
        std::string name = (index % 2 == 0) ? "n" + std::to_string(index)
                                            : "a_considerably_longer_instance_name_" + std::to_string(index);
        atoms.push_back(table.intern(name));
        names.push_back(std::move(name));
    }

    REQUIRE(table.size() == names.size() + 1);

    for (usize index = 0; index < names.size(); ++index)
    {
        // Fetched again rather than held from before the growth: `text()` reads
        // the string the table owns *now*, which is the only view a caller can
        // rely on across an intern.
        CHECK(table.text(atoms[index]) == names[index]);
        CHECK(table.lookup(names[index]) == atoms[index]);
        CHECK(table.intern(names[index]) == atoms[index]);
    }

    // Nothing above appended, so the count is still what it was.
    CHECK(table.size() == names.size() + 1);
}

TEST_CASE("atom ids are assigned in intern order")
{
    // Not a promise to the rest of the engine -- name_atom.h is explicit that an
    // atom's numeric value is not data and must never be hashed, sorted or
    // serialised. It is pinned here only so that a change to the assignment
    // scheme is a deliberate edit to this test rather than a silent one.
    AtomTable table;

    CHECK(table.intern("first").id == 1);
    CHECK(table.intern("second").id == 2);
    CHECK(table.intern("first").id == 1);
    CHECK(table.intern("third").id == 3);
}
