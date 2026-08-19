---
title: "Luau C API — Verified Against Vendored Source"
captured: 2026-08-19
covers: "The embedding surface M2+ needs: tagged userdata and atom/namecall dispatch; threads, resumption, sandboxing and errors; require-by-string, .luaurc and module registration; allocator, memory categories, GC pacing, compile options and vectors"
confidence: "Every claim below was read out of third_party/luau at the pinned commit and carries a file:line. No web source was consulted. Where the vendored tree contradicts an earlier report or a design document, the contradiction is named and filed in ../UNCONFIRMED.md."
rule: "FROZEN SNAPSHOT — never edit the body; corrections go in a dated Addendum section at the end."
---

# Luau C API — Verified Against Vendored Source

**Pin: Luau 0.734 @ `3fc82b1071ab387531175869afc4fb528464afa4`**, as recorded in
`third_party/manifest.json`. Read 2026-08-19.

**Method.** This document was produced by reading the vendored sources under
`third_party/luau` — headers *and* implementations — and nothing else. No web
page, no release note, no upstream documentation site was consulted. Where a
header comment and the code it describes disagree, the code is reported and the
comment is flagged. This is deliberate: `MASTER_PROMPT.md` §9 forbids guessing
an API signature, and the point of this file is that future sessions read it
*instead of* re-reading the headers.

**Line numbers are pin-specific.** They are accurate at
`3fc82b1071ab387531175869afc4fb528464afa4` and will drift on the next vendor
bump. Symbol names and semantics are the durable part; treat a line number that
does not match as a signal to re-verify, not as a typo.

---

## 0. Read this first: where the design documents are wrong

The most valuable output of this pass is not the signature tables — it is the
list of places where `docs/architecture.md`, `docs/api-design.md`, an ADR, or
the earlier [`luau-2026.md`](luau-2026.md) report assumes something the
vendored source does not do. Each has a row in
[`UNCONFIRMED.md`](UNCONFIRMED.md); the section here explains it.

| Row | The assumption | What the source does | §  |
|---|---|---|---|
| U-17 | One `TAG_INSTANCE` for all Instances **and** per-class metatables via `lua_newuserdatataggedwithmetatable` | There is exactly one metatable per *tag*, and reassignment is refused outright. The two halves of that sentence cannot both be true. | 1.3 |
| U-18 | `LUA_UTAG_LIMIT = 128` reads as 128 usable tags | ~126 in practice: tag 0 belongs to `lua_newuserdata`, 128/129 are VM-reserved | 1.1 |
| U-19 | `useratom` is "string interning", called "when a string is created" | Lazy, one-shot, and `-1` latches permanently | 1.4 |
| U-20 | Atom dispatch can be wired up whenever | Fast opcodes are chosen at `luau_load` time; deopt is one-way; all registration must finish before any script runs | 1.5 |
| U-21 | A per-class slot numbering behind the atom switch | `cachedslot` is per-*callsite* and shared across tags — the slot must be a pure function of the atom | 1.6 |
| U-22 | The direct-field and embedder-GC/weak-ref APIs are available | Both are behind FastFlags that default to **false** | 1.7, 5 |
| U-23 | `luau-2026.md` §4 describes the dispatch surface | It omits the entire direct-dispatch mechanism and names the *slow* route as the primitive | 1.5 |
| U-24 | `lua_newuserdatataggedwithmetatable` is "~3x faster" | Not verifiable from source; it saves one round trip, one push, one barrier | 1.3 |
| U-25 | The tree is stock 0.734 | It carries `LUA_TCLASS`/`LUA_TOBJECT`/`LUA_TINTEGER`, a `class` library, embedder-GC and direct-field APIs, and an `Inliner/` tree | 5 |
| U-26 | "`interrupt` armed every resumption" | There is no per-thread interrupt — one pointer per VM | 2.4 |
| U-27 | A 1 s hard-kill | Not preemptible inside a long builtin; must ignore GC-phase calls; and the kill is catchable by script `pcall` | 2.4 |
| U-28 | `userthread` maintains the per-thread context | Three holes: main thread, `lua_resetthread`, and no inheritance to children | 2.2 |
| U-29 | R4 "sandbox always" is `luaL_sandbox` | It removes nothing, freezes one level deep, and freezes only the string metatable | 2.5 |
| U-30 | Scripts get `safeenv` fast paths | `getfenv`/`setfenv` exist, permanently clear `safeenv`, and their mere mention kills constant folding module-wide | 2.5, 4.4 |
| U-31 | `lua_debugtrace` gives the watchdog a full traceback | Static 4 KB buffer shared process-wide, middle elided; and the frames die at `lua_resetthread` | 2.6 |
| U-32 | R3: no hardcoded user-facing strings | `LUA_ERRMEM`/`LUA_ERRERR` replace the error object with fixed English literals | 2.6 |
| U-33 | The version pin fixes behaviour | FastFlag values change semantics and are not part of the version | 5 |
| U-34 | Signal handlers can yield | `lua_pcall` makes everything under it non-yieldable | 2.3 |
| U-35 | "a module that errors … the failure is cached" | Failures are **never** cached; the module re-runs every time | 3.4 |
| U-36 | "cyclic-require semantics" | Off by default, needs two FastFlags plus the `export` keyword, and never works for plain modules | 3.4 |
| U-37 | Resolution "identical across engine runtime / analyzer / LSP / tooling" | Runtime and analyzer feed the same parser different options and disagree whenever an alias is redefined along a chain | 3.2 |
| U-38 | ADR 0002: shipping builds drop the compiler | Linking `Luau.Require` drags in `Luau.Config`, which needs `Luau.Compiler` | 3.5 |
| U-39 | `is_require_allowed` gates engine-module access | Registered modules are matched *before* it runs | 3.1 |
| U-40 | `@luaug/…` resolves via `luarequire_registermodule` | Exact whole-string, ASCII-lowercased, eager, one registration per path, and invisible to cache clearing | 3.1 |
| U-41 | There is a `.luaurc` reader to use | `Luau.Require` has zero filesystem knowledge; the reference implementation is CWD-dependent and lives in an executable | 3.3 |
| U-42 | A `.luaurc` with `languageMode` plus aliases | An unrecognised key is a hard error that aborts the whole require; `.config.luau` matches lowercase keys only and is wall-clock-timed | 3.3 |
| U-43 | `@self` cannot be overridden | At default flags a user alias named `self` wins | 3.2 |
| U-44 | `vectorLib` + `vectorCtor` + `vectorType` "give constant folding + fastcalls" | `vectorType` affects neither; it only tags a type annotation | 4.4 |
| U-45 | GC step size derived from `lua_allocationrate` | Rate is bytes/s, step is **KB**; the rate returns `-1` routinely; and it reads the wall clock (R10) | 4.3 |
| U-46 | The heap cap raises a graceful keyed error | Refusing an allocation can only produce `LUA_ERRMEM` with Luau's fixed message | 4.1 |
| U-47 | "a thin counting wrapper over the OS allocator" | Duplicates accounting the VM already keeps, and sees page churn, not object churn | 4.1, 4.2 |
| U-48 | Memory categories 32–255 per Script | 224 concurrent scripts maximum, and the range check is compiled out in release | 4.2 |
| U-49 | Bytecode cache keyed on source + Luau version + options | `LBC_VERSION_TARGET` is not what gets emitted when certain FastFlags are set | 4.6, 5 |
| U-50 | The memory-cap allocator can be installed | There is no `lua_setallocf`; it must be chosen at `lua_newstate`, and the host currently calls `luaL_newstate` | 4.1 |

None of these are fatal to the architecture. Most are "the doc describes shape
A and shape B, only B exists" or "this needs an explicit decision that was never
made". They are recorded so the decision happens once.

---

## 1. Userdata, tags, metatables and the dispatch path

### 1.1 The tag space, and what the VM has already claimed

| Constant | Value | Where |
|---|---|---|
| `LUA_UTAG_LIMIT` | 128 | `third_party/luau/VM/include/luaconf.h:101` |
| `LUA_LUTAG_LIMIT` | 128 | `third_party/luau/VM/include/luaconf.h:106` |
| `UTAG_IDTOR` | 128 | `third_party/luau/VM/src/ludata.h:8` |
| `UTAG_PROXY` | 129 | `third_party/luau/VM/src/ludata.h:11` |
| `UTAG_INTERNAL_LIMIT` | 130 | `third_party/luau/VM/src/ludata.h:14` |
| `LU_TAG_ITERATOR` (light) | 128 | `third_party/luau/VM/src/lobject.h:105` |

Both limits are `#ifndef`-guarded and so look tunable. They are not free
parameters: `LUA_UTAG_LIMIT` sizes `global_State::udatagc[]`, `udatamark[]` and
`udatamt[]` (`VM/src/lstate.h:243-245`), and `LUA_LUTAG_LIMIT` sizes
`lightuserdataname[]` (`lstate.h:251`).

**Host-usable full-userdata tags are 0..127, and the practical budget is ~126:**

- Tag **0** is what the `lua_newuserdata(L, s)` macro passes
  (`VM/include/lua.h:496`). Any third-party library compiled against these
  headers uses it. Assigning tag 0 to a real class makes every plain
  `lua_newuserdata` indistinguishable from that class. Reserve it.
- Tag **128** (`UTAG_IDTOR`) is claimed by `lua_newuserdatadtor`, which stores an
  inline destructor pointer after the payload (`VM/src/lapi.cpp:1593`).
- Tag **129** (`UTAG_PROXY`) is claimed by `newproxy()`
  (`VM/src/lbaselib.cpp:352`). `lua_newuserdatatagged` explicitly permits it
  (`lapi.cpp:1562`); host code must not use it.
- `UTAG_INTERNAL_LIMIT` (130) sizes `udatadirect[]` and `udatadirectfields[]`
  (`lstate.h:239, 254`), which are indexed by the raw `uint8_t Udata::tag` on the
  VM hot path — so those two arrays cover the internal tags too.

Light-userdata tag 128 is used internally by generalized-`for` iterator state
(`LU_TAG_ITERATOR`, `lobject.h:105`, consumed at `lvmexecute.cpp:2638, 2689,
2744, 2765, 2829, 2852`). Host light tags stop at 127 so there is no collision,
but do not assume 128 is free.

**`lua_newuserdatadtor` is mutually exclusive with tags.** It forces
`UTAG_IDTOR`, so an object created that way can never have a per-tag metatable,
direct access, or direct field get. Use `lua_setuserdatadtor(L, tag, fn)`
instead — always.

`Udata` itself (`VM/src/lobject.h:309`) is
`{ CommonHeader; uint8_t tag; int len; LuaTable* metatable; alignas(8) char data[1] }`
— 16 bytes of header before the payload on 64-bit. Note the **per-object**
`metatable` pointer, which is a different thing from the per-tag
`global_State::udatamt[tag]` slot. `sizeudata(len)` (`ludata.h:17`) rounds
payloads over 16 bytes up to a 16-byte multiple so subsequent blocks stay
aligned; `data` is only declared `alignas(8)`, so 16-byte alignment is a
consequence of the size class, not a guarantee. Do not put over-aligned types in
a tagged payload without checking.

### 1.2 Creating, checking and converting

| Signature | Where | Notes |
|---|---|---|
| `void* lua_newuserdatatagged(lua_State* L, size_t sz, int tag)` | `VM/include/lua.h:210` (impl `lapi.cpp:1560`) | Metatable is NULL; you must `lua_setmetatable` after. Runs `luaC_checkGC` + `luaC_threadbarrier` first, so the stack may move. |
| `void* lua_newuserdatataggedwithmetatable(lua_State* L, size_t sz, int tag)` | `lua.h:211` (impl `lapi.cpp:1572`) | Requires `lua_setuserdatametatable(L, tag)` to have run already (`api_check` at `lapi.cpp:1584`); with asserts off you silently get `metatable = NULL`. |
| `void* lua_newuserdatadtor(lua_State* L, size_t sz, void (*dtor)(void*))` | `lua.h:212` (impl `lapi.cpp:1593`) | Forces `UTAG_IDTOR`. Avoid. |
| `void* lua_touserdatatagged(lua_State* L, int idx, int tag)` | `lua.h:181` (impl `lapi.cpp:1621`) | `ttisuserdata(o) && uvalue(o)->tag == tag ? data : NULL`. One byte compare; no error, no stack change, no metatable consulted. **The type check to use everywhere.** |
| `int lua_userdatatag(lua_State* L, int idx)` | `lua.h:182` (impl `lapi.cpp:1627`) | `-1` for anything that is not a full userdata. |
| `void* lua_touserdata(lua_State* L, int idx)` | `lua.h:180` (impl `lapi.cpp:1610`) | Deliberately conflates full and *light* userdata. A non-NULL result is not proof of a full userdata. |
| `void lua_setuserdatatag(lua_State* L, int idx, int tag)` | `lua.h:361` (impl `lapi.cpp:1821`) | Writes the tag byte **only**. |
| `void* lua_tolightuserdatatagged(lua_State* L, int idx, int tag)` | `lua.h:179` (impl `lapi.cpp:1604`) | NULL on type or tag mismatch — and a legitimately-NULL light pointer is indistinguishable from a mismatch. |
| `void lua_pushlightuserdatatagged(lua_State* L, void* p, int tag)` | `lua.h:209` (impl `lapi.cpp:812`) | Not collectable, no own metatable, no destructor. |
| `int lua_lightuserdatatag(lua_State* L, int idx)` | `lua.h:183` (impl `lapi.cpp:1635`) | Reads `TValue::extra[0]`. |
| `void* luaL_checkudatatagged(lua_State* L, int ud, int tag)` | `VM/include/lualib.h:47` (impl `laux.cpp:154`) | `lua_touserdatatagged`, and on NULL raises with the name from `lua_getuserdataname(L, tag)`. The right argument checker for a tagged binding. |
| `void* luaL_checkudata(lua_State* L, int ud, const char* tname)` | `lualib.h:46` (impl `laux.cpp:136`) | The slow name-based check: `lua_touserdata` + `lua_getmetatable` + registry lookup + `lua_rawequal`, two pushes and pops. Never in a hot path. |

**`lua_setuserdatatag` does not update the metatable** (`lapi.cpp:1821-1827`).
Retagging an object created by `lua_newuserdatataggedwithmetatable` leaves
`u->metatable` pointing at the old tag's table while the tag-keyed dispatch
arrays now select the new tag's callbacks. Prefer creating a fresh userdata over
retagging.

Light userdata identity includes the tag:
`pvalue(t1) == pvalue(t2) && lightuserdatatag(t1) == lightuserdatatag(t2)`
(`VM/src/lobject.cpp:53`, and the same in `lobject.cpp:78` and
`lvmutils.cpp:354`). Two light userdata with the same pointer but different tags
are not equal and hash to distinct table keys.

`lua_rawgetptagged` / `lua_rawsetptagged` (`lua.h:224`, impls `lapi.cpp:889` and
`1046`) give tagged-pointer table keys without materialising a `TValue` on the
stack — the natural shape for a pointer-keyed identity cache.

### 1.3 Per-tag metatables and destructors

| Signature | Where | Notes |
|---|---|---|
| `void lua_setuserdatametatable(lua_State* L, int tag)` | `lua.h:417` (impl `lapi.cpp:1880`) | Pops a table and stores it in `global->udatamt[tag]`. **Reassignment is not supported.** |
| `void lua_getuserdatametatable(lua_State* L, int tag)` | `lua.h:418` (impl `lapi.cpp:1890`) | Always pushes exactly one value: the table, or nil. |
| `const char* lua_getuserdataname(lua_State* L, int tag)` | `lua.h:421` (impl `lapi.cpp:1908`) | Reads `__type` out of the tag's metatable; `"userdata"` if absent. |
| `void lua_setuserdatadtor(lua_State* L, int tag, lua_Destructor dtor)` | `lua.h:365` (impl `lapi.cpp:1829`) | A plain store into `global->udatagc[tag]`. `nullptr` clears. |
| `lua_Destructor lua_getuserdatadtor(lua_State* L, int tag)` | `lua.h:366` (impl `lapi.cpp:1835`) | |
| `void lua_setlightuserdataname(lua_State* L, int tag, const char* name)` | `lua.h:462` (impl `lapi.cpp:1964`) | Interned and `luaS_fix`ed. **Renaming is not supported** and a second call is a silent no-op in release. |

**One metatable per tag, not per class.** `global_State::udatamt` is
`LuaTable* udatamt[LUA_UTAG_LIMIT]` (`lstate.h:245`);
`lua_newuserdatataggedwithmetatable` reads `global->udatamt[tag]`
(`lapi.cpp:1583`); and `lua_setuserdatametatable` guards reassignment with
`api_check(L, !L->global->udatamt[tag])` (`lapi.cpp:1884`). There is no way to
replace or clear a tag's metatable for the life of the `lua_State`.

