# 15-multiplayer

Two copies of one game, one of them deciding (N1, ADR 0069 and ADR 0070).

```
run.bat --host                  # a window, and the authority
run.bat --join=127.0.0.1        # a second window, showing the host's world
run.bat --serve                 # the authority with no window
run.bat                         # solo: the same world, nobody networked
```

`--host` and `--serve` take a port (`--host=7777`, the default); `--join`
takes an address and an optional port (`--join=192.168.0.10:7777`).

## What it shows

- **One project, every posture.** The script asks `NetworkService.Authority` —
  a gameplay question, "do I decide this world?" — and builds and animates the
  world only when the answer is yes. Solo answers yes too, so running it with no
  flag at all is the same game a host plays.
- **A replica builds nothing.** The ground, the ring of crates and the turning
  bar arrive from the authority: names, colours, sizes and motion, as a diff
  against the last state the replica acknowledged. The crates fall on the
  authority; the replica's copies are driven, never simulated, so they land
  where the authority's did.
- **No script opens a port.** The posture is chosen on the command line, before
  any script exists, and `NetworkService` only reports it.

## What it does not show yet

Players: a replica sees the world and cannot act in it. Intent — what a player
did, sent to the authority — is the next piece of N1.

The transport is ENet, which is **unencrypted and unauthenticated**: a LAN or an
otherwise trusted link.
