# 21-navigation

Navigation (ADR 0089): a walker that finds its own way through a maze.

```
run.bat
```

- **The maze is the ASCII map at the top of the script**, built from anchored
  parts. That is all walkable ground is: nothing is baked or placed for
  navigation.
- **`NavigationService:FindPath` answers the corners**; the script walks the
  `CharacterBody` to each in turn, one `Move` a tick, and draws a small marker
  at each.
- **Every arrival draws a new goal** from a seeded `Random`, so every run is
  the same walk.
- **G** moves the goal mid-walk and the path is asked for again. **B** drops a
  block just ahead of the walker; the next query sees it and goes round.