This is the U-17 contradiction. Two workable shapes exist:

- **One tag per class.** Real per-class dispatch via
  `lua_newuserdatataggedwithmetatable` plus per-tag
  `lua_registeruserdatadirectaccess` — capped at ~126 classes *shared with every
  value type* (CFrame, Color3, UDim2, Rect, Random, Connection, AssetRef, …).
- **One tag, one shared metatable**, resolving the class from the InstanceId
  inside the atom switch. Fully supported, no cap.

`docs/architecture.md:551-554` currently asserts the first sentence of shape (a)
and the rest of shape (b) in the same paragraph. Pick one.

**The metatable is stored by pointer, so later mutation is visible** — the
conformance suite deliberately registers the metatable first and then populates
`__index`/`__namecall` (`tests/Conformance.test.cpp:799-814`). Note that this
does *not* apply to the direct-access snapshot; see §1.6.

`lua_setuserdatametatable` does **not** mark the table readonly for you. Freeze
it yourself with `lua_setreadonly` (the conformance suite does, at
`Conformance.test.cpp:910, 933`) — `luaL_sandbox` never touches tag metatables.

**There is no `__gc` in this VM.** The complete metamethod set is the `TMS` enum
at `VM/src/ltm.h:12`: `TM_INDEX, TM_NEWINDEX, TM_MODE, TM_NAMECALL, TM_CALL,
TM_ITER, TM_LEN, TM_EQ, TM_ADD, TM_SUB, TM_MUL, TM_DIV, TM_IDIV, TM_MOD, TM_POW,
TM_UNM, TM_LT, TM_LE, TM_CONCAT, TM_TYPE, TM_METATABLE`. Per-tag destructors are
the only finalization hook. They run from `luaU_freeudata`
(`VM/src/ludata.cpp:25-32`) during GC sweep, and the upstream comment at
`ludata.cpp:28-29` warns that using `L` there is "highly unsafe" — only truly
read-only calls such as `lua_getthreaddata` are safe. `lua_Destructor`'s second
argument is the **payload** pointer, not the `Udata*` (`lua.h:363`).

`luaL_newmetatable` (`lualib.h:45`, impl `laux.cpp:124`) is the classic
registry-keyed helper and is completely orthogonal to the tag system — nothing
about it makes `lua_newuserdatataggedwithmetatable` work. Pair it with
`lua_pushvalue` + `lua_setuserdatametatable` to get both
(`Conformance.test.cpp:3690-3696`).

On the "~3x faster" claim (U-24): what `lua_newuserdatataggedwithmetatable`
actually saves versus `newuserdatatagged` + `lua_setmetatable` is one API round
trip, one stack push/pop, and one `luaC_objbarrier` — skipped with the
justification at `lapi.cpp:1580-1581` that new objects are allocated unmarked. No
benchmark in `third_party/luau/bench` measures it. Measure it against the
engine's own allocation pattern before quoting a multiplier.

### 1.4 Atoms: how `useratom` really works

```c
int16_t (*useratom)(lua_State* L, const char* s, size_t l);
```
`VM/include/lua.h:607`. The header comment says "gets called when a string is
created to assign an atom id". **That is not the mechanism.** The real one is
`luaS_updateatom` (`VM/src/lstring.h:21`):

```c
#define luaS_updateatom(L, ts) \
    { if (ts->atom == ATOM_UNDEF) \
        ts->atom = L->global->cb.useratom ? L->global->cb.useratom(L, ts->data, ts->len) : -1; }
```

Every `TString` is born with `atom = ATOM_UNDEF` (`-32768`, `lstring.h:12`; set
in `lstring.cpp:79, 103, 137`). The callback fires **lazily, the first time the
atom is queried**, and the answer — *including the `-1` "not interesting"
answer* — is cached on the `TString` permanently. `TString::atom` is `int16_t`
(`lobject.h:293`), so the usable atom range is 0..32767.

There are exactly four call sites: `lapi.cpp:524` (`lua_tostringatom`),
`lapi.cpp:546` (`lua_tolstringatom`), `lapi.cpp:560` (`lua_namecallatom`), and
`lvmload.cpp:666` (bytecode load).

**Consequence (U-19):** any name string interned and queried before `useratom` is
installed is latched to `-1` for the life of the VM and can never enter the fast
path. Install `useratom` on `lua_callbacks(L)` immediately after creating the
state — before `luaL_openlibs`, before any `luau_load`.

`lua_callbacks(L)` returns `&L->global->cb` (`lapi.cpp:2030`), i.e. **per-VM, not
per-thread**; every coroutine shares it. `lua.h:597-598` states that all
callbacks except `interrupt` may only be changed while the VM is not running
code. `cb.userdata` (`lua.h:601`) is an arbitrary host pointer Luau never
touches — the natural home for the engine's atom table.

| Signature | Where | Notes |
|---|---|---|
| `const char* lua_tostringatom(lua_State* L, int idx, int* atom)` | `lua.h:173` (impl `lapi.cpp:516`) | Returns NULL and leaves `*atom` untouched for a non-string. **No number→string coercion**, so it never allocates and never invalidates the stack. Use this in a generic C `__index`. |
| `const char* lua_tolstringatom(lua_State* L, int idx, size_t* len, int* atom)` | `lua.h:174` (impl `lapi.cpp:530`) | As above plus length; `*len = 0` on a non-string. |
| `const char* lua_namecallatom(lua_State* L, int* atom)` | `lua.h:175` (impl `lapi.cpp:553`) | Reads `L->namecall`. |

`L->namecall` (`lstate.h:301`) is per-thread and **is only ever cleared at thread
creation** (`lstate.cpp:99`). The VM sets it (`lvmexecute.cpp:989, 3615`) and
nothing resets it, so outside a `__namecall` invocation `lua_namecallatom`
returns a stale method name. Treat it as valid only for the duration of the
metamethod.

A free UX win: `laux.cpp:42-43` special-cases a C closure whose debug name is
literally `"__namecall"` so that argument-error messages report the *method*
name. Name the closure that.

### 1.5 The dispatch paths, cheapest to most general

Four routes exist. `luau-2026.md:272-279` describes only the slowest of them
(U-23), which is why this section exists.

**(1) `LOP_NAMECALLUDATA` / `LOP_GETUDATAKS` / `LOP_SETUDATAKS` — the atom fast
path.** Declared at `Common/include/Luau/Bytecode.h:436` with the spec in the
comment at 432-435: equivalent to their `GETTABLEKS`/`SETTABLEKS`/`NAMECALL`
counterparts but tailored to userdata, using only the lower 2 bytes of AUX for
the constant index and the upper bytes as a runtime cache
(`LUAU_INSN_AUX_KV16` / `LUAU_INSN_AUX_SLOT`, `Bytecode.h:505-506`).

**The compiler never emits these — the loader synthesizes them**, once per
`Proto`, at `luau_load` time (`VM/src/lvmload.cpp:637-674`):

```c
for (Instruction* instruction = p->code; ...) {
    /* GETTABLEKS->GETUDATAKS, SETTABLEKS->SETUDATAKS, NAMECALL->NAMECALLUDATA */
    if (instruction[1] < 0x10000) {
        luaS_updateatom(L, s);
        if (s->atom >= 0) *instruction = (*instruction & 0xffffff00) | targetOp;
    }
}
```

Two hard preconditions: the constant index must be `< 0x10000` (line 661,
because the upper 16 AUX bits become the inline cache), and `useratom` must
return `>= 0` for that name.

**Consequence (U-20):** chunks loaded before the atom table knows a name never
get the fast opcodes, and adding a dispatchable property name at runtime does not
retro-fit already-loaded bytecode. Under hot reload the atom table must be
complete and stable before the first load of a module.

Execution: `LOP_GETUDATAKS` at `lvmexecute.cpp:3434`, `LOP_SETUDATAKS` at
`:3508`, `LOP_NAMECALLUDATA` at `:3577`. The get/set forms push
`tm`/`self`/`key`(`/value`) past `top`, call `luau_setupcci` — a stripped
`CallInfo`, no `luau_precall`, no closure dispatch — invoke the C callback
directly, then tear the frame down inline. They bump `L->nCcalls` and honour
`LUAI_MAXCCALLS` (`:3464-3467`).

`LOP_NAMECALLUDATA` is the fastest method call available: it swallows the
following `LOP_CALL`/`LOP_CALLFB` itself (asserted at `:3602`), sets up `self` at
`ra+1` with args already in place, and calls the host callback with **no Luau
frame push at all**. The comment at `:3618` says "namecalls do not increase C
call number and allow yielding" — a negative return means the callback yielded
and the interpreter returns immediately. Result copying honours the CALL
instruction's `nresults` including `LUA_MULTRET`.

**Deoptimisation is permanent and one-way.** On a miss the VM rewrites the
instruction in the loaded `Proto` back to the generic opcode
(`lvmexecute.cpp:3501, 3570, 3661`) and never re-upgrades it. Registering direct
access after a chunk has already executed a callsite leaves that callsite slow
forever. **All `lua_setuserdatametatable` + `lua_registeruserdatadirectaccess`
work must complete at VM boot, before any user script runs.**

Note also `VM_PATCH_AUX_SLOT` (`lvmexecute.cpp:96`) mutates the loaded `Proto`'s
code in place — bytecode is not shareable across concurrently-running
`lua_State`s, which Luau already forbids.

**(2) The direct-field-get path inside plain `LOP_GETTABLEKS`**
(`lvmexecute.cpp:565`) — see §1.7. It sits *ahead* of the `__index`-C-function
path and reuses the instruction's 8-bit `INSN_C` cache against the per-tag
dispatch table. Because the loader converts an atomised `GETTABLEKS` into
`GETUDATAKS`, a callsite only reaches this code after `GETUDATAKS` has
deoptimised once; the two mechanisms compose but cost one deopt round trip.

**(3) The C-`__index` fast path** (`lvmexecute.cpp:601`, mirrored for
`__newindex` at `:739`). Three conditions: full userdata, its **per-object**
metatable has `__index`, and that `__index` is a **C** function
(`clvalue(fn)->isC`). A Luau-function or table `__index` both fall to
`luaV_gettable`. It calls `luaV_callTM`, which does allocate a `CallInfo` and
bump `nCcalls` (`lvmutils.cpp:668-688`) — cheaper than a general call, not free.
`L->cachedslot` (`lstate.h:295`) is handed to the metamethod as scratch and
written back into `INSN_C` after the call (`:613-616, 752-755`), which is what
lets a C `__index` maintain a per-callsite inline cache. It is not exposed
through `lua.h`.

**(4) `LOP_NAMECALL`** (`lvmexecute.cpp:928`). For a full userdata the branch at
`:977-1050` runs, in priority order:

1. `__namecall` on the metatable — `self` and the metamethod are placed in
   registers, `L->namecall` is set, and control falls straight through into
   `LOP_CALL` (comment at `:1058`; under `FFlag::LuauCallFeedback` it `VM_NEXT`s
   to the following CALL instead). **No table lookup at all** — the cheapest
   general method dispatch, and exactly what an atom switch wants.
2. `__index` that is a *table* — an inline-cached probe at `INSN_C & nodemask8`,
   backpatched on miss. Good if you want real closures per method.
3. Otherwise full `luaV_gettable`; a nil result raises `luaG_methoderror`.

`fasttm`/`gfasttm`/`fastnotm` (`ltm.h:43`) implement an 8-bit *negative* cache:
bit `e` set means "metamethod `e` is known absent", set by `luaT_gettm` on a nil
result (`ltm.cpp:97-108`). Only events `<= TM_EQ` (0..7) fit, which covers
`TM_INDEX`=0, `TM_NEWINDEX`=1 and `TM_NAMECALL`=3 (static_assert at
`ltm.cpp:76`). The cache is invalidated wholesale by `invalidateTMcache`
(`ltable.h:15`) from `luaH_set`/`luaH_setstr` (`ltable.cpp:774, 812`), so adding
`__index` after a miss is correctly observed. Practical consequence: a metatable
that *has* these metamethods pays a `luaH_getstr` per dispatch through `fasttm`;
one that lacks them pays a single bit test.

The slow path is `luaV_gettable` (`lvmutils.cpp:101`), `MAXTAGLOOP = 100`
`__index` hops (line 18). For a userdata it goes straight to
`luaT_gettmbyobj(L, t, TM_INDEX)` (line 167) — a full `luaH_getstr`, no tmcache
— and raises `luaG_indexerror` on nil. `luaV_settable` is the mirror at line 179.

### 1.6 The direct-access API

```c
typedef void (*lua_UserdataDirectAccess)(lua_State* L, void* data, int atom, uint16_t* cachedslot, int utag);
typedef int  (*lua_UserdataDirectNamecall)(lua_State* L, void* data, int atom, uint16_t* cachedslot, int utag);
int lua_registeruserdatadirectaccess(lua_State* L, int tag,
        lua_UserdataDirectAccess get, lua_UserdataDirectAccess set,
        lua_UserdataDirectNamecall namecall);
```
`VM/include/lua.h:427` and `:430` (impl `lapi.cpp:1924`). The header marks this
**"experimental API and is subject to breaking changes"** at `lua.h:423`. `data`
is the payload pointer, already unwrapped. Not gated by any FastFlag.

The header's own warning at `lua.h:425-426` is the load-bearing one:

> `cachedslot` is initially 0 and can be set to a custom value… IMPORTANT:
> `cachedslot` values are shared between all userdata, callbacks function of one
> userdata tag has to correctly handle values set by another.

**The cache lives in the bytecode instruction, so it is per-callsite**, and every
tag flowing through that callsite sees the same value. This is safe **only if the
slot is a pure function of the atom** — one global name→slot enum shared by all
classes, never a per-class slot index. A per-class numbering mis-dispatches at
any polymorphic callsite. The conformance suite's reference implementation uses
exactly one global `DirectSlot` enum across both its tags
(`tests/Conformance.test.cpp:946-1001`). That is U-21.

Registration rules, from `lapi.cpp:1924-1956`:

1. It returns **0 immediately** if `global->udatamt[tag]` is NULL — the tag's
   metatable must already be registered.
2. It **snapshots the `TValue`** of each of `__index`/`__newindex`/`__namecall`
   into `global->udatadirect[tag]`, and only wires up callbacks for metamethods
   that already exist (`fasttm` checks at `:1940/1946/1952`). So the metatable
   must already be *fully populated*, and later mutation of the metatable does
   not update the snapshot — the metatable and the direct path can silently
   disagree. Freeze the metatable with `lua_setreadonly` right after registering,
   as the conformance suite does.
3. The snapshotted value must be a function — `luau_setupcci` asserts
   `ttisfunction(ci->func)`.
4. Returns 1 on success.

`lua_UdataDirectAccessData` (`lstate.h:175`) holds the three `TValue`s plus the
three function pointers, instantiated as
`global_State::udatadirect[UTAG_INTERNAL_LIMIT]` (`lstate.h:239`) and nil'd at
state init (`lstate.cpp:256-263`). The `TValue`s are GC roots marked in
`markroot` (`lgc.cpp:902-916`) and — **only when `DFFlag::LuauGcMarkUdataAccess`
is on**, and it defaults to false (`lgc.cpp:22`) — re-marked in the atomic phase
(`lgc.cpp:1011-1012`). Registering direct access while a GC cycle is in flight
can therefore leave a freshly-stored metamethod unmarked. One more reason to do
everything at boot.

### 1.7 Direct field get (behind a flag)

A second, newer and cheaper mechanism: per-field handlers with no `lua_State`,
no atom, no call frame, and no ability to error or allocate.

```c
typedef void (*lua_UserdataDirectFieldGet)(void* ud, void* result);
void lua_registeruserdatadirectfieldget(lua_State* L, int tag, const char* field,
                                        lua_UserdataDirectFieldGet fn);
void lua_userdatadirectfield_setnumber(void* result, double n);
void lua_userdatadirectfield_setvector(void* result, LUA_VECTOR_TYPE x, LUA_VECTOR_TYPE y, LUA_VECTOR_TYPE z);
void lua_userdatadirectfield_setboolean(void* result, int b);
void lua_userdatadirectfield_setinteger64(void* result, int64_t n);
void lua_userdatadirectfield_setnil(void* result);
```
`lua.h:448` and `:452`, documented at `lua.h:438-447`; impls `lapi.cpp:2060` and
`2081-2139`.

Registration lazily creates `global->udatadirectfields[tag] = luaH_new(L, 0, 1)`,
interns the field name and `luaS_fix()`es it (never collected), and stores the
function pointer as a light userdata value. It requires **no metatable and no
atom**. A field absent from the dispatch table falls through to normal `__index`
handling (`tests/DirectFieldAccess.test.cpp:170-224`).

