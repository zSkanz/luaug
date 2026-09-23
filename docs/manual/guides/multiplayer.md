# Multiplayer

One project runs alone, hosts a match, serves one with no window, or joins one.
The command line chooses the role, and the script is the same in all four.

```
run.bat                        solo: nobody else, and this machine decides
run.bat --host                 a window, and the authority other machines join
run.bat --serve                the authority with no window: a dedicated server
run.bat --join=127.0.0.1       a window showing a world another machine decides
```

A host and a server listen on port 7777; `--join=address:port` names another.

## The one question a script asks

**Does this machine decide the world?** `NetworkService.Authority` answers it.
It is true solo too, so a game written for a match runs alone with no
configuration branch:

```luau
--!strict
local NetworkService = game:GetService("NetworkService")

if NetworkService.Authority then
    -- build the world, run the rules, move what players asked to move
else
    -- a replica: point a camera at what arrives, send what the player did
end
```

A replica builds nothing it expects the authority to send. Everything under
`Workspace` that replicates arrives from the authority: parts, models,
folders, their names, colours and motion, the `Lighting` service's time of day
and fog, decals, particle emitters and `RemoteEvent`s. `Terrain` does not
replicate. A world's ground arrives with the world, from its scene.

## Players

`NetworkService:GetPlayers()` lists everyone taking part, and `PlayerAdded` and
`PlayerRemoving` say when that changes. `NetworkService.LocalPlayer` is the
player at this machine, and a dedicated server has none. `Player.UserId` is the
same number on every machine.

**What a player did reaches the authority as intent**: the values of their input
actions, read with `player:GetIntent("Move")` exactly as `GetState` reads them
on the machine where the key was pressed. The authority decides what the key
did. A client that could say "I moved here" or "I hit" would be a client that
always wins.

## A player's own character

Set `player.Character` to the part that is them. Two things follow from it:

- **Their machine moves it at once.** A replica runs the same movement code on
  its own character from its own keys, and the authority's snapshots correct it
  rather than overwrite it. Pressing a key does not wait a round trip.
- **Their machine is sent what is near it.** A replica is sent the parts within
  `StreamingService.LoadRadius` of its character, and everything else in the
  world stays on the authority. A part a script there still holds becomes a
  husk when it leaves, parented to nil, and `StreamingService.InstanceStreamedOut`
  fires for it.

Everybody else's parts are drawn between the last two snapshots, a few ticks
behind the authority, so they glide instead of stepping.

## Messages: `RemoteEvent`

Some things a game says are not state and are not a key held down: "I bought
the sword", "the round starts", "you won". A `RemoteEvent` carries them.

Create it on the authority, under `Workspace`, and find it on a replica by
name:

```luau
local honk: RemoteEvent
if NetworkService.Authority then
    honk = Instance.new("RemoteEvent")
    honk.Name = "Honk"
    honk.Parent = workspace
else
    honk = workspace:WaitForChild("Honk") :: RemoteEvent
end
```

| Call | From | Fires |
|---|---|---|
| `honk:FireServer(...)` | any machine | `honk.ServerReceived(player, ...)` on the authority |
| `honk:FireClient(player, ...)` | the authority | `honk.ClientReceived(...)` on that player's machine |
| `honk:FireAllClients(...)` | the authority | `honk.ClientReceived(...)` everywhere a player is |

**The authority learns who sent a message from the connection**, and it arrives
first in `ServerReceived`. Nothing a message says can make it someone else's.
Solo and hosting, this machine is its own server and its own client, so every
call is delivered here as it would be across a network.

A message carries values:

- nil, booleans, numbers, strings and vectors;
- instances, each arriving as the receiver's own copy, or nil where the
  receiver does not have one;
- tables of all of those, eight deep.

A function, a thread, a table that contains itself, or more than 64 KiB is
refused at the call that tried to send it. Messages are reliable and arrive in
order, at the start of the receiver's next tick. An authority takes at most
256 from one player a tick.

## What is not here

A call that waits for an answer, unreliable messages, a replicated container
other than `Workspace`, rollback, and lag compensation for hits. `examples/15-multiplayer`
is the whole of it in one file: racers driven by intent, predicted by their own
machine, and a horn that is a `RemoteEvent`.

## Where to look next

- [Talking to a backend](manual:guides/backend) — accounts, inventories and
  anything that outlives a match
- [Input actions](manual:input/actions)
- [Streaming a large world](manual:assets/streaming)
