# 0079 — A client asks through a `RemoteFunction`, and the server never waits on a client

- Status: accepted
- Date: 2026-09-23
- Milestone: N2 (phase 4), the first thing ADR 0077 left for later by name
- Decided by: the agent, under the owner's standing instruction of 2026-08-26
  to take the repository's decisions on their behalf, and the owner's word the
  same day to build what the engine still lacked.
- Builds on: ADR 0077 (game messages through a `RemoteEvent`)

## Context

ADR 0077 gave a game one-way messages and left "a call that waits for an
answer" for when a game needed one. Questions are ordinary game code: "how many
coins do I have", "may I open this door", "what does this shop sell". With only
events, every question is two events and a correlation table the game writes
by hand, and every game writes the same one.

Of the engines this one is measured against, only the one its API is familiar
from has a call that returns a value across the network. Unreal's, Unity's and
Godot's RPCs return nothing. That platform's remote function has two halves,
and they are not equally sound:

- A client asking the server is safe. The server is trusted to answer, and a
  server that never answers is the game's own bug.
- The server asking a client is the documented hazard. A client can fail to
  answer, on purpose or by disconnecting, and the server's script waits for
  ever. That platform's own documentation warns against it.

## Decision

1. **`RemoteFunction` is an instance, and it replicates**, like a
   `RemoteEvent`: created on the authority, found on a replica by name.
2. **A client asks with `InvokeServerAsync(...)`**, which yields until the
   answer arrives and returns it. The `Async` suffix is this API's rule for a
   call that parks its caller. It is not `InvokeServer`.
3. **The authority answers with `OnServerInvoke`**, a function a script
   assigns, called with the asking player first.
   - It runs in a thread of its own and may yield. Its answer goes when it
     finishes.
   - If it raises, the caller's `InvokeServerAsync` raises
     `net.err.remote_invoke_failed` with the handler's message.
   - If there is no handler, the caller raises `net.err.remote_no_handler`.
     The call does not wait for a handler to be assigned.
4. **There is no `InvokeClient`.** The server tells a client things with a
   `RemoteEvent`, and never waits on one.
5. **One script runs solo, hosting and networked.** On an authority with a
   player of its own, `InvokeServerAsync` asks itself, as that player, at the
   start of the next tick: the moment a replica's question would be answered.
   A dedicated server has no player to ask as, and refuses.
6. **The IDL gains callbacks.** A callback is a member a script assigns one
   function to and the engine calls. It is named `On*`, typed in the
   definitions as an optional function field, and listed on its own in the
   reference. It is not an event, because an event has any number of
   listeners and answers nothing.
7. **On the wire it is a `RemoteEvent` message with a number.** Protocol 8
   adds a call number and a flags byte to both remote messages.
   - A call number of zero is a message and names a `RemoteEvent`.
   - Any other number is a question to the authority, or with the reply flag
     an answer to a replica, and names a `RemoteFunction`.
   - The failed flag marks an answer that is the handler's error, as one
     string.
   - Each end drops a message that names the wrong kind of instance, a client
     that sends an answer, and an authority that sends a question.

   The values, limits and channel are 0077's.

## Consequences

- A question is one call. The correlation table every game wrote is gone.
- A handler that never returns holds its caller for ever, as a server bug
  should be visible. A caller whose connection drops before its answer
  arrives is still parked when the session ends.
- An answer names the `RemoteFunction` it came through. If the authority
  destroys that instance while a handler runs, the answer has no network id
  and is not sent.

## Evidence

- `tests/conformance/world/remote.spec.luau`, played solo:
  - an answer returns, with the asking player first in the handler;
  - the handler reads back, and nil removes it;
  - a handler that yields still answers;
  - a handler error, an answer that cannot travel, and a missing handler each
    raise at the caller.
- `engine/script/tests/service_tests.cpp`:
  - on a replica, two questions go out with distinct numbers, and the answers
    resume their callers, one with its value and one with its error;
  - a dedicated server refuses to ask.
- `engine/replication/tests/session_tests.cpp`: a question reaches the
  authority with its number and the connection's player. The answer reaches
  that replica with its flags. A question aimed at an event, a message aimed
  at a function, and a client's forged answer are all dropped.