That five-entry list **is the complete result vocabulary**: number, vector,
boolean, int64, nil. No strings, no tables, no nested userdata. A handler
returning a `Vector3` or a scalar property qualifies; one returning a `CFrame`
userdata does not. The vector overload's arity follows `LUA_VECTOR_SIZE` (3 at
our default).

**All of it is behind `FFlag::LuauDirectFieldGet`, which defaults to false**
(`LUAU_FASTFLAGVARIABLE` at `lvmexecute.cpp:21`; the macro expands to
`FValue<bool> flag(#flag, false, false)`, `Common/include/Luau/Common.h:139-143`).
The registration function's first statement is
`if (!FFlag::LuauDirectFieldGet) return;` (`lapi.cpp:2062-2063`) — a **silent
no-op**, no error, no assert. The five setters `LUAU_ASSERT` instead. See §5.

### 1.8 Readonly, safeenv, `__type`, and what light userdata cannot do

| Signature | Where | Notes |
|---|---|---|
| `void lua_setreadonly(lua_State* L, int idx, int enabled)` | `lua.h:227` (impl `lapi.cpp:910`) | Tables only, and `api_check`ed against the registry table. Cheap, and reversible from C at any time — "freezing" is permanent only for scripts. |
| `int lua_getreadonly(lua_State* L, int idx)` | `lua.h:228` (impl `lapi.cpp:919`) | Backs `table.isfrozen` (`ltablib.cpp:651-657`). |
| `void lua_setsafeenv(lua_State* L, int idx, int enabled)` | `lua.h:229` (impl `lapi.cpp:928`) | Sets `LuaTable::safeenv`, which enables the import/builtin fast paths. |
| `int lua_setmetatable(lua_State* L, int objindex)` | `lua.h:243` (impl `lapi.cpp:1058`) | For userdata writes `uvalue(obj)->metatable` + barrier and does **not** check readonly (only the table branch raises, line 1073). For any other type it writes `global->mt[ttype(obj)]`. Always returns 1. |
| `int lua_getmetatable(lua_State* L, int objindex)` | `lua.h:231` (impl `lapi.cpp:936`) | Userdata → the **per-object** pointer, not `udatamt[tag]`. Pushes nothing and returns 0 when absent. Does not honour `__metatable` protection. |

Readonly is enforced at `luaV_settable` (`lvmutils.cpp:195`), `LOP_SETTABLEKS`
(`lvmexecute.cpp:709, 715`), `lua_setfield`/`rawset`/`rawseti`/`rawsetfield`/
`rawsetptagged` (`lapi.cpp:1015, 1027, 1039, 1051`), `lua_setmetatable` on a
table (`:1073`), `lua_cleartable` (`:2011`), and the table library
(`ltablib.cpp:121, 289, 555, 632`). The message is fixed English:
`"attempt to modify a readonly table"` (`luaG_readonlyerror`, `ldebug.cpp:342`).

**`luaH_clone` deliberately drops readonly and safeenv** (`ltable.cpp:910`, inside
`luaH_clone` at line 900) while copying the metatable. `table.clone` of a frozen
engine table yields a writable one pointing at the same metatable. Relevant if
scripts may clone engine-owned tables.

`typeof()` resolves through `luaT_objtypenamestr` (`ltm.cpp:141`): a full
userdata reports its metatable's `__type` string, **unless** the tag is
`UTAG_PROXY`, which is deliberately excluded (line 145). Tagged light userdata
falls back to `lua_setlightuserdataname`. `luaB_type` (`lbaselib.cpp:196`)
deliberately does not differentiate; `luaB_typeof` (`:204`) does.

**Light userdata cannot participate in per-class dispatch at all.** There is
exactly one metatable for the whole `LUA_TLIGHTUSERDATA` type —
`global->mt[ttype]` (`lapi.cpp:954`, `ltm.cpp:136`, and `luaT_gettmbyobj` at
`ltm.cpp:110` routes everything that is not userdata/table/class there). Tags
only affect identity, table-key semantics, and the type name. There is no per-tag
`__index` or `__namecall` for light userdata.

### 1.9 The boot order that all of this implies

1. `lua_newstate` (see §4.1 — not `luaL_newstate`, if you want a custom
   allocator).
2. Install `useratom` on `lua_callbacks(L)` **immediately**, before anything
   interns a name (§1.4).
3. `luaL_openlibs`, then engine globals and libraries.
4. Per tag: build the metatable → `lua_setuserdatametatable(L, tag)` → populate
   `__index`/`__newindex`/`__namecall` → `lua_registeruserdatadirectaccess` →
   `lua_setreadonly` the metatable (§1.3, §1.6).
5. `lua_setuserdatadtor` per tag as needed.
6. `luaopen_require` if used — it must precede sandboxing (§3.1).
7. `luaL_sandbox(L)` **last** (§2.5).
8. Per script thread: `lua_newthread` → `luaL_sandboxthread(child)` →
   `lua_xpush` the function → `luau_load` → `lua_resume`.

Nothing in steps 4–5 may be deferred until after a script has run.

---

## 2. Threads, resumption, sandboxing and the error pipeline

### 2.1 Status enums

```c
enum lua_Status   { LUA_OK = 0, LUA_YIELD, LUA_ERRRUN, LUA_ERRSYNTAX, LUA_ERRMEM, LUA_ERRERR, LUA_BREAK };
enum lua_CoStatus { LUA_CORUN = 0, LUA_COSUS, LUA_CONOR, LUA_COFIN, LUA_COERR };
```
`VM/include/lua.h:26` and `:37`. `LUA_ERRSYNTAX` is annotated "legacy error code,
preserved for compatibility"; `LUA_BREAK` = 6 is "yielded for a debug
breakpoint". `lua_State::status` is a `uint8_t` (`lstate.h:272`).

One more value exists that is not in the enum: `SCHEDULED_REENTRY = 0x7f`
(`VM/src/ldo.h:61`), documented there as internal. It **is** observable in
`lua_State::status` while the VM unwinds a scheduled reentry — never test
`status != LUA_OK` from a callback and assume yield-or-break.

`coroutine.status` maps the `lua_CoStatus` values to
`{"running","suspended","normal","dead","dead"}` (`VM/src/lcorolib.cpp:12`):
`LUA_COFIN` and `LUA_COERR` are both "dead".

### 2.2 Thread lifecycle

| Signature | Where | Notes |
|---|---|---|
| `lua_State* lua_newthread(lua_State* L)` | `lua.h:128` (impl `lapi.cpp:232`) | Order: `luaC_checkGC` → `luaC_threadbarrier` → `luaE_newthread` → push onto `L` → `cb.userthread(L, L1)`. |
| `lua_State* lua_mainthread(lua_State* L)` | `lua.h:129` (impl `lapi.cpp:246`) | One line. Never fails, never allocates, safe from any callback. |
| `void lua_resetthread(lua_State* L)` | `lua.h:130` (impl `lstate.cpp:155`) | Closes upvalues, collapses CI to `base_ci`, shrinks stack and CI array, `status = LUA_OK`, nils every slot. |
| `int lua_isthreadreset(lua_State* L)` | `lua.h:131` (impl `lstate.cpp:185`) | `ci == base_ci && base == top && status == LUA_OK` — "empty and idle", not "was reset". |
| `void lua_xmove(lua_State* from, lua_State* to, int n)` | `lua.h:146` (impl `lapi.cpp:202`) | Pops `n` off `from`, pushes on `to`. No-op if `from == to`. Same VM only. |
| `void lua_xpush(lua_State* from, lua_State* to, int idx)` | `lua.h:147` (impl `lapi.cpp:223`) | Copies one value without popping. The canonical way to seed a coroutine. |
| `int lua_pushthread(lua_State* L)` | `lua.h:207` (impl `lapi.cpp:820`) | Pushes `L`; returns non-zero iff it is the main thread. |
| `lua_State* lua_tothread(lua_State* L, int idx)` | `lua.h:184` (impl `lapi.cpp:643`) | NULL for a non-thread; never raises. |
| `void lua_setthreaddata(lua_State* L, void* data)` / `void* lua_getthreaddata(lua_State* L)` | `lua.h:268`, `:267` (impls `lapi.cpp:1315`, `1310`) | One `void*` per `lua_State` (`lstate.h:303`). |

`lua_newthread` **can run a GC step**, so anything unrooted on the C side may die
during the call. The returned `lua_State*` is rooted **only** by the stack slot
it just pushed on `L` — pop it without `lua_ref` and the coroutine is
collectable. `luaE_newthread` (`lstate.cpp:132`) shares the parent's globals
table verbatim (`L1->gt = L->gt`), inherits `activememcat` and `singlestep`, and
`preinit_state` (`lstate.cpp:88-105`) sets `L1->userdata = NULL` — **thread data
is not inherited**.

`lua_resetthread` preconditions are `api_check` only (so compiled out in
release): `!L->isactive`, and `(status != LUA_OK || ci == base_ci)` — a dead or
errored thread, or an OK thread with no frames, never a live one. It does **not**
touch `L->gt` (a sandboxed thread stays sandboxed, which is what makes pooling
work), does **not** clear `L->userdata`, does not touch `singlestep`, and **does
not invoke the `userthread` callback**. It destroys the error object and the
entire call stack (`lstate.cpp:161-183`) — capture any traceback first.

`lua_xmove`/`lua_xpush` grow the destination via `ensure_stack_impl(to, from, n)`
(`lapi.cpp:57, 212`), which checks `to`'s stack but **raises the overflow error on
`from`** — the unwind hits the source thread, not the destination.

**`lua_Callbacks::userthread`** (`lua.h:606`) has exactly two call sites:

- Creation: `lapi.cpp:241-242`, inside `lua_newthread`, **after** the new thread
  is pushed on the parent's stack, with `(parent, child)`.
- Destruction: `lstate.cpp:148-149`, inside `luaE_freethread`, with
  `(NULL, dying)` **before** `freestack` — so the dying thread's stack is still
  readable, but you are inside GC sweep (`lgc.cpp:762`). `lua.h:383-386` is
  explicit: GC-time callbacks "must not perform any reentrant operations on the
  `lua_State`. Only truly read-only APIs like `lua_getthreaddata` are safe." No
  `lua_unref`, no pushes, no allocation through the Lua allocator.

**Three holes (U-28)** in "the `userthread` callback maintains it":

1. It is **never called for the main thread** — `lua_newstate`
   (`lstate.cpp:190`) does not invoke it, and `close_state`
   (`lstate.cpp:107-130`) frees the main thread without a `userthread(NULL, …)`
   call. Main-thread context must be created and destroyed by hand around
   `lua_newstate`/`lua_close`.
2. `lua_resetthread` does not call it, so a recycled thread keeps stale
   `threaddata` unless the pool clears it.
3. Children do not inherit `userdata` — the creation callback must copy it from
   the parent explicitly.

Coroutines created from Luau (`coroutine.create`/`wrap`) do go through
`lua_newthread`, so they fire it.

`lua_getthreaddata` is the right home for a per-coroutine `ScriptThreadCtx`
precisely because it is the one API the header blesses for GC-callback use.

### 2.3 Resumption

| Signature | Where |
|---|---|
| `int lua_resume(lua_State* L, lua_State* from, int narg)` | `lua.h:263` (impl `ldo.cpp:714`) |
| `int lua_resumeerror(lua_State* L, lua_State* from)` | `lua.h:264` (impl `ldo.cpp:726`) |
| `int lua_yield(lua_State* L, int nresults)` | `lua.h:261` (impl `ldo.cpp:744`) |
| `int lua_break(lua_State* L)` | `lua.h:262` (impl `ldo.cpp:756`) |
| `int lua_status(lua_State* L)` | `lua.h:265` (impl `lapi.cpp:1285`) |
| `int lua_costatus(lua_State* L, lua_State* co)` | `lua.h:269` (impl `lapi.cpp:1290`) |
| `int lua_isyieldable(lua_State* L)` | `lua.h:266` (impl `ldo.cpp:764`) |
| `void lua_callhook(lua_State* L, lua_Hook hook, void* userdata)` | `lua.h:542` (impl `ldebug.cpp:193`) |

**Protocol.** `L` is the coroutine; `from` is the resuming thread or NULL, used
*only* for C-stack accounting — `resume_start` does
`L->nCcalls = from ? from->nCcalls : 0` (`ldo.cpp:647`). There is no parent link
in this VM. For a first resume the function must already be on `L`'s **own**
stack (via `lua_xpush`) with `narg` args after it; for a resume-after-yield only
the `narg` results go on `L`'s stack. Resuming from a top-level scheduler with
`from = NULL` resets `nCcalls` to 0 and is the cheapest option.

