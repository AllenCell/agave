##################################################################
##
## After any shader edits, run this script to convert the shaders
## and then build renderlib to embed the shaders into the library.
##
## Because "R" string literals are limited in length, this script
## breaks up the shader source into chunks and then concatenates
## them in the generated C++ source file.
##################################################################
import sys


def read_file_in_chunks(file_path, chunk_size=12 * 1024):
    """
    Reads a file and yields chunks of text no larger than chunk_size,
    ensuring chunks split on newline boundaries.
    """
    with open(file_path, "r", encoding="utf-8") as file:
        buffer = ""
        for line in file:
            if len(buffer) + len(line) > chunk_size:
                yield buffer
                buffer = line
            else:
                buffer += line
        if buffer:
            yield buffer


def convert_shader_to_c(input_file, output_file, chunk_size=12 * 1024):
    shadername = input_file  # input_file.split('/')[-1]
    shadername = (
        shadername.replace("-", "_")
        .replace(".", "_")
        .replace(" ", "_")
        .replace("/", "_")
    )

    with open(output_file, "w", encoding="utf-8") as cpp_file:
        cpp_file.write("// Generated C source file containing shader\n\n")
        cpp_file.write("#include <string>\n\n")
        num_chunks = 0
        for i, chunk in enumerate(read_file_in_chunks(input_file, chunk_size)):
            cpp_file.write(
                f'const std::string {shadername}_chunk_{i} = R"(\n{chunk}\n)";\n\n'
            )
            num_chunks += 1

        cpp_file.write(f"const std::string {shadername}_src = \n")
        for i in range(num_chunks):
            cpp_file.write(f"    {shadername}_chunk_{i}")
            if i < num_chunks - 1:
                cpp_file.write(" +\n")
        cpp_file.write(";\n")


if __name__ == "__main__":
    # ALL NEW SHADERS MUST BE CONVERTED TO C STRINGS VIA THIS SCRIPT IN ORDER TO BE EMBEDDED
    shaders = [
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
    ]
    for input_shader in shaders:
        shadername = input_shader  # input_file.split('/')[-1]
        shadername = (
            shadername.replace("-", "_")
            .replace(".", "_")
            .replace(" ", "_")
            .replace("/", "_")
        )
        shadername = shadername + "_gen.hpp"

        convert_shader_to_c(input_shader, shadername)
