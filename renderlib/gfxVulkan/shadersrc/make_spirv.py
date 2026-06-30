#!/usr/bin/env python3
"""
Compile Vulkan GLSL shaders to SPIR-V and emit C++ headers with uint32_t blobs.

Usage:
  python3 make_spirv.py /path/to/glslc
"""

import pathlib
import re
import struct
import subprocess
import sys
import tempfile


SHADERS = [
    "basicVolume.vert",
    "basicVolume.frag",
    "copy.vert",
    "copy.frag",
    "flat.vert",
    "flat.frag",
    "gui.vert",
    "gui.frag",
    "imageNoLut.vert",
    "imageNoLut.frag",
    "pathTraceVolume.vert",
    "pathTraceVolume.frag",
    "ptAccum.vert",
    "ptAccum.frag",
    "thickLines.vert",
    "thickLines.frag",
    "toneMap.vert",
    "toneMap.frag",
    "volume.vert",
    "volume.frag",
]


def symbol_name(shader: str) -> str:
    return re.sub(r"[^0-9a-zA-Z_]", "_", shader) + "_spv"


def compile_shader(glslc: str, shader_path: pathlib.Path) -> bytes:
    with tempfile.NamedTemporaryFile(suffix=".spv") as tmp:
        subprocess.check_call(
            [
                glslc,
                "--target-env=vulkan1.3",
                str(shader_path),
                "-o",
                tmp.name,
            ]
        )
        return pathlib.Path(tmp.name).read_bytes()


def write_header(shader: str, spirv: bytes, output_path: pathlib.Path) -> None:
    if len(spirv) % 4 != 0:
        raise RuntimeError(f"{shader} produced a SPIR-V blob that is not word aligned")

    words = struct.unpack(f"<{len(spirv) // 4}I", spirv)
    name = symbol_name(shader)
    with output_path.open("w", encoding="utf-8") as out:
        out.write("// Generated SPIR-V shader header. Do not edit by hand.\n")
        out.write("#pragma once\n\n")
        out.write("#include <cstddef>\n")
        out.write("#include <cstdint>\n\n")
        out.write(f"static const uint32_t {name}[] = {{\n")
        for i in range(0, len(words), 8):
            out.write("  ")
            out.write(", ".join(f"0x{word:08x}u" for word in words[i : i + 8]))
            out.write(",\n")
        out.write("};\n")
        out.write(f"static const size_t {name}_word_count = sizeof({name}) / sizeof({name}[0]);\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: make_spirv.py /path/to/glslc", file=sys.stderr)
        return 2

    glslc = sys.argv[1]
    source_dir = pathlib.Path(__file__).resolve().parent
    for shader in SHADERS:
        shader_path = source_dir / shader
        spirv = compile_shader(glslc, shader_path)
        write_header(shader, spirv, source_dir / f"{symbol_name(shader)}.hpp")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