Returns: `LUA_OK` (results on `L`'s stack, count = `lua_gettop(L)`), `LUA_YIELD`
(yielded values likewise, because `lua_yield` sets `base = top - nresults`),
`LUA_BREAK`, or an error code with the error object at `L`'s top.

**On error, `lua_resume` does not unwind `L->ci`.** `resume_finish`
(`ldo.cpp:700-705`) only writes status, the error object and `ci->top`, so the
call frames survive and a traceback is available until `lua_resetthread` destroys
them. That ordering constraint is U-31: harvest the traceback, then recycle.

**Two early failures behave differently from a real error** (`resume_start`,
`ldo.cpp:639-657`):

- status is neither `LUA_YIELD` nor `LUA_BREAK` and not `(status == 0 && ci ==
  base_ci)` → pops `narg`, pushes `"cannot resume non-suspended coroutine"`,
  returns `LUA_ERRRUN`;
- `nCcalls >= LUAI_MAXCCALLS` (200) → pushes `"C stack overflow"`, returns
  `LUA_ERRRUN`.

In both cases **`L->status` is not modified** and `isactive` is never set. A
drain loop that reads `lua_status`/`lua_costatus` after a non-zero resume result
to decide whether to recycle the thread mis-handles exactly these two: the
coroutine is untouched and still resumable. Distinguish by checking status
*before* resuming.

**`lua_costatus` decision order** (`lapi.cpp:1290`): `co == L` → `LUA_CORUN`;
`status == LUA_YIELD` → `LUA_COSUS`; `status == LUA_BREAK` → `LUA_CONOR`; any
error status → `LUA_COERR`; `ci != base_ci` → `LUA_CONOR`; `top == base` →
`LUA_COFIN`; else `LUA_COSUS`.

**Trap:** a freshly created `lua_newthread` with nothing pushed has `top ==
base`, so it reports **`LUA_COFIN` (dead)**, and `lua_resume` on it raises
"cannot resume dead coroutine" (`ldo.cpp:453-454`). Scheduler bookkeeping must
never observe a thread between `lua_newthread` and `lua_xpush`. Note also that
`LUA_BREAK` maps to `LUA_CONOR` ("normal"), not suspended.

**`lua_yield` must be tail-returned** from a C function: `return lua_yield(L,
n);`. It returns `-1`, and `luau_precall` treats any negative C return as
`PCRYIELD`. Precondition: if `L->nCcalls > L->baseCcalls` it raises
`"attempt to yield across metamethod/C-call boundary"` — i.e. only when
`lua_isyieldable(L)` (`ldo.cpp:764`: `nCcalls <= baseCcalls`).

**`lua_break`** is the same shape but sets `LUA_BREAK` and does not reposition
`base`. A break propagates outward: `coroutine.resume`, seeing `LUA_BREAK` from
an inner coroutine, calls `interruptThread` (`lcorolib.cpp:74-81`), which fires
`cb.debuginterrupt` and breaks the *calling* thread too. **A deferred-signal
drain must treat `LUA_BREAK` as a third outcome** — neither error nor yield — and
re-resume later.

**`lua_resumeerror`** resumes `L` by *injecting an error* rather than values: the
error object must be the single value on `L`'s stack. It does not run the body —
it finds the nearest `CallInfo` carrying `LUA_CALLINFO_HANDLE` (a pcall/xpcall
continuation frame, `resume_findhandler` at `ldo.cpp:509`) and runs
`resume_handle`; with no protected frame the coroutine dies with that error. This
is the mechanism for cancelling a parked coroutine from the scheduler:
`lua_xmove(L, co, 1); lua_resumeerror(co, L);`
(`tests/Conformance.test.cpp:456, 1483`).

**`lua_pcall` is not Luau's `pcall` (U-34).**

| | `lua_pcall` (`lua.h:251`, impl `lapi.cpp:1167` / `ldo.cpp:769`) | `lua_pcallyieldable` (`lua.h:256`, impl `lapi.cpp:1243`) |
|---|---|---|
| Yieldable under it | **No** — runs with `nCcalls > baseCcalls` | Yes |
| Precondition | `api_check(L->status == 0)` — not on a parked coroutine | Current C function must have been pushed with a continuation (`cl->c.cont`) |
| Frame marking | — | `LUA_CALLINFO_HANDLE`, `errfunc` in `ci->errfunc` |
| Used by | host code | Luau's own `pcall`/`xpcall` (`lbaselib.cpp:281-336`, registered at `:408`, `:411`) |

Anything the engine calls into script that must be allowed to yield — signal
handlers, `BindToClose`, script entry points — has to run on its own coroutine
via `lua_resume`, or through `lua_pcallyieldable` from a C function registered
with `lua_pushcclosurek`. A `task.wait()` inside an engine callback invoked
through `lua_pcall` raises the boundary error.

`lua_callhook` (`ldebug.cpp:193` → `luau_callhook`, `lvmexecute.cpp:166`) is
designed to inspect a **paused** thread: if status is `LUA_YIELD` or `LUA_BREAK`
it temporarily forces `status = 0` and `base = ci->base` so the hook can make
Luau calls, then restores. `userdata` arrives via `lua_Debug::userdata`
(`lua.h:575`, "only valid in `lua_callhook`"). This is how a scheduler inspects a
parked coroutine without resuming it.

`coroutine.close` (`lcorolib.cpp:219`) is the reference implementation of safe
reuse and the only in-VM caller of `lua_resetthread`: it refuses unless
`lua_costatus` is `COFIN`/`COERR`/`COSUS`, reconstructs the message for
`LUA_ERRMEM`/`LUA_ERRERR` from the fixed literals because the error object is not
on the stack in those cases (`:238-243`), and only **then** resets. Copy that
order.

### 2.4 The interrupt, and what a watchdog can actually do

```c
void (*interrupt)(lua_State* L, int gc);  // gets called at safepoints (loop back edges, call/ret, gc) if set
```
`lua.h:603`. There is **one pointer per VM**, on `global_State::cb`
(`lstate.h:232`); `lua_callbacks(L)` returns `&L->global->cb`
(`lapi.cpp:2030`). **There is no per-thread interrupt (U-26).** A per-resumption
watchdog has to be a single global callback that recovers the per-script budget
via `lua_getthreaddata(L)`. Re-assigning the pointer around each resume is racy
in the way `lua.h:596-598` warns about: `interrupt` is the *only* callback safe
to set from an arbitrary thread; the others may only change while the VM is not
running code.

Three callers, distinguished by `gc`:

1. **Bytecode safepoints, `gc == -1`** — the `VM_INTERRUPT` macro
   (`lvmexecute.cpp:98-110`) is invoked from exactly seven opcodes: `LOP_CALL`
   (`:1065`), `LOP_CALLFB` (`:1165`), `LOP_RETURN` (`:1278`), `LOP_FORNLOOP`
   (`:2556`), `LOP_FORGLOOP` (`:2707`), `LOP_JUMPBACK` (`:3003`), `LOP_JUMPX`
   (`:3026`). The identical macro exists for the native path at
   `CodeGen/src/CodeGenUtils.cpp:52`.
2. **GC, `gc >= 0`** — `GC_INTERRUPT` (`lgc.cpp:126-131`), twice per
   `luaC_step`: `GC_INTERRUPT(0)` at entry (`:1277`) and
   `GC_INTERRUPT(lastgcstate)` at exit (`:1335`), where the value is a `GCS*`
   state 0..4. Note the entry call passes literal 0, so **the discriminator is
   the sign, not the value**.
3. **String pattern matching, `gc == -1`** — `lstrlib.cpp:434-442`, wrapped in
   `L->nCcalls++ … L->nCcalls--` with the comment "this interrupt is not
   yieldable".

**Nothing else fires it (U-27).** A script stuck in one long builtin call —
`string.rep` with a huge count, `table.concat`, a huge `string.format`, buffer
ops — reaches no safepoint and cannot be killed.

Upstream's own reference abort (`CLI/src/Repl.cpp:57-67`) encodes three
requirements:

```c
static void sigintCallback(lua_State* L, int gc) {
    if (gc >= 0) return;                     // (a) mandatory
    lua_callbacks(L)->interrupt = NULL;      // (c) do not re-fire while unwinding
    lua_rawcheckstack(L, 1);                 // (b) no stack slack is guaranteed here
    luaL_error(L, "Execution interrupted");
}
```

(a) is not optional: raising from a GC-phase interrupt throws out of the middle
of `luaC_step`. (b) uses `lua_rawcheckstack` (`lua.h:144`) — the variant that
"allows for unlimited stack frames" and cannot fail-soft — because the interrupt
fires where `lua_checkstack` has no room to negotiate.

**A hard kill raised from the interrupt is catchable by the script.** It unwinds
as an ordinary Luau error, so any enclosing `pcall`/`xpcall` swallows it
(`luaD_pcall`, `ldo.cpp:769`, or the `resume_handle` path at `ldo.cpp:550`).
Clearing the interrupt pointer first does not change that. **There is no
uncatchable-error mechanism in this VM.** The only robust kill is to re-arm the
interrupt and raise again at the next safepoint until the coroutine dies, or to
abandon the thread and `lua_resetthread` it.

The non-destructive alternative is `return lua_break(L)`: `VM_INTERRUPT` then
sees `L->status != 0`, decrements `savedpc` back to the current instruction and
exits the interpreter loop, so `lua_resume` returns `LUA_BREAK` and execution can
resume exactly where it stopped.

### 2.5 Sandboxing — what `luaL_sandbox` does and does not do

`luaL_sandbox` (`lualib.h:161`, impl `linit.cpp:65-92`) does **exactly three
things**:

1. iterates `LUA_GLOBALSINDEX` with `lua_next` and calls
   `lua_setreadonly(true)` on every value that is a table — **one level deep
   only**; nested tables are left writable;
2. pushes a `""` literal and, if it has a metatable, freezes it — despite the
   comment saying "set all builtin metatables to read-only", **the string
   metatable is the only one it touches**;
3. `lua_setreadonly(LUA_GLOBALSINDEX, true)` then
   `lua_setsafeenv(LUA_GLOBALSINDEX, true)`.

**It removes nothing.** No library is deleted, no function is stubbed. Library
curation — deciding which functions exist at all — is entirely the engine's job.
That is U-29: R4 "sandbox always" is not satisfied by calling this function.

It also does **not** touch tag metatables registered via
`lua_setuserdatametatable`; freeze those yourself (§1.3).

On the positive side, the vendored base library already omits `loadstring`,
`dofile`, `require` and `collectgarbage` (`lbaselib.cpp:363-383`), and `os` is
limited to `clock`/`date`/`difftime`/`time` (`loslib.cpp:214-217`) — which
already matches the restriction `docs/api-design.md` assumes.

**`getfenv`/`setfenv` are present** (`lbaselib.cpp:367, 376`) and both call
`lua_setsafeenv(…, false)` on the environment they touch (`lbaselib.cpp:131,
140`). One `getfenv()` call from a script **permanently** de-optimizes that
environment: `safeenv` is what enables eager import resolution at load time
(`lvmload.cpp:200`) and the builtin fast paths (`lvmexecute.cpp:499, 3056, 3171,
3221, 3271, 3322`). `safeenv` is also cleared automatically by
`luaH_new`/`luaH_clone` (`ltable.cpp:578, 911`). If the perf baselines assume
`safeenv` for all script code, these two globals must be removed or the
assumption qualified (U-30). There is a second, compile-time cost to their mere
presence in source — see §4.4.

**Ordering is a hard rule.** `luaL_sandbox` must be the **last** step of VM setup:
afterwards `LUA_GLOBALSINDEX` is readonly and any later global registration
fails. `luaL_sandboxthread` (`linit.cpp:94-109`) creates a new empty table, gives
it a readonly metatable whose `__index` is the current globals, `lua_replace`s
`LUA_GLOBALSINDEX` (which for that pseudo-index just assigns `L->gt`,
`lapi.cpp:315-319`) and sets `safeenv` on it. It must be called **with the child
thread as its argument** — `luaE_newthread` shares `gt` with the parent
(`lstate.cpp:139`), so calling it on the wrong `lua_State*` repoints the parent
instead of isolating the child — and it must run **before** `luau_load` on that
thread, because load-time import resolution reads `L->gt->safeenv`.

Two more notes on `luaL_sandboxthread`: `_G` inside a sandboxed thread still
resolves through `__index` to the **original frozen globals table**, not the
thread's private one (`lbaselib.cpp:395-396` sets `_G` at open time); and
`linit.cpp:106` carries an explicit caveat — "it's important to set it to false
if code is loaded twice into the thread", i.e. call
`lua_setsafeenv(L, LUA_GLOBALSINDEX, false)` before a **second** `luau_load` into
the same thread. That is directly relevant to hot reload, which loads repeatedly
into one script thread.

### 2.6 Errors, tracebacks and references

**Error transport is C++ exceptions by default.** `LUA_USE_LONGJMP` defaults to 0
(`luaconf.h:65-66`), so `luaD_throw` does `throw lua_exception(L, errcode)`
(`ldo.cpp:161-164`) and `luaD_rawrunprotected` (`ldo.cpp:123-159`) catches
`lua_exception` — asserting it came from the same `lua_State` — **and also
catches `std::exception`**, converting an escaping C++ exception from embedder
code into a Luau `LUA_ERRRUN` carrying `e.what()` (`ldo.cpp:142-156`).

Two consequences: engine C++ called from Luau must be exception-safe/RAII,
because a Luau error unwinds through it as a real C++ exception; and any
`std::exception` you let escape becomes a Luau error string rather than a crash.
If `LUA_USE_LONGJMP` were ever set to 1, `luaD_throw` would `longjmp`
(`ldo.cpp:63-75`) and destructors would not run. `lua_Callbacks::panic`
(`lua.h:604`) is reachable **only** in that build (`ldo.cpp:71-74`); in the
default build it is dead code. Do not rely on it for last-resort logging.

**`luaD_seterrorobj` (`ldo.cpp:380-393`) substitutes fixed English literals** for
`LUA_ERRMEM` and `LUA_ERRERR`: `"not enough memory"` and
`"error in error handling"` (strings fixed at VM open, `lstate.cpp:83-84`; macros
at `ldebug.h:13-14`). The original error value is discarded. It always finishes
with `L->top = oldtop + 1`, so after any failing `lua_resume`/`lua_pcall` the
error is at index `-1` — but for those two codes it is never your object.

**This is why R3 cannot be met by inspecting the error value (U-32).** The engine
must map on the *status code*, not the message. The same applies to every stdlib
error string: `"cannot resume dead coroutine"`, `"attempt to yield across
metamethod/C-call boundary"`, `"C stack overflow"`, `"attempt to modify a
readonly table"`.

| Signature | Where | Notes |
|---|---|---|
| `l_noret lua_error(lua_State* L)` | `lua.h:349` (impl `lapi.cpp:1468`) | `api_checknelems(L,1)` then `luaD_throw(L, LUA_ERRRUN)`. Any value type. Add position info with `luaL_where` first if you want it. |
| `l_noret luaL_errorL(lua_State* L, const char* fmt, ...)` | `lualib.h:52` (impl `laux.cpp:100-109`) | Macro `luaL_error` at `lualib.h:7`. Auto-prefixes `"short_src:line: "`. "Can be called without stack space reservation" (`laux.cpp:99`). |
| `void luaL_traceback(lua_State* L, lua_State* L1, const char* msg, int level)` | `lualib.h:64` (impl `laux.cpp:391`) | Reentrant. Builds into a `luaL_Strbuf` on `L`, walks `L1`'s frames, **skips every C frame**. |
| `const char* lua_debugtrace(lua_State* L)` | `lua.h:562` (impl `ldebug.cpp:664-709`) | See below. |
| `int lua_getinfo(lua_State* L, int level, const char* what, lua_Debug* ar)` | `lua.h:545` (impl `ldebug.cpp:206-239`) | `level >= 0` counts from the top of the call stack; `level < 0` addresses a function *value* at `L->top + level`. Returns 0 when the level does not resolve. |

`lua_debugtrace` writes into a function-local `static char buf[4096]`
(`ldebug.cpp:666`). The header's warning at `lua.h:561` is literal, and **the
buffer is shared across every `lua_State` in the process** — which under a "many
light VMs per core" model is every VM. It also elides the middle of deep stacks:
first 10 frames, then `"... (+N frames)"`, then the last 10. It is not the "full
traceback" a watchdog spec asks for. **Use `luaL_traceback(schedulerL, co, msg,
0)` for anything captured, logged or shown** (U-31); that is also how you produce
a traceback of a dead coroutine onto the scheduler thread's stack.

`lua_Callbacks::debugprotectederror` (`lua.h:612`) fires from `luaD_pcall`
(`ldo.cpp:811-818`) and `resume_finish`'s handler loop (`ldo.cpp:664-674`), only
when the thread is yieldable, and **the hook is only allowed to break** — if it
leaves `status == LUA_BREAK` the error is swallowed. It is the hook for "break
into the debugger on caught error", not a place to raise.

**References.** `lua_ref` (`lua.h:479`, impl `lapi.cpp:1749`) reads the value at
`idx` — it does **not** pop. Nil → `LUA_REFNIL` (0) with nothing stored.
Otherwise it takes a slot from `global_State::registryfree` or `luaH_getn(reg) +
1`, and the **free list is threaded through the registry's own integer slots**
(the freed slot holds the next free index as a number, `lapi.cpp:1814`).

> **The engine must never write its own integer keys into `LUA_REGISTRYINDEX`**,
> or `lua_ref` will hand out a slot already in use and `lua_unref` will corrupt
> the free list. Use string or lightuserdata keys (as
> `luaL_newmetatable`/`luaL_getmetatable` do, `laux.cpp:124-134`).

`lua_unref` (`lua.h:480`, impl `lapi.cpp:1789`) **always returns `LUA_NOREF`
(-1)** — the idiom is `myref = lua_unref(L, myref);`. It no-ops for `ref <=
LUA_REFNIL`. Its `api_check` asserts the slot is non-nil, so double-unref is an
assert in debug and silent free-list corruption in release.

**The liveness test is `ref > LUA_REFNIL`, not `ref != LUA_NOREF`** — `lua_ref`
returns 0 for a nil value, and `-1` is only ever produced by `lua_unref`, so the
naive test treats a nil-ref as a live reference. `lua_getref` (`lua.h:476`) is a
macro over `lua_rawgeti` and returns the *type* it pushed.

**Pseudo-indices.** `LUA_REGISTRYINDEX = -(LUAI_MAXCSTACK) - 2000`,
`LUA_ENVIRONINDEX` `-2001`, `LUA_GLOBALSINDEX` `-2002` (`lua.h:19`), and
`LUAI_MAXCSTACK` is 8000 (`luaconf.h:81`) — so the concrete values are
**-10000 / -10001 / -10002**, and `lua_upvalueindex(1)` is -10003. `pseudo2addr`
(`lapi.cpp:88-112`) returns a pointer to a **shared scratch slot**
`global_State::pseudotemp` for `ENVIRONINDEX`/`GLOBALSINDEX` — the address is
valid only until the next pseudo-index access. Never hold it.

### 2.7 GC and stack constraints on holding coroutines across frames

1. **Rooting.** A `lua_State*` is an ordinary GC object; after `lua_newthread`
   its only root is the parent's stack slot. Hold it across frames with
   `lua_ref` or it will be swept, and `luaE_freethread` will fire
   `userthread(NULL, co)` out from under you.
2. **Stack moves.** During `GCSpropagate` the collector runs
   `shrinkstackprotected` on each traversed thread (`lgc.cpp:585-586`), which can
   `luaD_reallocstack`/`luaD_reallocCI` and **move the whole stack**
   (`correctstack`, `ldo.cpp:169`). Every cached `StkId`/`TValue*`/`CallInfo*` is
   invalidated, and so is any `const char*` from `lua_tolstring` once its owner is
   popped. **Cache nothing across a resume, a GC step, or any allocating API.**
3. **Clearing.** `clearstack` (`lgc.cpp:486`) nils every slot above `top` on
   inactive threads. In `propagatemark` (`lgc.cpp:561-589`) a thread is re-greyed
   only when `th->isactive || th == mainthread`, so a parked coroutine's stack
   above `top` is cleared during that cycle.
4. **Barriers.** `luaC_threadbarrier` (`lgc.h:115`) — the invariant is spelled out
   at `lapi.cpp:36-43`: any API pushing a collectable object must call it or
   "stack references that point to dead objects" result, "since black threads
   don't get rescanned"; anything pushing a *new* object must call
   `luaC_checkGC` **before** it. Every public `lua_*` push does this for you; the
   rule matters only if the engine writes `TValue`s through internal headers.

| Limit | Value | Where |
|---|---|---|
| `LUA_MINSTACK` | 20 | `VM/include/luaconf.h:74` |
| `LUAI_MAXCSTACK` | 8000 | `luaconf.h:81` |
| `LUAI_MAXCALLS` | 20000 | `luaconf.h` (same block) |
| `LUAI_MAXCCALLS` | 200 | `luaconf.h:91` |

`LUAI_MAXCCALLS = 200` is the one that constrains a scheduler: `resume_start`
refuses with "C stack overflow" when `from->nCcalls >= 200` (`ldo.cpp:648`), so
deeply nested resume chains fail. `luaD_checkCstack` (`ldo.cpp:244-253`) raises at
exactly `MAXCCALLS` and throws `LUA_ERRERR` past a hard limit of 225.
`luaD_growCI` (`ldo.cpp:230-239`) applies the same +1/8 hard limit to
`LUAI_MAXCALLS` Luau frames.

`lua_checkstack` (`lua.h:143`) returns 0 on failure and enforces
`LUAI_MAXCSTACK`; `lua_rawcheckstack` bypasses that limit and cannot fail-soft.
`auxresume` checks `lua_checkstack(co, narg)` explicitly before moving args and
errors with "too many arguments to resume" (`lcorolib.cpp:35-46`).

---

## 3. Require, `.luaurc`, and module registration

`require` is **not** part of the VM or `luaL_openlibs` — grepping
`VM/include/lualib.h` and `VM/src/lbaselib.cpp` for it returns nothing. It comes
from the separate `Luau.Require` library, and if you never call `luaopen_require`
there is no `require` at all.

### 3.1 The `luarequire_Configuration` vtable

`Require/include/Luau/Require.h:76`. **15 function pointers**, validated at push
time by `validateConfig` (`Require/src/Require.cpp:12-40`), which raises a Luau
error naming the missing pointer.

**11 mandatory:** `is_require_allowed`, `reset`, `jump_to_alias`, `to_parent`,
`to_child`, `is_module_present`, `get_chunkname`, `get_loadname`,
`get_cache_key`, `get_config_status`, `load`.
**Optional:** `to_alias_override`, `to_alias_fallback`,
`get_luau_config_timeout`.
**Exactly one of `get_alias` / `get_config` must be set** — both is
"cannot define both", neither is also an error (`Require.cpp:34-37`).

The struct is **copied by value** into a full userdatum upvalue at push time
(`Require.cpp:50-54`), so mutating your struct afterwards has no effect, and each
`luarequire_pushrequire` / `luaopen_require` / `luarequire_pushproxyrequire` call
makes its own independent copy by invoking `config_init` again. The struct is
value-initialised (`{}`) before your init runs (`Require.cpp:54`), so unset
optional pointers are guaranteed null.

| Member | Signature (abridged) | Semantics |
|---|---|---|
| `is_require_allowed` | `bool (…, const char* requirer_chunkname)` | Called **first** inside `resolveRequire` (`RequireImpl.cpp:87`); false → "require is not supported in this context". |
| `reset` | `NavigateResult (…, const char* requirer_chunkname)` | Points state at the **module itself**, not its directory (`RequireNavigator.cpp:77, 131, 151`). |
| `jump_to_alias` | `NavigateResult (…, const char* path)` | Only when an alias value is neither `./…` nor `../…` nor `@…` (`RequireNavigator.cpp:255`) — the escape hatch for absolute or opaque targets. |
| `to_alias_override` | `NavigateResult (…, const char* alias_unprefixed)` | Optional. Receives the alias without `@`, already lowercased. Called after `reset` and **before** any config search (`RequireNavigator.cpp:94`, again per chained hop at `:215`). **The correct hook for engine-namespace aliases resolved without touching a filesystem.** |
| `to_alias_fallback` | `NavigateResult (…, const char* alias_unprefixed)` | Optional last chance after the whole ancestor chain failed (`RequireNavigator.cpp:140, 245`). |
| `to_parent` / `to_child` | `NavigateResult (…)` / `(…, const char* name)` | The only state-mutating primitives. Extension resolution and `init.luau` live in `to_child` — `Luau.Require` knows nothing about files. |
| `is_module_present` | `bool (…)` | Queried once after navigation (`RequireImpl.cpp:100`); gates the three write callbacks. |
| `get_cache_key` | `WriteResult (…, char*, size_t, size_t*)` | Fetched **first** (`RequireImpl.cpp:103`), before the cache is consulted. Must be cheap and a stable module identity. |
| `get_chunkname` / `get_loadname` | as above | Fetched only on a cache **miss** (`RequireImpl.cpp:117-123`). |
| `get_config_status` | `ConfigStatus (…)` | Mandatory even if you never use config files — return `CONFIG_ABSENT`. |
| `get_alias` **xor** `get_config` | `WriteResult (…)` | See §3.3. |
| `get_luau_config_timeout` | `int (…)` | Optional; default 2000 ms, negative = infinite (`Navigation.cpp:48, 204-211`). |
| `load` | `int (…, const char* path, const char* chunkname, const char* loadname)` | Returns the number of results pushed, or `-1` to yield. |

**Enums.** `luarequire_NavigateResult` (`Require.h:50`) is
`{NAVIGATE_SUCCESS, NAVIGATE_AMBIGUOUS, NAVIGATE_NOT_FOUND}` — three states, and
there is **no `NAVIGATE_FAILURE`** despite the header comment at `Require.h:122`
naming one. The distinction matters: at `RequireNavigator.cpp:271` a `NOT_FOUND`
from `to_parent` during the alias search is treated as "reached the root" and
silently ends the search, whereas `AMBIGUOUS` is a hard error. Getting that wrong
in your `to_parent` silently changes alias resolution.

`luarequire_WriteResult` (`Require.h:60`) is
`{WRITE_SUCCESS, WRITE_BUFFER_TOO_SMALL, WRITE_FAILURE}`. Contract at
`Require.h:57-59`: on success set `*size_out` to bytes written; on
`BUFFER_TOO_SMALL` set it to the **required** size. The consumer
(`Navigation.cpp:138-161`) retries **exactly once**; a second
`BUFFER_TOO_SMALL` becomes failure ("could not get chunkname/loadname/cache key
for module"). Initial buffers are **64 bytes** for chunkname/loadname/cache-key/
alias and **1024 bytes** for `get_config` (`Navigation.cpp:12-13`), so any path
longer than 63 bytes costs two calls.

`luarequire_ConfigStatus` (`Require.h:68`) is
`{CONFIG_ABSENT, CONFIG_AMBIGUOUS, CONFIG_PRESENT_JSON, CONFIG_PRESENT_LUAU}`.

**`load` returning -1 (yield)** requires the stack to be **exactly 4 deep** or you
get "stack cannot be modified when require yields"
(`RequireImpl.cpp:357-358`); `require` then does `lua_yield(L, 0)` and the
embedder must resume the requiring thread with the module result pushed, at which
point `lua_requirecont` runs as the continuation. More than one result → "module
must return a single value". **Zero results → nothing is cached** and the module
re-runs on the next require.

**Entry points:**

| Signature | Where | Notes |
|---|---|---|
| `int luarequire_pushrequire(lua_State* L, luarequire_Configuration_init init, void* ctx)` | `Require.h:162` (impl `Require.cpp:65`) | Pushes the closure without registering a global. Upvalues: the copied config as full userdata, and `ctx` as **light** userdata. |
| `void luaopen_require(lua_State* L, luarequire_Configuration_init init, void* ctx)` | `Require.h:165` (impl `Require.cpp:70-74`) | `pushrequire` + `lua_setglobal(L, "require")`. **Must run before `luaL_sandbox`** (`CLI/src/Repl.cpp:228-229`). |
| `int luarequire_pushproxyrequire(lua_State* L, …)` | `Require.h:171` (impl `Require.cpp:76-79`) | A two-argument closure `(path, requirer_chunkname)` that resolves as if required from the named module. Shares the same caches. **This is what a hot-reload/dev server needs** to re-resolve on behalf of another chunk without a real call frame. |
| `int luarequire_registermodule(lua_State* L)` | `Require.h:176` (impl `RequireImpl.cpp:386-419`) | A `lua_CFunction`, not a plain helper — push it and `lua_call` with exactly 2 args. |
| `int luarequire_clearcacheentry(lua_State* L)` | `Require.h:180` (impl `RequireImpl.cpp:421-429`) | One arg: the **cache key**, not the require path. |
| `int luarequire_clearcache(lua_State* L)` | `Require.h:183` (impl `RequireImpl.cpp:431-436`) | No args. Replaces `_MODULES` with a fresh table. |

Because `ctx` is *light* userdata, Luau does not own it — the CLI pins its
requirer object in the registry keyed by its own address to keep it alive
(`CLI/src/Repl.cpp:196-200`).

**Registered modules (U-39, U-40).** `luarequire_registermodule` enforces
`lua_gettop == 2` and `path[0] == '@'`, ASCII-lowercases the path before storage,
and does **not** type-check the value — any Lua value is accepted, and registering
nil effectively unregisters. Registration is idempotent overwrite.

`checkRegisteredModules` (`RequireImpl.cpp:133-153`) lowercases the **entire**
require string with a manual A–Z shift and does a single `lua_getfield` on the
registry table `_REGISTEREDMODULES`. It is **exact whole-string equality**: no
prefix matching, no namespace matching, no fallthrough to sub-paths. Registering
`@luaug` does not make `require('@luaug/render')` resolve — every module path
must be registered individually and eagerly (the value is a constructed Lua
value, not a factory, so there is no lazy-init hook).

It runs at `RequireImpl.cpp:318-321`, i.e. **before `is_require_allowed`**,
before the Navigator, and before the `_MODULES` cache. So any chunk your policy
denies — including `loadstring`'d code — can still `require('@luaug/…')` and
obtain the engine table. Under R4 the gate must be inside the exposed modules
themselves, not in `is_require_allowed`.

Registered modules also never enter `_MODULES`, so `luarequire_clearcache` and
`luarequire_clearcacheentry` cannot invalidate them; and the lowercasing makes
these paths effectively case-insensitive, which sits awkwardly with a
case-sensitive PascalCase convention.

### 3.2 The resolution algorithm

`lua_require` (`RequireImpl.cpp:372`) walks up call levels with
`lua_getinfo(L, level++, "s", &ar)`, skipping frames whose `what[0] == 'C'`, and
uses `ar.source` as the requirer chunkname; with no Luau frame it errors "require
is not supported in this context". So **`require` cannot be invoked from a pure C
call chain**, and the chunkname you passed to `luau_load` *is* the requirer
identity — your `reset()` must map it back to a location.

`Navigator::navigate` (`RequireNavigator.cpp:44`) first replaces every `\` with
`/` (line 46) — backslashes are accepted and normalised. `getPathType`
(`PathUtilities.cpp:10`) allows exactly three prefixes: `./`, `../`, and a
leading `@`; anything else is "require path must start with a valid prefix: ./,
../, or @". Bare names and absolute paths are rejected outright. `.` and `..`
alone (no trailing slash) are also unsupported.

`navigateImpl` (`RequireNavigator.cpp:57`) for both relative forms does
`resetToRequirer()` then **one unconditional `to_parent`** — from the requirer
*module* to its container — then walks the path. So `./x` is a sibling of the
requirer and `../x` takes one more step up (the leading `..` is consumed again
inside `navigateThroughPath`). **Config files are never consulted for relative
requires.**

`navigateThroughPath` (`:162`) drops the first component when the path starts
with `@` (the caller already positioned the state), then per component: `.` and
empty are **skipped** (so `//`, a trailing `/`, and `/./` are tolerated), `..` →
`to_parent`, anything else → `to_child`.

