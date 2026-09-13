# AGAVE : Advanced GPU Accelerated Volume Explorer

AGAVE is a desktop application for viewing multichannel volume data. Several formats are supported, including OME-ZARR 0.4 and 0.5, OME-TIFF and Zeiss .czi files.

![screenshot](https://github.com/user-attachments/assets/b96618f2-7020-4b93-936e-9b32b795ea83)

## To install AGAVE:

[Install instructions](INSTALL.md)

## How to build from source

After cloning this repo, initialize the submodules, which contain a couple of dependency libraries:

```console
git submodule update --init
```

The commands below build the desktop application, unit tests, and the
Python native module (use Python 3.10 or later).
CMake options are provided for AGAVE_BUILD_APP and AGAVE_BUILD_TESTS, and both default to "ON".

Building requires the Vulkan SDK. CMake uses the SDK named by `VULKAN_SDK`,
or searches the platform's usual Vulkan SDK installation directory.

You may also refer to the github actions workflows for reference building AGAVE.

### Windows

Make sure you are in an environment where vsvarsall has been run, e.g. a "VS2026 x64 Native Tools Command Prompt"

**tensorstore** requires:

- Python 3.7 or later
- CMake 3.24 or later
- Perl, for building libaom from source (default). Must be in PATH. Not required if -DTENSORSTORE_USE_SYSTEM_LIBAOM=ON is specified.
- NASM, for building libjpeg-turbo, libaom, and dav1d from source (default). Must be in PATH.Not required if -DTENSORSTORE*USE_SYSTEM*{JPEG,LIBAOM,DAV1D}=ON is specified.
- GNU Patch or equivalent. Must be in PATH.

A convenient way to install Perl, NASM, and GNU Patch is with chocolatey.

```console
choco install strawberryperl nasm patch
```

**Install Qt LTS 6.9.3.**
In your favorite Python virtual environment:

```console
pip install aqtinstall
aqt install-qt --outputdir C:\Qt windows desktop 6.9.3 win64_msvc2022_64 -m qtwebsockets qtimageformats
```

Use vcpkg (must use target triplet x64-windows) to install the following:

```console
vcpkg install spdlog zlib libjpeg-turbo liblzma tiff zstd curl --triplet x64-windows
```

**Build AGAVE**

```console
cmake -S . -B build -G "Ninja Multi-Config" ^
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel
cmake --install build --config Release

# you may also choose --config RelWithDebInfo or Debug
# for day to day Windows dev / debugging of the app executable
# Alternative build command:
cmake --build build --target install
```

Adjust the vcpkg path depending on your configuration.

If you encounter issues during your build, check that all of your dependencies are installed and try again. It can be helpful to delete the \_deps subdirectory in your build path.

### macOS Apple Silicon (Homebrew)

In your favorite Python virtual environment:

```console
pip install aqtinstall
aqt install-qt --outputdir ~/Qt mac desktop 6.9.3 -m qtwebsockets qtimageformats
export Qt6_DIR=~/Qt/6.9.3/macos
brew install spdlog libtiff nasm curl

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DAGAVE_BUILD_APP=ON -DAGAVE_BUILD_TESTS=ON
cmake --build build --parallel
# optional
cmake --install build
```

After the build, `build/agave.app` is runnable. To create the redistributable
disk image, run `cpack --config build/CPackConfig.cmake`.

### Linux

Install Qt 6.9.3 in your directory of choice and tell the build where to find it.
In your favorite Python virtual environment:

```console
pip install aqtinstall
aqt install-qt --outputdir ~/Qt linux desktop 6.9.3 -m qtwebsockets qtimageformats
export Qt6_DIR=~/Qt/6.9.3/gcc_64

sudo apt install cmake ninja-build libtiff-dev libglm-dev libgl1-mesa-dev \
  libegl1-mesa-dev libspdlog-dev libcurl4-openssl-dev liblzma-dev \
  libzstd-dev zlib1g-dev nasm patch libxcb1-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DAGAVE_BUILD_APP=ON -DAGAVE_BUILD_TESTS=ON
cmake --build build --parallel
# optional
cmake --install build
```

If cmake fails please refer to the Dockerfile or the github actions workflows for a more complete list of Linux dependencies.

### Iterative standalone Python development

The platform commands above create a persistent `build` directory. CMake uses
the active Python interpreter and C++ compiler environment, and its default
build compiles the shared static renderlib, the desktop application,
and the ABI-specific `agave_py2` native module:

```console
cmake --build .
```

Then, from the repository's `agave_pyvk` directory:

```console
python -m pip install -e .
```

Run the same commands from a VS2026 x64 Native Tools Command Prompt on Windows.
The CMake build stages the native module and its runtime dependencies directly
in the Python package. The editable install performs only normal Python
packaging; it does not invoke CMake or rebuild renderlib. Python source changes
are visible immediately.

Optional: To save a little time in building the Python module only, use the same
platform-specific CMake configure command with these options:

```console
-DAGAVE_BUILD_APP=OFF
```

## Versioned Releases

Use tbump (https://github.com/your-tools/tbump). See the tbump.toml file which shows all the files that contain necessary version info.

Just run

```

tbump major.minor.patch --dry-run

```

and if everything looks ok

```

tbump major.minor.patch

```

or, to do the git steps manually:

```

tbump major.minor.patch --only-patch

```
