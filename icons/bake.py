"""Rebuild the default Orbit theme; keeps the historical bake entry point."""

import runpy
from pathlib import Path

if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).resolve().parents[1] / "tools/repo/draw_icons.py"), run_name="__main__")