`extractAlias` (`:23`) takes everything after `@` up to the first `/`, and the
result is ASCII-lowercased before any lookup (`:67-75`) — which is why
`require('@TeSt/heLLoWoRld')` matches a module registered as `@test/helloworld`
(`tests/RequireByString.test.cpp:609`). `@` alone yields the empty alias and the
error " is not a valid alias".

**The ancestor config search** (`navigateToAndPopulateConfig`, `:262`) is the key
semantic:

> clear the `Config` **wholesale** (line 266) → `to_parent` → `get_config_status`
> → if present, parse it entirely into the cleared `Config` → repeat until the
> config contains the desired alias, or `to_parent` returns `NOT_FOUND`
> (interpreted as *root*, not an error).

Consequences: **aliases do not merge** across the chain — the nearest ancestor
config that *defines* the alias wins completely; a nearer config that does not
define it is discarded; the parse uses `overwriteAliases = false` and no
`configLocation` (`:298-301`); a **parse error anywhere on the chain aborts the
whole require**, even if an ancestor further up would have defined the alias; and
the cursor is left wherever the search stopped, so the alias value resolves
relative to *that* config's directory.

This is half of U-37. The other half: the analyzer (`CLI/src/Analyze.cpp:252-318`)
recurses **root-downward** and **accumulates** configs with `overwriteAliases =
true`, so a child `.luaurc` overrides a parent's alias while inheriting the rest.
The two give different answers whenever an alias is redefined along a chain.
Worse, the analyzer-side `.luaurc` file resolver lives in `CLI/src/Analyze.cpp`,
which is an *executable's* source, not a library — LuauG cannot link it and must
reimplement it (`Luau.Analysis` only defines the abstract `ConfigResolver`
interface at `Analysis/include/Luau/ConfigResolver.h:12`).

**Chained aliases** (`navigateToAlias`, `:197`): an alias value may itself be
`./…`, `../…`, `@another`, or opaque. Relative values are walked from the
*current* state (wherever the config search left the cursor). `@another` recurses
with an `AliasCycleTracker`; a repeat yields "detected alias cycle (@a -> @b ->
@c -> @a)" (`AliasCycleTracker.cpp:14-22`). Subtlety at `:233-247`: a chained
alias not present in the current config triggers a **fresh** upward config
search, and the cycle tracker is **reset to `{}`** for that new config (line 240)
— cycle detection is per-config-file, not global.

**`@self` is not unconditional at this pin (U-43).**
`DFFlag::LuauSelfIsSelfAndAlwaysSelf` defaults to **false**
(`RequireNavigator.cpp:16`). With it **on** (`:80-92`), `@self` short-circuits
immediately after `resetToRequirer` and neither `to_alias_override` nor a config
alias named `self` is consulted. With it **off** — the default (`:121-138`) —
`to_alias_override` gets a shot at `self`, and the ancestor config chain is
searched **first**; only if nothing defines `self` does it fall back. Upstream's
own fixture defines `"self": "./this_should_never_be_read"` and its test must
enable the flag to get the documented behaviour. Either set the flag at boot or
reject `self` as an alias key in generated projects. (Either way the *meaning* of
`@self` is "navigate from the requirer module itself", so `@self/sub` from
`nested/init.luau` resolves to `nested/sub`.)

**The reference filesystem model** is `VfsNavigator`
(`CLI/include/Luau/VfsNavigator.h:14`, impl `CLI/src/VfsNavigator.cpp`), and it
*is* linkable — `Luau.CLI.lib` is built unconditionally
(`Sources.cmake:451-459`). Its rules: `kSuffixes = {".luau", ".lua"}`,
`kInitSuffixes = {"/init.luau", "/init.lua"}` (`:13-14`); a component resolves to
`<p>.luau`, `<p>.lua`, or a directory `<p>/` containing `init.luau`/`init.lua`;
**two matches at the same level is `Ambiguous`** (a file `foo.luau` next to a
directory `foo/`, or `foo.luau` next to `foo.lua`); a component literally named
`init` is never given a suffix (`:31`), which is why
`require('.../nested/init')` fails; directories with no init file are still
`Success` (`:62`) so they can be traversed as pure containers; `toChild` refuses
`.config` (`:200-201`) so `.config.luau` can never be required as a module —
there is no equivalent guard for `.luaurc` because it has no module-looking
extension; `getConfigPath` (`:219`) strips the module suffix, so a module's own
directory is where its config is looked for; and `getConfigStatus` (`:243`)
returns `Ambiguous` when `.luaurc` and `.config.luau` coexist.

**But `VfsNavigator` is CWD-dependent (U-41).** `resetToPath` (`:143`) calls
`getCurrentWorkingDirectory()` for a non-absolute requirer path, and the derived
absolute path is also the cache key the CLI uses. For deterministic, relocatable
cache keys (R10, and the bytecode-cache design) the engine must define its own
`get_cache_key` over project-relative paths. Treat `VfsNavigator` as a
specification to reimplement, not a component to reuse.

`ReplRequirer` (`CLI/src/ReplRequirer.cpp:220`) is the only complete worked
example of the whole vtable in the tree, and its `load` (`:136`) is the pattern to
copy: create the module thread on the **main** thread so it does not inherit
`L`'s environment (`lua_mainthread` + `lua_newthread` + `lua_xmove`),
`luaL_sandboxthread` it, read the file, `Luau::compile`, `luau_load` with the
chunkname, optionally `createplaceholder`, codegen/coverage hooks, `lua_resume`,
then xmove the single result back and `lua_remove` the thread. It is compiled
only into the CLI executable (`Sources.cmake:461-475`), **not** into any library
— read and reimplement.

