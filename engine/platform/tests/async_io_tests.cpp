#include "luaug/platform/async_io.h"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace luaug::platform;
using luaug::core::u32;
using luaug::core::usize;

namespace {

// A directory of real files, because the point of this module is that SDL --
// not a mock -- reads them. Removed on the way out so a failing case does not
// leave the machine dirty.
struct Fixture
{
    std::filesystem::path root;

    // Deliberately does NOT call platform::init: SDL's async file IO needs no
    // subsystem, and a fixture that brought one up would leave it up for the
    // cases in this suite that assert the process starts uninitialized.
    explicit Fixture(u32 maxInFlight)
    {
        std::error_code ec;
        root = std::filesystem::temp_directory_path(ec) / "luaug-async-io-tests";
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
        REQUIRE(std::filesystem::is_directory(root));

        REQUIRE(initIo(maxInFlight));
        resetIoStats();
    }

    ~Fixture()
    {
        shutdownIo();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    std::filesystem::path write(const std::string& name, const std::string& contents) const
    {
        const std::filesystem::path path = root / name;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.close();
        return path;
    }
};

// Pumps until the request leaves `Pending` or the budget runs out. Bounded
// rather than open-ended, because the bound is what turns "the disk hung" into
// a failing assertion instead of a hung suite -- and it is bounded in TIME
// rather than in iterations, since how many pumps fit into one disk read is
// exactly the thing that differs between this machine and a CI runner.
constexpr int MaxWaitMillis = 5000;

// Waits until the submitter has actually handed a read to SDL.
//
// Needed since D038 moved admission onto its own thread: `readFileAsync` now
// queues and returns, so "submitted" and "in flight" are two moments with a
// scheduler between them. Every case below that reasons about QUEUE ORDER has
// to pin the first one down first, or it is racing the submitter rather than
// testing the queue.
void waitUntilInFlight(u32 count)
{
    for (int i = 0; i < MaxWaitMillis && ioStats().inFlight < count; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(ioStats().inFlight >= count);
}

[[nodiscard]] IoStatus pumpUntilSettled(IoRequest request)
{
    for (int i = 0; i < MaxWaitMillis; ++i) {
        const IoStatus status = ioStatus(request);
        if (status != IoStatus::Pending) {
            return status;
        }
        pumpIo();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ioStatus(request);
}

// The same bound, for the cases that watch a callback rather than a status.
template <class Predicate>
void pumpUntil(Predicate done)
{
    for (int i = 0; i < MaxWaitMillis && !done(); ++i) {
        pumpIo();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

[[nodiscard]] std::string asString(const std::vector<std::byte>& bytes)
{
    std::string out;
    out.resize(bytes.size());
    if (!bytes.empty()) {
        std::memcpy(out.data(), bytes.data(), bytes.size());
    }
    return out;
}

} // namespace

TEST_CASE("an async read delivers the file's bytes")
{
    Fixture fixture(4);
    // Built with an explicit length: a std::string from this literal would stop
    // at the nul, and the point of the case is that the reader does not.
    const std::string contents("the quick brown fox\0with an embedded nul", 40);
    const std::filesystem::path path = fixture.write("plain.bin", contents);

    const IoRequest request = readFileAsync(path, IoPriority::Normal);
    REQUIRE(request.valid());
    REQUIRE(pumpUntilSettled(request) == IoStatus::Ready);

    std::vector<std::byte> bytes;
    REQUIRE(takeIoResult(request, bytes));
    CHECK(bytes.size() == 40);
    CHECK(asString(bytes) == contents);

    // Taking releases the slot, so the handle now names nothing.
    CHECK(ioStatus(request) == IoStatus::Unknown);
    CHECK_FALSE(takeIoResult(request, bytes));

    const IoStats stats = ioStats();
    CHECK(stats.completed == 1);
    CHECK(stats.bytesRead == 40);
}

TEST_CASE("an empty file is a success with no bytes")
{
    Fixture fixture(4);
    const std::filesystem::path path = fixture.write("empty.bin", "");

    const IoRequest request = readFileAsync(path, IoPriority::Normal);
    REQUIRE(pumpUntilSettled(request) == IoStatus::Ready);

    std::vector<std::byte> bytes;
    REQUIRE(takeIoResult(request, bytes));
    CHECK(bytes.empty());
}

TEST_CASE("a missing file fails rather than hanging")
{
    Fixture fixture(4);
    const IoRequest request = readFileAsync(fixture.root / "does-not-exist.bin", IoPriority::Normal);
    REQUIRE(request.valid());

    const IoStatus status = pumpUntilSettled(request);
    CHECK(status == IoStatus::Failed);
    CHECK(ioStats().failed == 1);
}

TEST_CASE("a callback fires during the pump and releases the request")
{
    Fixture fixture(4);
    const std::filesystem::path path = fixture.write("callback.bin", "payload");

    int calls = 0;
    std::string received;
    IoStatus seen = IoStatus::Unknown;
    const IoRequest request =
        readFileAsync(path, IoPriority::High, [&](IoRequest, IoStatus status, std::vector<std::byte>&& bytes) {
            calls += 1;
            seen = status;
            received = asString(bytes);
        });
    REQUIRE(request.valid());

    pumpUntil([&calls] { return calls > 0; });

    CHECK(calls == 1);
    CHECK(seen == IoStatus::Ready);
    CHECK(received == "payload");
    // The callback was the one chance to keep the bytes.
    CHECK(ioStatus(request) == IoStatus::Unknown);
}

TEST_CASE("priority decides which queued read is admitted next")
{
    // One in flight at a time, which is what makes the queue observable at all.
    Fixture fixture(1);
    fixture.write("first.bin", "1");
    fixture.write("low.bin", "2");
    fixture.write("critical.bin", "3");

    std::vector<std::string> order;
    const auto record = [&order](const char* name) {
        return [&order, name](IoRequest, IoStatus, std::vector<std::byte>&&) { order.emplace_back(name); };
    };

    // The first is admitted immediately and occupies the only slot; the other
    // two wait, and the more urgent of them must go next even though it was
    // submitted last.
    const IoRequest first = readFileAsync(fixture.root / "first.bin", IoPriority::Normal, record("first"));
    waitUntilInFlight(1);
    const IoRequest low = readFileAsync(fixture.root / "low.bin", IoPriority::Low, record("low"));
    const IoRequest critical = readFileAsync(fixture.root / "critical.bin", IoPriority::Critical, record("critical"));
    REQUIRE(first.valid());
    REQUIRE(low.valid());
    REQUIRE(critical.valid());

    pumpUntil([&order] { return order.size() >= 3; });

    REQUIRE(order.size() == 3);
    CHECK(order[0] == "first");
    CHECK(order[1] == "critical");
    CHECK(order[2] == "low");
}

TEST_CASE("raising a queued request's priority moves it up the queue")
{
    Fixture fixture(1);
    fixture.write("blocker.bin", "1");
    fixture.write("a.bin", "2");
    fixture.write("b.bin", "3");

    std::vector<std::string> order;
    const auto record = [&order](const char* name) {
        return [&order, name](IoRequest, IoStatus, std::vector<std::byte>&&) { order.emplace_back(name); };
    };

    const IoRequest blocker = readFileAsync(fixture.root / "blocker.bin", IoPriority::Normal, record("blocker"));
    // The blocker has to be IN FLIGHT before the other two are queued, or the
    // submitter may pick one of them first and this stops being a test about
    // the queue. See `waitUntilInFlight`.
    waitUntilInFlight(1);
    const IoRequest a = readFileAsync(fixture.root / "a.bin", IoPriority::Normal, record("a"));
    const IoRequest b = readFileAsync(fixture.root / "b.bin", IoPriority::Normal, record("b"));
    REQUIRE(blocker.valid());
    REQUIRE(a.valid());
    REQUIRE(b.valid());

    // Same band, so `a` would go first on submission order alone. A chunk the
    // focus turned towards is why this call exists.
    setIoPriority(b, IoPriority::Critical);

    pumpUntil([&order] { return order.size() >= 3; });

    REQUIRE(order.size() == 3);
    CHECK(order[0] == "blocker");
    CHECK(order[1] == "b");
    CHECK(order[2] == "a");
}

TEST_CASE("cancelling a queued read releases it without reading anything")
{
    Fixture fixture(1);
    fixture.write("held.bin", "1");
    fixture.write("dropped.bin", "2");

    const IoRequest held = readFileAsync(fixture.root / "held.bin", IoPriority::Normal);
    // The held read has to occupy the only in-flight place before the second is
    // queued, or "queued" below counts a read the submitter simply has not got
    // to yet (D038 put admission on its own thread).
    waitUntilInFlight(1);
    const IoRequest dropped = readFileAsync(fixture.root / "dropped.bin", IoPriority::Normal);
    REQUIRE(held.valid());
    REQUIRE(dropped.valid());
    CHECK(ioStats().queued == 1);

    cancelIo(dropped);
    CHECK(ioStatus(dropped) == IoStatus::Unknown);
    CHECK(ioStats().queued == 0);
    CHECK(ioStats().cancelled == 1);

    REQUIRE(pumpUntilSettled(held) == IoStatus::Ready);
    std::vector<std::byte> bytes;
    CHECK(takeIoResult(held, bytes));
    // Only the one that was not cancelled ever reached SDL.
    CHECK(ioStats().issued == 1);
}

TEST_CASE("many reads in flight all land")
{
    Fixture fixture(4);
    constexpr int count = 64;
    std::vector<IoRequest> requests;
    requests.reserve(count);
    for (int i = 0; i < count; ++i) {
        const std::string name = "file" + std::to_string(i) + ".bin";
        fixture.write(name, std::string(static_cast<usize>(i) + 1, 'x'));
        requests.push_back(readFileAsync(fixture.root / name, IoPriority::Normal));
        REQUIRE(requests.back().valid());
    }

    for (int i = 0; i < count; ++i) {
        REQUIRE(pumpUntilSettled(requests[static_cast<usize>(i)]) == IoStatus::Ready);
        std::vector<std::byte> bytes;
        REQUIRE(takeIoResult(requests[static_cast<usize>(i)], bytes));
        CHECK(bytes.size() == static_cast<usize>(i) + 1);
    }
    CHECK(ioStats().completed == static_cast<luaug::core::u64>(count));
}

TEST_CASE("a read requested without the service running is refused rather than lost")
{
    shutdownIo();
    CHECK_FALSE(isIoInitialized());

    const IoRequest request = readFileAsync("anything.bin", IoPriority::Normal);
    // An invalid handle is a refusal the caller can see. The alternative -- a
    // valid-looking request that never lands -- is a bug that surfaces as a
    // chunk that never appears.
    CHECK_FALSE(request.valid());
    CHECK(ioStatus(request) == IoStatus::Unknown);
}
