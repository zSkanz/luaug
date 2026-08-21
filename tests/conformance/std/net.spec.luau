--!strict
--- Conformance: `@std/net` (docs/api-design.md §7, ADR 0012, ADR 0030).
---
--- Two things are pinned here and they are different in kind.
---
--- The SHAPE: that the module resolves, that it exports what
--- `runtime/std/net/init.luau` declares, and that it exports nothing else. That
--- last one matters because the declaration file and the C++ that implements it
--- are two files, and this suite is the only thing that makes them agree.
---
--- The CONTRACT: that a request yields rather than blocking, that a failure is a
--- returned value rather than a raise, and that a malformed CALL is the other
--- way round.
---
--- **No case here touches a socket, and that is a constraint rather than a
--- preference.** A conformance run carries a hundred-thousand-FRAME budget
--- (`main.cpp`), and headless frames are decoupled from the wall clock by
--- design — a hundred thousand of them on the null backend is under a second of
--- real time. A case that waited on a real connection would spend seconds of
--- wall clock inside a budget denominated in ticks, and the suite would be
--- killed mid-run. The first version of this file did exactly that.
---
--- So every request below fails while the URL is being PARSED, before a socket
--- exists. The park-and-resume path is identical either way — the worker still
--- takes the job, still files a result, and the host still resumes the coroutine
--- at a frame safe point — so what is lost is only the socket, and the socket is
--- what `luaug_net_tests` covers with a loopback server of its own.

local net = require("@std/net")
local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

-- Refused while the URL is being read: this build vendors no TLS library, and
-- refusing beats downgrading. Used throughout as "a request that fails without a
-- network", which is the only kind this suite can afford.
local UnreachableUrl = "https://example.test/"

describe("@std/net", function()
    it("resolves and is the same table every time", function()
        expect(net):ToBeTruthy()
        expect(require("@std/net")):ToBe(net)
    end)

    it("exports request", function()
        expect(typeof(net.request)):ToBe("function")
    end)

    it("does not export serve, which is reserved rather than present", function()
        -- api-design.md §7 lists `serve`; v1 does not ship it, and the decision
        -- is recorded at the top of `net_module.cpp`. Asserted here so that a
        -- future `serve` arrives by a deliberate change to this line rather than
        -- by accident.
        expect((net :: any).serve):ToBe(nil)
        expect((net :: any).socket):ToBe(nil)
    end)

    it("is frozen, so a script cannot install a serve of its own", function()
        expect(function()
            (net :: any).serve = function() end
        end):ToThrow()
    end)

    describe("request", function()
        it("yields rather than blocking the VM", function()
            -- The property that makes this usable at all. If it blocked, the
            -- frame would stop for as long as the server took.
            --
            -- It yields even for a request that cannot possibly succeed, and
            -- that is deliberate: a function that sometimes yields and
            -- sometimes returns is one every caller has to be written for
            -- twice.
            local returned = false
            task.spawn(function()
                net.request({ url = UnreachableUrl })
                returned = true
            end)

            expect(returned):ToBe(false)

            -- Spun on a WALL-CLOCK deadline rather than `task.wait(0.1)`, and
            -- for the reason in this file's header: a tenth of a second of SIM
            -- time is six ticks, which on the null backend is microseconds of
            -- real time -- less than a worker thread needs to be scheduled at
            -- all. The thing being waited for keeps wall-clock time, so the
            -- wait has to as well.
            local deadline = os.clock() + 5
            while not returned and os.clock() < deadline do
                task.wait()
            end
            expect(returned):ToBe(true)
        end)

        it("reports a failure as a value rather than raising", function()
            -- A request that cannot be made is a fact about the world, not a
            -- bug in the script, and a caller should be able to handle it
            -- without wrapping every call in pcall.
            local response
            expect(function()
                response = net.request({ url = UnreachableUrl })
            end).Never:ToThrow()

            expect(response.ok):ToBe(false)
            expect(response.statusCode):ToBe(0)
            expect(#response.statusMessage > 0):ToBe(true)
            -- A failure still arrives in the documented shape: a caller reading
            -- `body` or `headers` on one gets an empty string and an empty
            -- table, never nil.
            expect(response.body):ToBe("")
            expect(typeof(response.headers)):ToBe("table")
        end)

        it("refuses https rather than downgrading it", function()
            local response = net.request({ url = UnreachableUrl })
            expect(string.find(response.statusMessage, "http_tls_unsupported") ~= nil):ToBe(true)
        end)

        it("refuses a URL carrying a credential", function()
            local response = net.request({ url = "http://user:secret@example.test/" })
            expect(response.ok):ToBe(false)
            expect(string.find(response.statusMessage, "http_userinfo_unsupported") ~= nil):ToBe(true)
        end)

        it("raises for a malformed call, because that is a bug in the script", function()
            expect(function()
                net.request({} :: any)
            end):ToThrow()

            -- A NUMBER url raises rather than being coerced. Lua would happily
            -- turn 8080 into "8080" and send a request to a host called 8080.
            expect(function()
                net.request({ url = 8080 :: any })
            end):ToThrow()

            expect(function()
                net.request({ url = "http://127.0.0.1/", headers = "no" :: any })
            end):ToThrow()

            -- An ARRAY of headers is the wrong shape and would otherwise
            -- produce a request with a header called "1".
            expect(function()
                net.request({ url = "http://127.0.0.1/", headers = { "Accept: text/plain" } :: any })
            end):ToThrow()

            expect(function()
                net.request({ url = "http://127.0.0.1/", timeout = 0 })
            end):ToThrow()

            expect(function()
                net.request({ url = "http://127.0.0.1/", timeout = "soon" :: any })
            end):ToThrow()
        end)

        it("raises before it yields, so a malformed call is caught where it was written", function()
            -- The exception to "always yields", and the reason it is right: a
            -- raise from inside the worker would surface a frame later, with a
            -- traceback pointing at the resume rather than at the call.
            local reached = false
            task.spawn(function()
                pcall(function()
                    net.request({} :: any)
                end)
                reached = true
            end)
            expect(reached):ToBe(true)
        end)
    end)
end)
