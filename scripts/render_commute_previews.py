#!/usr/bin/env python3
"""Render production EInkDisplay.cpp with real glyphs and Adafruit GFX on host.

Pillow only converts the production framebuffer to PNG; it does not lay out text.
Requires the cached firmware dependencies, C++17 compiler, and Pillow.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist" / "previews")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--libdeps", type=Path, default=ROOT / ".pio/libdeps/zectrix_note4")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    gfx = args.libdeps / "Adafruit GFX Library"
    qr = args.libdeps / "QRCode/src"
    for source in (gfx / "Adafruit_GFX.cpp", qr / "qrcode.c"):
        if not source.is_file():
            parser.error(f"Missing cached firmware dependency: {source}; run dependency install or pass --libdeps")
    with tempfile.TemporaryDirectory(prefix="transitink-render-") as temporary:
        scratch = Path(temporary)
        executable = scratch / ("renderer.exe" if os.name == "nt" else "renderer")
        # Compile the simple display-text helper verbatim from its production
        # file without bringing the HTTP client into this hardware-free runner.
        weather_source = (ROOT / "src/WeatherClient.cpp").read_text(encoding="utf-8")
        helper_start = weather_source.index("String weatherDisplayText(")
        weather_helper = scratch / "weather_display_text.cpp"
        weather_helper.write_text('#include "WeatherClient.h"\n#include "core/UiText.h"\n' + weather_source[helper_start:], encoding="utf-8")
        sources = [ROOT / "test_host/render_eink_display.cpp", weather_helper, gfx / "Adafruit_GFX.cpp", qr / "qrcode.c"]
        sources += [ROOT / "src" / source for source in [
            "HkGlyphFont.cpp", "generated/HkGlyphFontData.cpp", "generated/UnifontGlyphFontData.cpp",
            "core/DisplayTextCore.cpp", "core/DisplayFontCore.cpp", "core/UiText.cpp", "core/BusEtaCore.cpp",
            "core/CommuteDashboardCore.cpp"]]
        command = [args.cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-DARDUINO=10800", "-DTRANSITINK_BOARD_ZECTRIX_NOTE4"]
        for include in [ROOT / "test_host/display_shims", ROOT / "include", gfx, qr]:
            command.extend(["-I", str(include)])
        command.extend(str(source) for source in sources)
        command.extend(["-o", str(executable)])
        subprocess.run(command, check=True, cwd=ROOT)
        subprocess.run([str(executable), str(scratch)], check=True, cwd=ROOT)
        previews = []
        for bitmap in sorted(scratch.glob("*.pbm")):
            with Image.open(bitmap) as frame:
                assert frame.size == (400, 300) and frame.mode == "1"
                assert frame.getextrema() == (0, 255)
                destination = output / (bitmap.stem + ".png")
                frame.save(destination)
            previews.append({"file": destination.name, "width": 400, "height": 300,
                             "mode": "1-bit monochrome", "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()})
        assert len(previews) == 40
    source_files = [ROOT / "src/EInkDisplay.cpp", ROOT / "src/core/CommuteDashboardCore.cpp", ROOT / "include/core/CommuteDashboardCore.h", ROOT / "src/WeatherClient.cpp", ROOT / "test_host/render_eink_display.cpp", gfx / "Adafruit_GFX.cpp",
                    ROOT / "src/generated/HkGlyphFontData.cpp", ROOT / "src/generated/UnifontGlyphFontData.cpp"]
    manifest = {"version": 3, "renderer": "production EInkDisplay.cpp + production glyphs + pinned Adafruit GFX",
                "fixtures": "synthetic; 13-40 use production planner outputs; hardware and clock mocked; no device write", "checks": ["planner-to-renderer arrival risk and nominal preservation", "semantic target/weather guards", "full/partial framebuffer parity", "400x300 monochrome"],
                "sources": [{"file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in source_files],
                "previews": previews}
    (output / "preview-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Rendered {len(previews)} production framebuffer previews to {output}")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
