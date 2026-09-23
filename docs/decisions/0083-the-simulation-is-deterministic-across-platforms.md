# 0083 — The simulation is deterministic across platforms (level C)

- Status: accepted
- Date: 2026-09-23
- Decided by: the owner ("Determinism: fix it too", the mandate of 2026-09-23,
  `docs/briefs/mandate-2026-09-23.md`), with the design by the agent.
- Amends: [0025](0025-determinism-guarantee.md) (the guarantee is now level C) and
  [0074](0074-jolt-runs-cross-platform-deterministic.md) (the two items it left
  are done)
- Patches: `third_party/luau`, one patch (`0001-deterministic-pow.patch`)

## Context

ADR 0074 turned on Jolt's cross-platform determinism. It measured what still
parted Windows from Linux, and wrote down the two remaining items:

- the engine's own transcendentals (`CFrame.fromEuler`, the pose rotations,
  easing);
- Luau's `math` library.

Both call each platform's C runtime, and MSVC's runtime and glibc round `sin`
and the rest differently in the last bit. A checkpoint every 150 ticks gives
one bit plenty of time to reach the world hash.

Working the items out found a third. A compiler that fuses `a * b + c` into
one fused multiply-add rounds once where the source rounds twice. Clang does
that by default wherever the target has the instruction, and every ARM Mac
has it. So a macOS build would part from both others even with every
transcendental fixed, in any float expression anywhere on the simulation's
path.

## Decision

**The guarantee is level C: the same seed and the same operations give the
same world hash on every platform and compiler.** Four things make it hold.

1. **`core::dmath`** is the engine's own `sin`, `cos`, `tan`, `asin`, `acos`,
   `atan`, `atan2`, `exp`, `log`, `log2`, `log10`, `pow`, `sinh`, `cosh` and
   `tanh`.
   - Each is argument reduction followed by a fixed polynomial, in plain IEEE
     double arithmetic (`+ - * /` and `sqrt`, all correctly rounded by the
     standard), with `floor`, `fmod`, `frexp` and `ldexp`, which are exact.
   - The reductions are the classic ones: pi/2 in three 33-bit pieces, ln 2
     split in two.
   - `pow` carries `y * log(x)` in double-double, so its error is the final
     rounding's rather than `y` times the logarithm's.
   - A small integer power is taken by squaring, and `^ 0.5` is `sqrt`, so
     `2 ^ 10` is 1024 and `10 ^ -2` is 0.01 exactly.
   - Measured against the runtimes: 1 ulp for `sin`, `cos`, `exp`, `log` and
     `pow`, and 3 at worst for `tan` and `tanh`.
   - It is its own static library (`luaug_dmath`), linked by `core` and by
     Luau's VM and compiler.
2. **The simulation's code calls it.** That means `core/math.cpp` (Euler
   angles, axis-angle, slerp), `easing.cpp`, `random.cpp` and the script
   datatypes. Rendering, the editor and audio keep `std::`: nothing they
   compute is hashed.
3. **Luau's `math` goes through it.**
   - `installDeterministicMath` puts the `dmath` functions in the `math` table
     before the sandbox seals it, and `vector.angle` too.
   - The compiler is given those names as `disabledBuiltins`, so no fastcall
     reaches the C runtime past them.
   - `^` does not go through a library at all: the VM's `LOP_POW` and
     `LOP_POWK`, the fastcall and the compiler's constant folding all called
     `pow` directly. One patch sends all four to `luaug_dpow`. Nothing in
     `third_party/` is edited by hand (R13).
   - Native code generation is not enabled by this engine; its own table of
     C-runtime functions would need the same treatment the day it is.
4. **`-ffp-contract=off`** for Clang and GCC, on every target, vendored code
   included. MSVC contracts only under `/fp:contract` or `/fp:fast`, and this
   build passes neither. `dmath.cpp` also says so in a pragma, so it does not
   depend on the flag.

**The determinism traces are one file per scenario**, `trace.txt`, and every
tier gates against it. `character`, `ragdoll` and `terrain` stop being
`sameBuildOnly` and carry traces.

## Evidence

- All five determinism scenarios produced byte-identical traces and final
  hashes under MSVC on Windows and Clang with glibc on Linux.
- Before this, `churn` had parted at tick 500.
- `dmath_tests.cpp` holds a table of exact bit patterns written on Windows.
  Every tier must reproduce every bit, which is the most direct test of the
  claim there is.
- **macOS proved it on the first run**: CI run 35922428910, Apple Clang on
  Apple silicon, passed `determinism` against the same `trace.txt` files and
  `core`'s golden bit table. That is three compilers, two C runtimes and two
  instruction sets producing one world hash.

## Consequences

- A tier that parts from a trace is a defect to find, not a platform
  difference to record. `tests/determinism/README.md` says so, and the
  architecture's §9 states the guarantee as level C.
- **What level C buys:**
  - a replay recorded on one machine plays back on another;
  - lockstep or rollback netcode becomes possible, since both need every peer
    to compute the same world;
  - server authority (ADR 0069) needs neither, but gains a replica that
    predicts exactly what the server will say.
- **What it costs:**
  - `dmath` runs a few times slower than a runtime's vectorised `sin`. On the
    simulation's path that is a handful of calls per tick per rotated thing,
    and below noise in every bench.
  - Turning off contraction costs ARM a fused multiply-add it never had on
    x86.
- `third_party/manifest.json`'s Luau row lists the patch, and
  `tools/repo/vendor.luau` reapplies it on every vendor.