### 3.3 `.luaurc`, `.config.luau`, and what Luau actually parses

**There is no config *reader* in `Luau.Require` (U-41).** The library contains
zero filesystem knowledge: locating `.luaurc`/`.config.luau`, deciding JSON vs
Luau, and reading the bytes are all `get_config_status` / `get_config`. Only
**parsing** is vendored. `kConfigName = ".luaurc"` (`Config/include/Luau/Config.h:19`)
and `kLuauConfigName = ".config.luau"` (`LuauConfig.h:16`) are just constants;
neither name appears in `Luau.Require`.

`get_config_status` is consulted **only inside the alias search loop, after a
`to_parent` step** (`RequireNavigator.cpp:274`). It is never consulted for
relative requires and never at the requirer's own level before the first
`to_parent` — matching the header note (`Require.h:42-46`) that a config's scope
is its descendants. `CONFIG_AMBIGUOUS` aborts the require with
"could not resolve alias \"<a>\" (ambiguous configuration file)".

**`get_alias` versus `get_config`** (`Navigation.cpp:121-126`: `get_alias` wins
if set). Choosing `get_alias` disables Luau's internal config parsing entirely —
you are handed the lowercased alias and return its value. The asymmetry that
matters: with `get_alias` a miss is **fatal immediately**
(`RequireNavigator.cpp:287-291`), whereas with `get_config` a config that simply
lacks the alias lets the search continue up the tree (the loop condition at
`:264` versus the unconditional `break` at `:291`). If LuauG resolves aliases
from its own project manifest, `get_alias` is the pointer to implement — but it
costs the upward-continuation behaviour.

**`Luau::parseConfig`** (`Config/include/Luau/Config.h:88`, impl
`Config/src/Config.cpp:360-390`) is genuinely shared: the runtime calls it from
`RequireNavigator.cpp:305` and the analyzer's config resolver from
`CLI/src/Analyze.cpp:285`. Recognised keys and **nothing else**: `languageMode`
(`nocheck|nonstrict|strict`, plus `noinfer` under compat), `lint.<Name>` (or
`lint."*"`), `lintErrors`, `typeErrors`, `globals` (array of strings),
`aliases.<key>`, and `language.mode` only under `options.compat`.

> **Any other key is a hard error** — `"Unknown key <a/b>"` (`Config.cpp:386`) —
> and it aborts the entire require, not just the alias lookup. A `$schema` field,
> an LSP-specific key, or a numeric value anywhere in the file breaks `require`
> at runtime even though the alias it needed was elsewhere in the file (U-42).

**The `.luaurc` "JSON" grammar is not JSON.** `parseJson`
(`Config/src/Config.cpp:246`) is hand-rolled on the Luau Lexer. Consequences:

- C-style `//` comments are skipped (the `next` helper at `:230-237` advances
  past `Lexeme::FloorDiv` to end of line);
- **trailing commas are accepted** (the `ParseAliases` test at
  `tests/RequireByString.test.cpp:786-791` relies on one);
- the only scalar values allowed are quoted strings and the bare literals
  `true`/`false` — **numbers and `null` are syntax errors**
  ("Expected field value at line N");
- arrays may contain only quoted strings and may not nest (`arrayTop` is a single
  bool, `:255`);
- errors carry a 1-based line number.

**Alias key quirks.** `isValidAlias` (`Config.cpp:180`) rejects empty, `.`, `..`,
and anything containing `/` or `\`; it allows `A-Za-z0-9-_.` plus a `@` **only at
index 0**. So a key written `"@dep"` passes validation and is stored under
`"@dep"` — while the Navigator strips `@` before lookup (`extractAlias`) and will
never find it. **Write alias keys unprefixed.**

`Config::setAlias` (`Config.h:50`, impl `Config.cpp:63-80`) stores under
`toLower(alias)` and keeps `originalCase`; `AliasInfo::configLocation` is a
`string_view` into an internal cache, and `Config`'s copy constructor
(`Config.h:20-36`) exists specifically to re-intern those views — a memcpy-style
copy would leave dangling views.

`parseAlias` (`Config.cpp:206`) tests `!config.aliases.contains(aliasKey)` at
line 219 using the **raw** key while `setAlias` stores the **lowercased** one.
With `overwriteAliases = false` — exactly what the runtime uses
(`RequireNavigator.cpp:300`) — a duplicate key containing any uppercase letter
therefore fails the `contains()` check and silently **overwrites**, while an
all-lowercase duplicate is correctly first-wins. Worth defending against in a
project-template generator or a `luaug check` lint.

**`.config.luau` is a different language.** `Luau::extractLuauConfig`
(`LuauConfig.h:92`, impl `Config/src/LuauConfig.cpp:329-352`) creates a brand-new
`luaL_newstate` + `luaL_openlibs` + `luaL_sandbox`, **compiles** the source with
`Luau::compile`, loads it as chunkname `=config`, resumes it, and serialises the
returned table. It must return exactly one table; yielding is an error. Only the
nested `luau` sub-table is read (`:319-326`), and inside it keys are matched
**lowercase-only**: `languagemode`, `lint`, `linterrors`, `typeerrors`,
`globals`, `aliases` (`:193-307`). A `.config.luau` written with camelCase
`languageMode` is **silently ignored** — the opposite convention from `.luaurc`.
Unknown keys inside `luau` are silently ignored, also the opposite of the JSON
path.

Its timeout is enforced by an interrupt on the config VM
(`InterruptCallbacks`, `LuauConfig.h:79`; wired at `Navigation.cpp:46-59`) with a
`steady_clock` deadline; expiry raises "configuration execution timed out".
**That makes `.config.luau` resolution wall-clock dependent and therefore
non-deterministic (R10)** — a reason for LuauG to support `.luaurc` only, or to
return a negative timeout.

`extractLuauConfigFromBytecode` (`LuauConfig.h:101`, impl `LuauConfig.cpp:354-375`)
and `NavigationContext::ConfigStatus::PresentLuauBytecode`
(`RequireNavigator.h:87`) both exist but are **unreachable from the require
pipeline**: the C enum has no corresponding value, `convertConfigStatus`
(`Navigation.cpp:28-38`) cannot produce it, and
`Navigator::navigateToAndPopulateConfig` (`RequireNavigator.cpp:303-316`) handles
only `PresentJson` and `PresentLuau`, falling through silently otherwise. Do not
design a precompiled-config path around it.

**For static tooling**, `Luau::Require::NavigationContext`
(`Require/include/Luau/RequireNavigator.h:48`) is the supported C++ interface:
four pure virtuals (`resetToRequirer`, `jumpToAlias`, `toParent`, `toChild`) plus
defaulted alias-override/fallback and config hooks. Note **two public data
members, not virtuals**: `luauConfigInit` (`std::function<void(lua_State*)>`) and
`luauConfigInterrupt` (raw function pointer) at `:95-96` — set them yourself if
you return `PresentLuau`. `Luau::Require::Navigator` (`:128`) is documented as
not-to-be-overridden; all customisation goes through the context. This is the way
to give an analyzer, an LSP, or an asset pipeline byte-identical resolution.

### 3.4 Caching, cycles, and hot reload

The one and only module cache is a plain table in the **Lua registry** under the
string key `"_MODULES"` (`RequireImpl.cpp:22`), created lazily with
`luaL_findtable`. Because it lives in the registry it is per-`global_State`,
shared by every `lua_newthread` of that VM — that is what makes "one evaluation
per module per VM" true. Keys are whatever `get_cache_key` returns. The upstream
tests poke it by name (`tests/RequireByString.test.cpp:545`), so the key is de
facto public API.

**Failures are never cached (U-35).** `_MODULES` is written **only** in
`lua_requirecont` (`RequireImpl.cpp:236-303`), which is reached only if `load`
returned normally. When a module body errors, the error propagates and skips the
continuation, so the next `require` re-executes the module from scratch.
`docs/api-design.md:361` says the opposite.

The **one** exception is worse than nothing: `luarequire_createplaceholder`
writes into `_MODULES` **before** the body runs, so if the body then errors a
**locked placeholder** is left in the cache permanently, and every later require
of that key returns a table that raises on any field access. Nothing in the
vendored tree cleans that up. If LuauG wants failure caching, or placeholder
cleanup on error, it must implement both in its own `load` callback.

**Cyclic require is off by default and is not general (U-36).** It requires
**all** of:

- `FFlag::LuauCyclicRequireShortCircuit = true` (`LUAU_FASTFLAGVARIABLE`,
  default false, `RequireImpl.cpp:13`);
- `FFlag::LuauExportValueSyntax = true` (default false, `Ast/src/Parser.cpp:25`);
- the participating modules written with the **`export` keyword** —
  `lua_usesexport` (`lua.h:466`, impl `lapi.cpp:1997-2003`) returns 1 only for a
  Luau closure whose proto has `LPF_USES_EXPORT`, and that is what the embedder
  checks before calling `createplaceholder` (`ReplRequirer.cpp:170-171`);
- the embedder's `load` callback actually calling
  `luarequire_createplaceholder`.

All four upstream cyclic tests enable both flags
(`tests/RequireByString.test.cpp:964, 974, 982, 990`). **With any of those
missing there is no cycle guard at all** in `lua_requireinternal` — a plain
`require` cycle re-enters `load` recursively until the C stack is exhausted. A
cycle between two ordinary `return {}` modules is **not supported at this pin
under any flag combination.**

The placeholder API:

| Signature | Where | Notes |
|---|---|---|
| `void luarequire_createplaceholder(lua_State* L)` | `Require.h:195` (impl `RequireImpl.cpp:195-206`) | Reads the cache key from **absolute stack index 2** (`luaL_checkstring(L, 2)`) — it depends on the fixed 4-value require frame `(path, cacheKey, chunkname, loadname)` (`kRequireStackValues = 4`, `:234`). Call it from inside `load`, on the `L` passed to `load`, with that stack untouched. |
| `void luarequire_lockplaceholder(lua_State* L, int idx)` | `Require.h:187` (impl `:187-193`) | Attaches the shared locked metatable (`__index`/`__newindex` both raise, `__metatable = "The metatable is locked"`) and sets readonly. |
| `void luarequire_populateplaceholder(lua_State* L, int placeholderIdx, int resultIdx)` | `Require.h:191` (impl `:208-230`) | Unfreezes, `lua_rawiter`-copies every field plus the metatable, refreezes. **Raw fields only**; a non-table result cannot be populated at all. |

Two more registry entries exist: `_CYCLIC_PLACEHOLDER_PROVIDED` (a bool-valued
table keyed by cache key) and the shared placeholder metatable stored via
`lua_rawsetp` under the address of a file-static `char`
(`RequireImpl.cpp:25`). **Neither `luarequire_clearcache` nor
`luarequire_clearcacheentry` prunes the provided-table.** Combined with the
placeholder-left-on-error problem, a hot-reload loop that clears the cache after
a failed cyclic load can leave a stale `true` entry that steers the next
successful load of the same key down the populate-placeholder branch. If LuauG
enables the cyclic flag, its reload path must clear that table too — it is
reachable by name with `luaL_findtable` on `LUA_REGISTRYINDEX`, the same way the
upstream tests reach `_MODULES`.

### 3.5 The link consequence (U-38)

```cmake
target_link_libraries(Luau.Require PUBLIC Luau.Config Luau.VM)
target_link_libraries(Luau.Config  PUBLIC Luau.Ast)
target_link_libraries(Luau.Config  PRIVATE Luau.Compiler Luau.VM)
```
`third_party/luau/CMakeLists.txt:135` and `:118-119`.

`Luau.Require` is created unconditionally (`CMakeLists.txt:42/55`) regardless of
`LUAU_BUILD_CLI`, so `LUAU_BUILD_CLI=OFF` does not remove it. But **linking it
drags in `Luau.Config`, which needs `Luau.Ast` publicly (the Lexer, for the
`.luaurc` parser) and `Luau.Compiler` privately** — because
`Config/src/LuauConfig.cpp:104` calls `Luau::compile` to execute `.config.luau`.
For a **static** library a PRIVATE dependency still propagates as a
`$<LINK_ONLY:>` link requirement.

That collides head-on with the ADR 0002 shipping story recorded in
`cmake/luaug_luau.cmake` — "shipping builds can drop [the Compiler] and load only
precompiled bytecode". You cannot have require-by-string via `Luau.Require` in a
compiler-free ship build without patching `third_party` (which R13 forbids), or
writing your own require on top of the `RequireNavigator` C++ interface with a
`NavigationContext` that never reports `PresentLuau`. `Luau.Require` is also
absent from the install/export list at `CMakeLists.txt:339`.

---

## 4. Allocator, memory categories, GC pacing, compile options, vectors

### 4.1 The allocator contract

```c
typedef void* (*lua_Alloc)(void* ud, void* ptr, size_t osize, size_t nsize);
lua_State* lua_newstate(lua_Alloc f, void* ud);
lua_Alloc  lua_getallocf(lua_State* L, void** ud);
```
`lua.h:55`, `:126`, `:471`. **There is no `lua_setallocf` at this pin (U-50)** —
only the getter is declared. The allocator is fixed for the life of the state and
can only be chosen at `lua_newstate`, and `ud` is the only channel for per-VM
context inside it. `luaL_newstate` (`lualib.h:58`, impl `linit.cpp:124-127`) is
just `lua_newstate(l_alloc, NULL)` and hard-wires the default malloc/realloc
allocator (`linit.cpp:111-122`). Moving off it is a prerequisite for any
memory-cap work.

The authoritative contract is the header comment at `VM/src/lmem.cpp:18-25`:

- `frealloc(ud, NULL, 0, x)` creates a new block of size `x`;
- `frealloc(ud, p, x, 0)` frees `p` and **must return NULL**;
- `frealloc(ud, NULL, 0, 0)` does nothing;
- returns NULL if it cannot create or reallocate;
- **"any reallocation to an equal or smaller size cannot fail!"**

The must-return-NULL rule is asserted at `lmem.cpp:667`
(`LUAU_ASSERT((nsize == 0) == (result == NULL))`), so a wrapper returning a
non-NULL sentinel on free trips an assert in debug and corrupts accounting in
release. The default `l_alloc` shows the minimal shape: `if (nsize == 0) { free(ptr); return NULL; } else return realloc(ptr, nsize);`
— `osize` ignored. Note there is no allocation-kind information available to the
allocator; `osize` is a size, not a tag.

**A memory cap can only produce `LUA_ERRMEM` (U-46).** Every path converts a NULL
return into `luaD_throw(L, LUA_ERRMEM)` (`lmem.cpp:504-505, 544-545, 649-650,
663-664`), carrying Luau's fixed `LUA_MEMERRMSG`. There is no hook to substitute
a `TextKey` at the throw site. Three further constraints:

1. a **shrink must never fail**, per the contract — the cap can only reject
   growth;
2. the very first allocation is `(*f)(ud, NULL, 0, sizeof(LG))` inside
   `lua_newstate` **before any `lua_State` exists** (`lstate.cpp:195`), so
   refusing it makes `lua_newstate` return NULL rather than raise;
3. the size passed to `frealloc` is usually a 16 KB or 32 KB **page**, not an
   object, so the cap granularity is coarse.

The workable design is: return NULL to enforce, then translate the resulting
`LUA_ERRMEM` into the keyed error at the pcall/resume boundary where the host
already builds its error object.

**Luau's paged allocator means a `frealloc` wrapper sees the wrong events
(U-47).** Allocations of 1..1024 bytes are served from size-segregated pages
(`kMaxSmallSizeUsed = 1024`, `lmem.cpp:137`; `sizeclass()` macro at `:197`;
`LUA_SIZECLASSES = 40`, `luaconf.h:111`). Only pages themselves and >1024-byte
blocks reach `frealloc`. A wrapper therefore observes **page churn, not
allocation churn**.

> **Stale comment warning.** `lmem.cpp:47-48` and `:64-65` both state the paged
> threshold is "currently 512 bytes". The constant is 1024. 512 is
> `kLargePageThreshold` (`:139`), which only chooses between 16 KB and 32 KB page
> sizes. Any perf reasoning copied from those comments is off by 2×.

If per-allocation visibility is what is actually wanted, the hook is
`lua_Callbacks::onallocate(lua_State* L, size_t osize, size_t nsize)`
(`lua.h:614`), fired from `luaM_new_` (`lmem.cpp:510-513`, with `osize = 0`),
`luaM_newgco_` (`:550-553`) and `luaM_realloc_` (`:671-674`). Unlike a `lua_Alloc`
wrapper it sees every Luau-level object allocation **including ones served from
pages**. It is not called on frees, and like all callbacks it may only be set
while the VM is not running code.

`lua_close` (`lua.h:127`, impl `lstate.cpp:299`) redirects to
`L->global->mainthread` first — passing any thread works. In an assert-enabled
build it asserts `totalbytes == sizeof(LG)`, `memcatbytes[0] == sizeof(LG)` and
`memcatbytes[i] == 0` for every other `i` (`lstate.cpp:121-124`): a per-category
accounting imbalance is a debug break at shutdown.

### 4.2 Memory categories

| Signature | Where | Notes |
|---|---|---|
| `void lua_setmemcat(lua_State* L, int category)` | `lua.h:338` (impl `lapi.cpp:2035-2039`) | **Per-thread**: sets `lua_State::activememcat` (`lstate.h:274`). New threads **inherit** the creator's value (`lstate.cpp:137`). |
| `size_t lua_totalbytes(lua_State* L, int category)` | `lua.h:339` (impl `lapi.cpp:2041-2045`) | A **negative** category is the supported way to read the whole-heap total in bytes — exact, unlike `lua_gc(LUA_GCCOUNT)` which is KB-truncated. |
| `void lua_memorydump(lua_State* L, void* file, lua_CategoryName categoryName)` | `lua.h:327` | Writes a JSON dump to a `FILE*` (`file` must be non-null, `api_check` at `lapi.cpp:1459`). The callback labels each memcat — the natural place to surface the engine's category map. |

**256 is a hard ceiling, not a tunable (U-48).** `LUA_MEMORY_CATEGORIES`
(`luaconf.h:116`) is `#ifndef`-guarded and looks overridable, but the category is
stored as **one byte** in every GC object's `CommonHeader`
(`uint8_t tt; uint8_t marked; uint8_t memcat`, `lobject.h:17-18`), and
`lua_CategoryName` takes a `uint8_t` (`lua.h:327`). Raising the macro buys
nothing. A 32–255 per-Script pool therefore caps at **224 concurrently-attributed
scripts**, and a "coalesce oldest when exhausted" fallback is mandatory rather
than a nicety.

