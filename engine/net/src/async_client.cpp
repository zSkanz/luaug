#include "luaug/net/async_client.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace luaug::net {

struct AsyncClient::Impl
{
    struct Job
    {
        u64 ticket = 0;
        HttpRequest request;
    };

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> queue;
    std::unordered_map<u64, NetResult> done;

    // How many results are sitting in `done`, readable WITHOUT the mutex.
    //
    // This is not an optimisation, it is the fix for a real starvation, and the
    // frame loop is what produced it. `take` is called once per frame for every
    // outstanding ticket; a headless run on the null backend has no frame pacing
    // at all, so that is a tight loop locking and unlocking this mutex a hundred
    // thousand times. `std::mutex` is not fair -- a thread reacquiring in a tight
    // loop can hold it essentially continuously -- and the worker, which needs it
    // for the single instant it takes to file a finished result, never got in.
    // The symptom was a request that completed on the socket and never came back
    // to Luau.
    //
    // A poll that finds nothing now touches no lock, so there is nothing to
    // starve. Release/acquire pairs the store with the `done` insert that
    // precedes it: a reader that sees a non-zero count sees the entry too.
    std::atomic<usize> completed{0};
    std::vector<std::thread> workers;
    u64 nextTicket = 1;
    // Counts submitted-and-not-yet-taken, which is neither `queue.size()` nor
    // `done.size()`: a job being worked on right now is in neither container.
    usize inFlight = 0;
    bool stopping = false;

    void work()
    {
        while (true) {
            Job job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || !queue.empty(); });
                if (stopping) {
                    return;
                }
                job = std::move(queue.front());
                queue.pop_front();
            }

            // Outside the lock, and this is the whole point of the class: the
            // call blocks for as long as the server takes, and nothing else
            // waits on it.
            NetResult result;
            result.error = performHttp(job.request, result.response);

            {
                std::lock_guard lock(mutex);
                if (stopping) {
                    return;
                }
                done.emplace(job.ticket, std::move(result));
                completed.fetch_add(1, std::memory_order_release);
            }
        }
    }
};

AsyncClient::AsyncClient(usize workers) : m_impl(std::make_unique<Impl>())
{
    if (workers == 0) {
        workers = 1;
    }
    m_impl->workers.reserve(workers);
    for (usize i = 0; i < workers; ++i) {
        m_impl->workers.emplace_back([impl = m_impl.get()] { impl->work(); });
    }
}

AsyncClient::~AsyncClient()
{
    shutdown();
}

NetTicket AsyncClient::submit(HttpRequest request)
{
    std::lock_guard lock(m_impl->mutex);
    if (m_impl->stopping) {
        return NetTicket{};
    }
    const u64 ticket = m_impl->nextTicket++;
    m_impl->queue.push_back({ticket, std::move(request)});
    m_impl->inFlight += 1;
    m_impl->wake.notify_one();
    return NetTicket{ticket};
}

bool AsyncClient::take(NetTicket ticket, NetResult& out)
{
    // The lock-free early out. See `completed` for why it is load-bearing
    // rather than a micro-optimisation.
    if (m_impl->completed.load(std::memory_order_acquire) == 0) {
        return false;
    }

    std::lock_guard lock(m_impl->mutex);
    const auto at = m_impl->done.find(ticket.value);
    if (at == m_impl->done.end()) {
        return false;
    }
    out = std::move(at->second);
    m_impl->done.erase(at);
    m_impl->completed.fetch_sub(1, std::memory_order_release);
    if (m_impl->inFlight > 0) {
        m_impl->inFlight -= 1;
    }
    return true;
}

usize AsyncClient::outstanding() const
{
    std::lock_guard lock(m_impl->mutex);
    return m_impl->inFlight;
}

void AsyncClient::shutdown()
{
    {
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->stopping) {
            return;
        }
        m_impl->stopping = true;
        m_impl->queue.clear();
        m_impl->done.clear();
        m_impl->completed.store(0, std::memory_order_release);
        m_impl->inFlight = 0;
    }
    m_impl->wake.notify_all();

    // A worker already inside `performHttp` finishes that one call before it
    // sees the flag. That is a bounded wait -- every request carries a whole-
    // request timeout (`http.h`) -- and it is why the timeout is a hard number
    // rather than a suggestion: it is also the longest a world can take to shut
    // down.
    for (std::thread& worker : m_impl->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_impl->workers.clear();
}

} // namespace luaug::net
