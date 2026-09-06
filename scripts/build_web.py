#!/usr/bin/env python3
"""Compile the SAME C++ engine/synth to a self-contained offline HTML simulator."""
import base64
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXPORTS = "be_init be_configure be_select be_abort be_tick be_press be_beat be_demo_mask be_value be_note be_cue be_sound be_sound_frames be_sample_rate".split()

def main() -> None:
    compiler = os.environ.get("CLANGXX") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("clang++ and wasm-ld are required to rebuild; or download the beat-echo-browser CI artifact.")
    wasm = ROOT / "web/beat_echo.wasm"
    command = [compiler, "--target=wasm32", "-std=c++17", "-O2", "-ffreestanding", "-fno-builtin",
               "-fno-exceptions", "-fno-rtti", "-nostdlib", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "core/include"),
               *(str(ROOT / p) for p in ["core/src/engine.cpp", "core/src/synth.cpp", "web/wasm.cpp", "web/runtime.cpp"]),
               "-Wl,--no-entry", "-Wl,--export-memory", "-Wl,--initial-memory=262144", "-Wl,-z,stack-size=65536",
               *("-Wl,--export=" + symbol for symbol in EXPORTS), "-o", str(wasm)]
    subprocess.run(command, check=True)
    template = (ROOT / "web/template.html").read_text(encoding="utf-8")
    for key, content in {
        "CSS": (ROOT / "web/style.css").read_text(encoding="utf-8"),
        "JS": (ROOT / "web/app.js").read_text(encoding="utf-8"),
        "WASM": base64.b64encode(wasm.read_bytes()).decode("ascii"),
    }.items():
        template = template.replace("{{" + key + "}}", content)
    (ROOT / "web/index.html").write_text(template, encoding="utf-8")
    print(f"Built {wasm.stat().st_size:,}-byte WASM + standalone web/index.html (no external dependencies)")

if __name__ == "__main__":
    main()