**The range check is not enforced in release.** `api_check` is `LUAU_ASSERT`
(`lcommon.h:14`), and the assignment then narrows through `uint8_t(category)`
(`lapi.cpp:2038`), so `lua_setmemcat(L, 300)` silently becomes category 44 in a
shipping build. Validate host-side before every call.

Accounting is maintained inside `lmem.cpp` and exposed by `lua_totalbytes`,
backed by `size_t memcatbytes[LUA_MEMORY_CATEGORIES]` on `global_State`
(`lstate.h:241`). **A host-side counting allocator duplicates work the VM already
does** (U-47).

### 4.3 GC control and pacing

`int lua_gc(lua_State* L, int what, int data)` — `lua.h:325`, impl
`lapi.cpp:1324-1455`. Returns 0 for options with no result, the **previous**
value for the three setters, and **-1 for an unrecognized option** (`:1451-1452`).

`enum lua_GCOp` (`lua.h:275-323`) is unvalued, so the ordinals are positional:

| Op | Value | Semantics (impl) |
|---|---|---|
| `LUA_GCSTOP` | 0 | `GCthreshold = SIZE_MAX` (`lapi.cpp:1331-1335`) |
| `LUA_GCRESTART` | 1 | `GCthreshold = totalbytes` (`:1336-1340`) |
| `LUA_GCCOLLECT` | 2 | `luaC_fullgc` (`:1341-1345`) |
| `LUA_GCCOUNT` | 3 | `totalbytes >> 10`, i.e. **KB** (`:1346-1351`) |
| `LUA_GCCOUNTB` | 4 | `totalbytes & 1023` (`:1352-1356`) |
| `LUA_GCISRUNNING` | 5 | `GCthreshold != SIZE_MAX` (`:1357-1361`) |
| `LUA_GCSTEP` | 6 | see below (`:1362`) |
| `LUA_GCSETGOAL` | 7 | percentage; returns previous (`:1427`) |
| `LUA_GCSETSTEPMUL` | 8 | percentage; returns previous |
| `LUA_GCSETSTEPSIZE` | 9 | **KB** in and out (`:1439-1444`) |
| `LUA_GCISPAUSED` | 10 | `gcstate == GCSpause` (`:1446-1450`) |

> **These do not line up with Lua 5.1.** There is no `LUA_GCSETPAUSE` at all, and
> slot 7 is `LUA_GCSETGOAL` where 5.1 has `LUA_GCSETSTEPMUL`. Anything
> cross-referencing PUC-Lua documentation for GC tuning — including the
> goal/stepmul guidance itself, which is Luau-specific and lives at
> `lua.h:301-316` — will be wrong by one or two slots.

**`LUA_GCSTEP`'s argument is in kilobytes** (U-45): `lapi.cpp:1362` does
`size_t amount = (cast_to(size_t, data) << 10)`, and the header comment at
`lua.h:292` says so. It lowers `GCthreshold` by that amount and loops `luaC_step`
until the threshold is met or the cycle ends; returns 1 if the cycle finished, 0
otherwise (`:1387-1391`). It tracks `actualwork` and pushes `GCthreshold` forward
by `totalbytes + actualwork + oldcredit` so an explicit step does not
double-charge assists (`:1418-1424`). If the GC is in its paused interval, an
explicit step triggers the start of the next cycle.

`LUA_GCSETSTEPSIZE` has the same unit trap: it stores `data << 10` and reads back
`gcstepsize >> 10` (`:1439-1444`).

**`lua_allocationrate` returns BYTES per second** (`lua.h:343`, impl
`lapi.cpp:2047-2050` → `luaC_allocationrate`, `lgc.cpp:1475-1497`). Mixing the
two units is a 1024× GC overrun per frame in one direction and a GC that never
keeps up in the other.

Two more properties the design docs do not account for:

- **It returns `-1` routinely, not exceptionally.** `lgc.cpp:1478` sets
  `durationthreshold = 1e-3` and lines `1484-1485` / `1493-1494` return `-1` if
  the window since the last cycle end (or between cycle end and atomic start,
  during sweep) is under 1 ms. A pacing formula that treats the result as
  unsigned or non-negative computes nonsense right after every cycle boundary.
  Branch on it explicitly.
- **It reads the wall clock.** `lgc.cpp:1482` and `:1491` call `lua_clock()`, a
  real monotonic timer (`VM/src/lperf.cpp:65`). Under R10 this value must be
  confined to sizing GC work — semantically transparent — and must never reach
  anything observable by the simulation: not a deterministic trace, not tick
  counts, not budgets that gate simulation work, not replay-relevant state.

Compile-time defaults, applied in `lua_newstate` (`lstate.cpp:230-232`):
`LUAI_GCGOAL 200`, `LUAI_GCSTEPMUL 200`, `LUAI_GCSTEPSIZE 1` (`VM/src/lgc.h:12`)
— note `gcstepsize` is stored as `LUAI_GCSTEPSIZE << 10` = 1024 bytes. `lgc.h` is
not a public header; read the values there, tune at runtime via `lua_gc`.

Header guidance at `lua.h:301-316`: G is the ratio of heap size to live data
(default 200% = the heap may reach ~2× live); S is collector pace versus
allocation pace (default 200%); the recommended S interval is
`[100/(G-100), 100+100/(G-100))` with a 150% floor — for G=200 use S in
[150,200], G=150 → [200,300], G=125 → [400,500].

GC states are `GCSpause=0, GCSpropagate=1, GCSpropagateagain=2, GCSatomic=3,
GCSsweep=4` (`lgc.h:19-23`). `GCStats` (`lstate.h:85`) is internal but documents
that Luau **already runs a proportional-integral controller** on the heap trigger
(`triggerterms[32]`, `triggerintegral`), which is worth knowing before adding a
second controller on top.

### 4.4 Compile options

```cpp
struct CompileOptions {
    int optimizationLevel = 1;
    int debugLevel = 1;
    int typeInfoLevel = 0;
    int coverageLevel = 0;
    const char* vectorLib = nullptr;
    const char* vectorCtor = nullptr;
    const char* vectorType = nullptr;
    int vectorPrecision = 0;
    const char* const* mutableGlobals = nullptr;
    const char* const* userdataTypes = nullptr;
    const char* const* librariesWithKnownMembers = nullptr;
    LibraryMemberTypeCallback libraryMemberTypeCb = nullptr;
    LibraryMemberConstantCallback libraryMemberConstantCb = nullptr;
    const char* const* disabledBuiltins = nullptr;
};
```
`Compiler/include/Luau/Compiler.h:27-75`, verbatim including defaults. The header
warns at line 26: "this structure is duplicated in `luacode.h`, don't forget to
change these in sync!". **Field order is load-bearing** because `luau_compile`
memcpys between the two. Callbacks:
`using LibraryMemberTypeCallback = int (*)(const char* library, const char* member);`
(line 20, returning a `LuauBytecodeType`) and
`using LibraryMemberConstantCallback = void (*)(const char* library, const char* member, CompileConstant* constant);`
(line 24). Entry points: `compileOrThrow` (`:96-97`) and
`std::string compile(const std::string& source, const CompileOptions& = {}, const ParseOptions& = {}, BytecodeEncoder* = nullptr)`
(`:100-105`), which encodes errors into the blob instead of throwing.

| Field | Meaning (header) | Real gates |
|---|---|---|
| `optimizationLevel` | 0 none · 1 baseline, debuggable · 2 includes inlining | `>=1` enables `analyzeBuiltins`, `buildTableConstantMap`, `foldConstants`, `predictTableShapes` (`Compiler.cpp:5253-5276`); `>=2` additionally enables builtin constant folding (`:5232`) and forces the type map (`:5291`) |
| `debugLevel` | 0 none · 1 line info + function names (backtraces) · 2 full, with local/upvalue names | |
| `typeInfoLevel` | 0 native modules only · 1 all modules | `if (typeInfoLevel >= 1 \|\| optimizationLevel >= 2) buildTypeMap(...)` (`:5291`) |
| `coverageLevel` | 0 none · 1 statement · 2 statement + expression | pairs with `lua_getcoverage` (`lua.h:582`) |
| `vectorLib` / `vectorCtor` | "alternative global builtin to construct vectors" | `Builtins.cpp:372-384`. Null `vectorCtor` → nothing happens. With `vectorLib` set the call must match `<lib>.<ctor>(...)`; without it, a bare global `<ctor>(...)`. Either yields `LBF_VECTOR`, the same id `vector.create` gets (`:265-267`) |
| `vectorType` | "alternative vector type name for type tables" | **One site only**: `Types.cpp:76-77`, `if (hostVectorType && ref->name == hostVectorType) return LBC_TYPE_VECTOR;` |
| `vectorPrecision` | 0 = f32 components, 1 = f64 | picks `Constant::Type_Vectord` during folding (`Compiler.cpp:5268`, refolds at `:3929/3944`) |
| `mutableGlobals` | "globals that are mutable; disables the import optimization" | `ValueTracking.cpp:150-159` |
| `userdataTypes` | "userdata types included in the type information" | `Compiler.cpp:5279-5289` |
| `librariesWithKnownMembers` + the two callbacks | | turns on `builtinsFoldLibraryK` (`Compiler.cpp:5241-5251`); requires `optimizationLevel >= 2` |
| `disabledBuiltins` | entries are `"name"` or `"lib.name"` | `Builtins.cpp:414-443` |

**`vectorType` does not cause folding or fastcalls (U-44).** ADR 0013
(`../decisions/0013-vector3-native-luau-vector.md:16-18`) says the three options
together "give constant folding + fastcalls", and `docs/architecture.md:533-535`
repeats the triple. In the vendored source `vectorType` reaches exactly one line
and only makes a type *annotation* spelled `Vector3` encode as `LBC_TYPE_VECTOR`
in the typeinfo blob. Folding and fastcalls come from `vectorLib` + `vectorCtor`
alone. Keep setting `vectorType` — it helps native codegen specialize — but do
not expect it to affect `Vector3.new(...)` codegen, and do not treat its absence
as the cause if folding fails.

**The full set of conditions for `Vector3.new(1,2,3)` to become a constant**
(gate at `Compiler.cpp:5232`):

1. `optimizationLevel >= 1`, or `analyzeBuiltins` never runs and there is not
   even a fastcall (`:5253-5256`).
2. `optimizationLevel >= 2` **and** no `AstExprGlobal` named `getfenv` or
   `setfenv` anywhere in the module. `FenvVisitor` (`Compiler.cpp:4850-4870`)
   sets the flags on any global **read** of those names — the global need not
   exist — and **the check is module-wide, not per-function**. The comment at
   `:5231` explains the bar: "we can't de-optimize folding at runtime".
3. The call is not a method call (`node->self` false, `Builtins.cpp:446`).
4. The callee resolves via `getBuiltin` (`Builtins.cpp:18-68`): a global in
   `Global::Default` state, an index `G.name` where `G` is a Default global, or a
   local initialized once from such a global (`local V = Vector3` and
   `local Vector3 = Vector3 or polyfill` are both handled, `:26-49`).
5. `vectorCtor` non-null and the name shape matches `vectorLib`.
6. Not listed in `disabledBuiltins`. Note this disables by **builtin id**, so
   disabling `vector.create` also disables your `vectorCtor` alias — both map to
   `LBF_VECTOR`.
7. Every argument itself folds to a non-Unknown, non-Table constant
   (`ConstantFolding.cpp:857-871`).
8. `foldBuiltin` arity: `count >= 2` with `args[0]` and `args[1]` both
   `Type_Number`; 2, 3 or 4 args accepted, missing components filled with 0
   (`BuiltinFolding.cpp:598-620`). **A 1-argument or 0-argument ctor call never
   folds.**

Condition (2) is U-30's second half: **a single mention of `getfenv` or `setfenv`
anywhere in a module kills all builtin constant folding for that module**,
including `Vector3.new`. The common feature-detection idiom `if getfenv then …
end` in ported code compiles to a nil read **and** silently costs the whole file
its folding. Worth a `luaug check` lint and a line in the migration guide.

`Global::Default` means: never assigned anywhere in the module (else
`Global::Written`), not `_G`, and not in `mutableGlobals` (else
`Global::Mutable`) — `ValueTracking.h:19-24`. Note `assignMutable`
(`ValueTracking.cpp:150-159`) marks `_G` `Mutable` **before it even looks at the
option array**, so freezing `_G` at runtime does not buy back the import
optimization. (`docs/architecture.md:534-535`'s parenthetical is accurate about
what LuauG passes and about the runtime table; this is just a note that the
compiler is unconditional here.)

`userdataTypes` registers each name that **actually appears in the source** via
`bytecode.addUserdataType`, assigning a bytecode type in
`[LBC_TYPE_TAGGED_USERDATA_BASE, LBC_TYPE_TAGGED_USERDATA_END]`; exceeding the
range raises "Exceeded userdata type limit in the compilation options"
(`Compiler.cpp:5288`). **Names absent from the source are skipped, so the index
assignment is source-dependent** — do not assume a stable mapping.

**The C mirror `lua_CompileOptions` (`luacode.h:23-71`) has no default member
initializers.** The `// default=1` comments describe what the C++ struct would
give you. `luau_compile` (`luacode.h:74`, impl `Compiler/src/lcode.cpp:8-29`)
default-constructs a `Luau::CompileOptions`, then — if `options` is non-null —
`static_assert`s the sizes match and **memcpys the entire C struct over it**,
overwriting all C++ defaults. So a zero-initialized `lua_CompileOptions` compiles
at `optimizationLevel 0` and `debugLevel 0`, silently losing folding, fastcalls
and line info. The two structs are kept in sync only by a size `static_assert`
(`lcode.cpp:16`), which will not catch a field **reordering**. Any future C-ABI
path — a plugin boundary, a shipping-client shim — must set all fields
explicitly. `luau_compile` returns a `malloc`'d blob the caller frees with
`free()`; on failure the blob is the encoded error, which `luau_load` decodes.
`luacode.h:79-85` also exposes
`luau_set_compile_constant_{nil,boolean,number,integer64,vector,vectord,string}`
for `libraryMemberConstantCb`; the header notes that vector component `w` is
invisible to a `LUA_VECTOR_SIZE == 3` runtime but still affects compile-time
folding, and that string storage must outlive the `luau_compile` call.

### 4.5 Vectors

```c
#ifndef LUA_VECTOR_SIZE
#define LUA_VECTOR_SIZE 3   // must be 3 or 4
#endif
#ifndef LUA_VECTOR_DOUBLE
#define LUA_VECTOR_DOUBLE 0
#endif
#if LUA_VECTOR_DOUBLE == 1
#define LUA_VECTOR_TYPE double
#else
#define LUA_VECTOR_TYPE float
#endif
#define LUA_EXTRA_SIZE (LUA_VECTOR_SIZE - 2)
```
`VM/include/luaconf.h:136-150`. Both default to LuauG's chosen values already,
and LuauG additionally forces them repo-wide at `CMakeLists.txt:48`.

