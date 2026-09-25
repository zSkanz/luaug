# 0094 — Signal, Collector and Promise are the engine's own

- Status: accepted
- Date: 2026-09-24
- Relates to: [0015](0015-deferred-only-signals.md) (deferred-only signals, R8),
  [0034](0034-luau-casing-objects-modules-files.md) (naming),
  [0093](0093-the-script-editor-types-with-luau-analysis.md)

## Context

The owner, on one day:

> quero que tenhamos nativamente Signal, Collector e Promise — para signal já
> temos algo, mas confirme que ele é bom e se baseie no GoodSignal; para
> collector se baseie no Janitor, mas chame de Collector; para promise se
> baseie em Promise.

Every project written against this API ends up bringing its own copies of
these three: a signal class, a cleanup bag, and a promise library. Each copy is
a slightly different dialect. None of them is typed by the engine's
definitions, and none of them knows about the engine's deferred drain.
GoodSignal, Janitor and evaera's Promise are the de-facto references for all
three, and all three are MIT-licensed. Here they are design references only: no
line of any of them is copied.

## Decision

### `Signal`: audited, kept deferred, and one member added

`Signal` has been native since M2, in C++, and the audit against GoodSignal
found it sound. The fire snapshots the connection list, and each connection is
checked again at invocation, so a disconnect made by an earlier handler in the
same fire is honoured. Every handler runs in its own coroutine, so it may
yield, and an error in one reaches neither the others nor the firing script.
`Once`, `Wait`, `Destroy` and `Connection.Connected` are all there. Connection
order is guaranteed within one fire.

It keeps two deliberate differences from GoodSignal.

- **Deferred, not immediate** (ADR 0015, R8). GoodSignal calls its handlers
  inside `Fire`. Here a fire enqueues, and the handlers run at the next drain
  in the order the fires were raised. That is what makes a tick deterministic
  and a replay exact.
- **Oldest first.** GoodSignal prepends each connection to a linked list, so it
  calls the newest handler first. That is an accident of the data structure,
  not a contract, and it is not adopted.

GoodSignal has one member this engine lacked, and it is added:
`Signal:DisconnectAll()`, which disconnects every connection, leaves the signal
usable, and fires nothing.

### `Collector` and `Promise`: written in Luau, shipped as bytecode

Both are written in `--!strict` Luau under `runtime/builtins/`, and both are
installed as read-only globals before the sandbox closes. From a script they
are native: no `require`, and typed by the definitions, because the IDL
declares them as datatypes of the `script` module.

**Luau rather than C++**, because both are libraries of coroutines and
closures. A promise chained through C++ against the deferred scheduler is
where the bugs would hide. In Luau the semantics can be followed line by line
against the libraries they are modelled on.

**Compiled at build time.** The shipping profile carries no compiler ([ADR
0002](0002-luau-pinned-direct-embed.md)), so a builtin shipped as source could not run there. A small host tool,
`luaug_luauembed`, compiles each builtin with the engine's own compile
options. Those options move into one header,
`luaug/script/compile_options.h`, so the runtime compile and the build-time
compile cannot drift apart (`Vector3` as the native vector, and the
deterministic math builtins disabled). The resulting bytecode is embedded in
the script module. Every profile runs the same bytecode.

**Collector** follows Janitor's surface, with names under ADR 0034:
`Collector.new()`, `:Add(object, method?, key?)`, `:AddPromise`, `:Remove`,
`:RemoveNoClean`, `:Get`, `:Cleanup`, `:Destroy` and `:LinkToInstance`. Each
kind of object is cleaned the way it is ended:

- a function is called;
- a thread is cancelled;
- a `Connection` is disconnected;
- an `Instance`, a `Signal` or a table with a `Destroy` method is destroyed;
- a `Promise` is cancelled.

`method` names another cleanup method, or `true` calls the object itself.
**`:Cleanup` runs in reverse insertion order.** Janitor iterates a hash table,
which R10 forbids, and last-in-first-out is the order destructors run in: a
connection made after the instance it listens to is disconnected first.

**Promise** follows evaera's semantics. Its constructors keep their names,
camelCase as namespace functions: `Promise.new`, `defer`, `resolve`,
`reject`, `try`, `delay`, `all`, `allSettled`, `race`, `any`, `some`,
`fromEvent`, `retry`, `retryWithDelay`, `is` and `promisify`. Its methods
are PascalCase as members:

- chaining: `AndThen`, `Catch`, `Finally`, `Tap`, `AndThenCall`,
  `AndThenReturn`;
- waiting: `Await`, `AwaitStatus`, `ExpectAsync`, `Now`;
- control: `Cancel`, `GetStatus`, `Timeout`.

Two names differ from evaera's, and both follow house rules:

- **The status is `Enum.PromiseState`** rather than a table of strings on
  `Promise`. That is how a tween reports its state (`Enum.PlaybackState`),
  it is typed, and a namespace constant would have had to be the camelCase
  `Promise.status` (ADR 0034).
- **`expect` is `ExpectAsync`.** API §9 wants a method that parks to say so
  in its name. `Await` already says so, because it is the same word as
  `Wait`, and the checker's exemption for `Wait*` now covers `Await*` too.
  `expect` does not say so.

The executor runs at once, in its own coroutine, so it may yield. Resolving
with a promise adopts it. A handler that errors rejects the promise it
returns. Cancelling propagates down to every consumer and up to the parent
once all of the parent's consumers have cancelled. A rejection nothing
handled is reported with a warning after the drain. `Promise.delay` runs on
the simulation's clock (`task.delay`, R10), and it resolves with the seconds
it was asked for, not with wall-clock time.

## Consequences

- A project stops shipping its own copies, and the three are typed everywhere
  the definitions reach: the script editor (ADR 0093), `luaug check` and
  luau-lsp.
- The tree gains its first build-time Luau compile, together with the one
  header of compile options that the runtime and the tool share. A future
  builtin written in Luau costs only a file and a line in CMake.
- Conformance specs pin all three: `tests/conformance/collector/` and
  `tests/conformance/promise/`, and `DisconnectAll` in `signals/`.
