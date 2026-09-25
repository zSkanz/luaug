"""Captures and measures ADR 0096's effects in one scene.

    python tools/repo/look_captures.py capture <variant>... [--host PATH] [--out DIR]
    python tools/repo/look_captures.py measure <variant>... [--host PATH] [--frames N]

`tests/look` is the scene: a valley at late afternoon, looking into the sun,
with a line in its script -- `local Variant = "none"` -- that says which effects
are in the world. This copies the project once per variant with that line
rewritten, and either renders one frame of it to a PNG (capture) or runs it
headless at 1920x1080 and reads the median frame time back (measure).

**Every capture is the same camera at the same `ClockTime`**, which is the
whole point: a before and an after differ in the variant and nothing else. The
host renders headless on a synthetic clock, so frame N is the same picture on
every machine at every speed.

**A cost is a difference of medians against `none`**, measured in the same run
of this script, alternating, so both see the same machine. It is the method
`docs/perf-baselines.md` has used for every pass since M7.5: the RHI has no
timestamp query, and its noise floor is about 0.08 ms.
"""

import argparse
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / "tests" / "look"
VARIANT_LINE = re.compile(r'^local Variant = "[^"]*"$', re.MULTILINE)


def default_host() -> Path:
    build = os.environ.get("LUAUG_BUILD_ROOT", str(Path.home() / "AppData/Local/LuauG/build"))
    return Path(build) / "win-msvc-dev" / "engine" / "app" / "luaug-host.exe"


def project_for(variant: str, workdir: Path) -> Path:
    target = workdir / variant
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(SCENE, target, ignore=shutil.ignore_patterns(".luaug"))
    script = target / "src" / "scripts" / "init.luau"
    text = script.read_text(encoding="utf-8")
    text, count = VARIANT_LINE.subn(f'local Variant = "{variant}"', text)
    if count != 1:
        sys.exit("look_captures: the scene's `local Variant = ...` line is missing")
    script.write_text(text, encoding="utf-8", newline="\n")
    return target


def run(host: Path, project: Path, arguments: list[str]) -> str:
    result = subprocess.run(
        [str(host), str(project), "--headless", "--exit", *arguments],
        cwd=project,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    output = result.stdout + result.stderr
    if result.returncode != 0:
        sys.exit(f"look_captures: {project.name} exited {result.returncode}\n{output[-2000:]}")
    return output


def capture(host: Path, variants: list[str], out: Path, frames: int, width: int, height: int) -> None:
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="luaug-look-") as scratch:
        for variant in variants:
            project = project_for(variant, Path(scratch))
            png = out / f"{variant}.png"
            run(host, project, [f"--frames={frames}", f"--width={width}", f"--height={height}", f"--screenshot={png}"])
            print(f"look_captures: wrote {png.relative_to(ROOT) if png.is_relative_to(ROOT) else png}")


MEDIAN = re.compile(r"median[^0-9]*([0-9.]+) ?ms")


def measure(host: Path, variants: list[str], frames: int, rounds: int) -> None:
    order = ["none"] + [v for v in variants if v != "none"]
    samples: dict[str, list[float]] = {v: [] for v in order}
    with tempfile.TemporaryDirectory(prefix="luaug-look-") as scratch:
        projects = {v: project_for(v, Path(scratch)) for v in order}
        for _ in range(rounds):
            for variant in order:
                output = run(host, projects[variant],
                             [f"--frames={frames}", "--width=1920", "--height=1080", "--frame-stats"])
                found = MEDIAN.search(output)
                if found is None:
                    sys.exit(f"look_captures: no median frame time in {variant}'s output\n{output[-1500:]}")
                samples[variant].append(float(found.group(1)))
    base = statistics.median(samples["none"])
    print(f"{'variant':<20} {'median ms':>10} {'cost ms':>9}   rounds")
    for variant in order:
        value = statistics.median(samples[variant])
        cost = value - base
        runs = ", ".join(f"{s:.3f}" for s in samples[variant])
        print(f"{variant:<20} {value:>10.3f} {cost:>+9.3f}   {runs}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=["capture", "measure"])
    parser.add_argument("variants", nargs="+")
    parser.add_argument("--host", type=Path, default=default_host())
    parser.add_argument("--out", type=Path, default=ROOT / "docs" / "briefs" / "atmosphere-post")
    parser.add_argument("--frames", type=int, default=0)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    arguments = parser.parse_args()
    if not arguments.host.exists():
        sys.exit(f"look_captures: no host at {arguments.host}")
    if arguments.mode == "capture":
        capture(arguments.host, arguments.variants, arguments.out, arguments.frames or 60, arguments.width,
                arguments.height)
    else:
        measure(arguments.host, arguments.variants, arguments.frames or 600, arguments.rounds)


if __name__ == "__main__":
    main()