**These are ABI-defining.** `LUA_EXTRA_SIZE` changes `sizeof(TValue)`, and
`LUA_VECTOR_DOUBLE` **moves `LUA_TVECTOR`'s ordinal within `enum lua_Type`**
(`lua.h:80-97`), because a double vector is a GC object rather than an inline
value. `engine/app/src/script_host.cpp:20-21` already static_asserts both.

Representation at 3-wide f32 (`lobject.h:140-151`):

```c
#define setvvalue(L, obj, x, y, z, w) { TValue* i_o = (obj); float* i_v = i_o->value.v; \
    i_v[0] = float(x); i_v[1] = float(y); i_v[2] = float(z); \
    condvector4(i_v[3] = float(w), (void)(w)); i_o->tt = LUA_TVECTOR; }
```

— note `(void)(w)`: the fourth component is accepted and discarded. The
`LUA_VECTOR_DOUBLE == 1` branch (`:132-139`) instead heap-allocates a `LuauVector`
GC object (`lobject.h:333-339`), which is why doubles cost an allocation each.

| Signature | Where | Notes |
|---|---|---|
| `void lua_pushvector(lua_State* L, LUA_VECTOR_TYPE x, y, z)` | `lua.h:199` (impl `lapi.cpp:730-739`) | Declared inside `#if LUA_VECTOR_SIZE == 4 / #else`; the 3-argument form is ours. Calls `luaC_checkGC` + `luaC_threadbarrier` **only** when `LUA_VECTOR_DOUBLE == 1`, so in our f32 configuration it is allocation-free and GC-free. |
| `const LUA_VECTOR_TYPE* lua_tovector(lua_State* L, int idx)` | `lua.h:169` (impl `lapi.cpp:566-572`) | NULL for a non-vector. `vvalue` is `(o)->value.v` (`lobject.h:88`), a `float[2]` in the `Value` union whose **third component lives in the adjacent `int extra[LUA_EXTRA_SIZE]` field** of the `TValue` (`lobject.h:49`). Only indices 0..2 are valid — index 3 reads `tt`. The pointer is into the Luau stack and dies on any reallocation or GC-triggering call. |
| `#define lua_isvector(L, n) (lua_type(L,(n)) == LUA_TVECTOR)` | `lua.h:506` | |

**Fastcall leniency.** `luauF_vector` (`lbuiltins.cpp:1060-1086`) accepts
`nparams >= 2` with numeric `arg0`/`arg1`; `z` defaults to 0 and is read only if
`nparams >= 3`; under `LUA_VECTOR_SIZE == 3` the 4-argument branch is compiled
out, so **a fourth argument is silently ignored** rather than rejected. And the
fastcall path only executes when `cl->env->safeenv` is true
(`lvmexecute.cpp:3056`) — outside a sandboxed environment the real global runs
instead. So `Vector3.new(1,2,3,4)` succeeds silently inside the sandbox and hits
the host's argument checking outside it: **environment-dependent behaviour for
the same source.** Either make the host `Vector3.new` equally lenient about extra
arguments, or accept that this cannot be diagnosed at runtime and lint it at
`luaug check` time.

Folded vector constants also fold component reads: `.x/.X`, `.y/.Y`, `.z/.Z`
become numbers (`ConstantFolding.cpp:894-935`), but `.w` is deliberately **not**
folded because the compiler cannot know whether the runtime is 3- or 4-wide
(comment at `:912-913`).

**`vectorPrecision` and `LUA_VECTOR_DOUBLE` are two independent knobs that must
be kept equal, and a mismatch fails quietly.** `vectorPrecision` only selects
`Constant::Type_Vectord` during folding (`BuiltinFolding.cpp:601-609`);
serialization then narrows it back to four floats unless
`FFlag::LuauCompileEmitVectorDouble` is set (`BytecodeBuilder.cpp:892-909`); and
the loader casts whatever it reads to `LUA_VECTOR_TYPE` regardless
(`lvmload.cpp:493-513`). LuauG pins both to the f32 side, so nothing is wrong
today — but the coupling is not enforced by any assert, and the static_asserts in
`script_host.cpp` cover only the runtime half.

### 4.6 `luau_load` and bytecode versions

```c
int luau_load(lua_State* L, const char* chunkname, const char* data, size_t size, int env);
```
`lua.h:249`, impl `lvmload.cpp:795-841`.

**It returns literal 0 or 1, not a `lua_Status`** — success at `:788-792`,
failure (with an error string pushed) at `:298, 306, 322, 837`. Since
`LUA_YIELD == 1`, a `switch` on `lua_Status` or a check for `LUA_ERRSYNTAX`
misreads failures. The existing `!= LUA_OK` comparison in
`engine/app/src/script_host.cpp:147` is correct; keep that shape.

`env` is 0 for the current environment (`L->gt`) or a stack index of a table
otherwise (`:326-327`). A blob whose first byte is 0 carries the compiler's
encoded error message as its remainder and is reported directly (`:293-299`). It
calls `luaC_checkGC` first and **pauses the GC for the whole deserialization**
(`:798-801`). It runs under `luaD_rawrunprotected`; only `LUA_ERRMEM` can escape,
and it is converted to a pushed `LUA_MEMERRMSG` string (`:829-838`).

Bytecode version constants (`Common/include/Luau/Bytecode.h:514-521`):

| Constant | Value |
|---|---|
| `LBC_VERSION_MIN` | 3 |
| `LBC_VERSION_MAX` | 13 |
| `LBC_VERSION_TARGET` | 9 |
| `LBC_VERSION_CLASSES` | 100 |
| `LBC_TYPE_VERSION_MIN` | 1 |
| `LBC_TYPE_VERSION_MAX` | 3 |
| `LBC_TYPE_VERSION_TARGET` | 3 |

Header comment at `:513`: "runtime supports [MIN, MAX], compiler emits TARGET by
default **but may emit a higher version when flags are enabled**". The load check
at `lvmload.cpp:301-307` rejects anything outside [3,13] except the sentinel 100;
the type-version check at `:311-323` only runs for bytecode version >= 4.
Versions 10–13 and 100 are documented Experimental (`Bytecode.h:52-59`).

**That "higher version when flags are enabled" is U-49:**

```cpp
uint8_t BytecodeBuilder::getVersion() {
    if (FFlag::DebugLuauUserDefinedClasses)  return LBC_VERSION_CLASSES;  // 100
    if (FFlag::LuauCompileEmitVectorDouble)  return 13;
    if (FFlag::LuauBytecodeCostModel)        return 12;
    if (FFlag::LuauEmitCallFeedback)         return 11;
    return LBC_VERSION_TARGET;
}
```
`Bytecode/src/BytecodeBuilder.cpp:1474-1488`. `getTypeEncodingVersion`
(`:1490-1493`) unconditionally returns `LBC_TYPE_VERSION_TARGET`.

FastFlags are process-global mutable state (`Luau::FValue`,
`Common/include/Luau/Common.h:79-102`), and several compiler behaviours read them
at compile time (`FFlag::LuauIntegerFastcalls` in `Builtins.cpp:10`,
`FFlag::LuauOptimizeExportTable` in `Compiler.cpp:5217`). **The effective FastFlag
set belongs in the bytecode cache key and in the provenance header**, alongside
the source hash and the Luau version. `LBC_VERSION_TARGET` alone is not
sufficient.

`LBC_CONSTANT_VECTOR` (`Bytecode.h:530`) always serializes **four floats**
(`BytecodeBuilder.cpp:888-893`) and the loader always reads four, discarding `w`
in 3-wide mode (`lvmload.cpp:493-502`). `LBC_CONSTANT_VECTORD` writes four
doubles but only when `FFlag::LuauCompileEmitVectorDouble` is on — otherwise a
`Type_Vectord` constant is silently narrowed to floats. Vector constants require
bytecode version >= 5; `VECTORD` requires 13.

Note that `Bytecode.h` lives in `Common/include` and is **not** exported by
`Luau.VM`'s usage requirements; `cmake/luaug_luau.cmake:42-43` already adds it
explicitly for the ADR 0031 provenance header.

---

## 5. FastFlags are the other half of the pin

`LUAU_FASTFLAGVARIABLE(x)` expands to `FValue<bool> x(#x, false, false)` —
**default false** (`Common/include/Luau/Common.h:139-143`). `FValue` is
process-global mutable state (`:79-102`). The CLI does *not* run on those
defaults: `setLuauFlagsDefault` (`CLI/include/Luau/Flags.h:4`, impl
`CLI/src/Flags.cpp:38-44`) walks `Luau::FValue<bool>::list` and sets **every**
flag whose name starts with `Luau` to true, except four analysis flags in
`ExperimentalFlags.h`. Every CLI entry point calls it (`ReplEntry.cpp:7`,
`Analyze.cpp:398`, `Compile.cpp:491`, `Bytecode.cpp:273`).

**An embedder that does not call it gets the compiled defaults.** So `luau` /
`luau-analyze` behaviour and embedded-runtime behaviour diverge — on require
semantics most visibly. Setting one from C++ is `LUAU_FASTFLAG(X)` then
`FFlag::X.value = true`.

Flags that change semantics in the areas this document covers:

| Flag | Default | Effect | Where |
|---|---|---|---|
| `LuauDirectFieldGet` | false | The whole direct-field-get mechanism. Registration **silently no-ops**; the setters `LUAU_ASSERT` | `lvmexecute.cpp:21`, `lapi.cpp:2062-2063` |
| `LuauGcTraceUdata` | false | `lua_setuserdatamark`, `lua_setembeddergc`, `lua_weakref`/`weakunref`/`getweakref`. These **`LUAU_ASSERT`** rather than no-op, and the backing `weakregistry` is only created under the flag | `lgc.cpp:20`, `lapi.cpp:1843-1878`, `lstate.cpp:78-82` |
| `LuauGcMarkUdataAccess` (DFFlag) | false | Re-marking of direct-access metamethod snapshots in the atomic phase | `lgc.cpp:22`, `:1011-1012` |
| `LuauCyclicRequireShortCircuit` | false | Cyclic require entirely | `Require/src/RequireImpl.cpp:13` |
| `LuauExportValueSyntax` | false | The `export` keyword, which cyclic require depends on | `Ast/src/Parser.cpp:25` |
| `LuauSelfIsSelfAndAlwaysSelf` (DFFlag) | false | Whether `@self` can be hijacked by a user alias | `Require/src/RequireNavigator.cpp:16` |
| `LuauRbsConfigAliasResolution` | false | A refactor of `extractConfig`; both branches behave equivalently at this pin | `Config/src/LuauConfig.cpp:23` |
| `LuauXpcallFixMessageYieldPath` | false | `nCcalls` restoration in `resume_handle`/`resume_finish` | `ldo.cpp:21`, `:580-605`, `:676-686` |
| `LuauYieldIter2` | false | The `LUA_CALLINFO_OPYIELD` / `luau_finishop` path in `resume_continue` | `lvmexecute.cpp:27`, `ldo.cpp:436-437` |
| `LuauIntegerLibrary` | false | Whether the `integer` library is opened at all | `lintlib.cpp:15`, `linit.cpp:42-48` |
| `LuauEmitCallFeedback` / `LuauBytecodeCostModel` / `LuauCompileEmitVectorDouble` / `DebugLuauUserDefinedClasses` | false | Raise the emitted bytecode version to 11 / 12 / 13 / 100 | `BytecodeBuilder.cpp:1474-1488` |
| `LuauCIProto`, `LuauBackedgeHeapCheck` | false | Whether GC steps run on loop back edges | `lvmexecute.cpp` |

**Two conclusions (U-33).** First, wanting either `LuauDirectFieldGet` or
`LuauGcTraceUdata` is a deliberate decision to depend on an unshipped Luau
feature — R5/ADR territory, and it must be recorded. Second, **the engine must
pin its flag set explicitly and treat it as part of the version pin**, or
identical source behaves differently between builds. That is a determinism risk
under R10 independent of any individual flag's merits.

**The vendored tree is not the plain upstream 0.734 VM surface the design
documents describe (U-25).** `third_party/manifest.json` records
`"version": "0.734"` and commit `3fc82b1071ab387531175869afc4fb528464afa4`, and
at that pin `VM/include/lua.h` exposes:

- `LUA_TCLASS` / `LUA_TOBJECT` and `LUA_TINTEGER` type tags (`lua.h:75-97`);
- a `class` library (`lualib.h:142-143`);
- `lua_weakref` / `lua_setembeddergc` embedder-GC APIs (`lua.h:368-413`);
- the userdata direct-field API (`lua.h:438-463`);
- `lua_callyieldable` / `lua_pcallyieldable` (`lua.h:254-256`);
- `LOP_CALLFB` / `LOP_NEWCLASS` / `LOP_CMPPROTO` opcodes and class-runtime paths
  inside `LOP_GETTABLEKS` and `LOP_NAMECALL` gated on
  `FFlag::DebugLuauUserDefinedClassesRuntime`;
- an `Inliner/` tree.

None of this breaks anything — it is inert at default flag values. But it does
mean two things: `lua_Type` enum values differ from stock Lua/Luau ordering,
reinforcing the existing "never persist `lua_Type`/tag values" rule at
`docs/architecture.md:563`; and **published Luau documentation for 0.734 is not a
safe reference for this checkout**. Only the vendored headers are.

---

## 6. `api_check` is documentation, not validation

```c
#define api_check(l, e) LUAU_ASSERT(e)
```
`VM/src/lcommon.h:14`. `LUAU_ASSERT` expands to `(void)sizeof(!!(expr))` unless
`NDEBUG` is undefined or `LUAU_ENABLE_ASSERT` is defined
(`Common/include/Luau/Common.h:68-73`).

**Every precondition quoted anywhere in this document is silent UB in a shipping
build.** Concretely: every tag bound check
(`lua_newuserdatatagged`, `lua_setuserdatatag`, `lua_setuserdatadtor`,
`lua_setuserdatametatable`, `lua_pushlightuserdatatagged`,
`lua_registeruserdatadirect*`); `lua_setmemcat` and `lua_totalbytes` category
bounds; `lua_resetthread`'s `!isactive`; `lua_pcall`'s `status == 0`;
`lua_xmove`'s same-VM check; `lua_ref`'s `idx != LUA_REGISTRYINDEX`;
`lua_unref`'s non-nil slot.

Two consequences:

1. **The binding layer must range-check tags and categories itself.** In release,
   an out-of-range tag writes past `udatagc[]`, `udatamt[]` or
   `lightuserdataname[]`, and `lua_setmemcat(L, 300)` silently becomes 44. The
   "reassignment is not supported" guard on `lua_setuserdatametatable` likewise
   will not stop a double registration in release — it will overwrite and leak
   the previous metatable's root.
2. **The engine's own debug builds should define `LUAU_ENABLE_ASSERT`** and
   install an assert handler, so these fire during development rather than in a
   shipped binary.

---

## UNCONFIRMED / COULD NOT VERIFY

Everything above is a direct reading of vendored source. These are the items this
pass could **not** settle from source alone.

1. **The "~3x faster" figure** for `lua_newuserdatataggedwithmetatable`
   (U-24). No benchmark in `third_party/luau/bench` measures it. The mechanical
   saving is one API round trip, one stack push/pop and one write barrier.
   Measure on the engine's own allocation pattern.
2. **Whether one tag per class or one shared tag is faster in practice** (§1.3).
   Both are supported; the trade is per-class direct dispatch versus a ~126-tag
   budget. Needs a microbenchmark on real Instance shapes before M2 binding work.
3. **Whether `LuauDirectFieldGet` and `LuauGcTraceUdata` are safe to enable.**
   Both are default-false FastFlags on features upstream has not shipped on. This
   is a stability judgement, not a source question — it needs an ADR.
4. **The performance delta between `__namecall` + atom switch and
   `LOP_NAMECALLUDATA`.** The instruction-count difference is visible in the
   source (§1.5) but the real-world margin is not.
5. **Whether the conformance suite's `DirectSlot` pattern scales** past two tags
   to a full Instance class hierarchy with a single global name→slot enum.
   `tests/Conformance.test.cpp:946-1001` is a two-tag proof, not a scaling one.
6. **Behaviour of the class/integer/`LOP_CALLFB` machinery** in this tree beyond
   "inert at default flags". This pass read only far enough to establish that the
   ordinals move (§5); nothing else was exercised.
7. **Whether any patch has been applied to the vendored tree.**
   `third_party/manifest.json` lists `"patches": []` for luau, and the surfaces
   observed are consistent with an upstream checkout newer than the 0.734 tag
   rather than a local modification — but this pass did not diff against upstream
   to prove it.
