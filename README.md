# AGAVE : Advanced GPU Accelerated Volume Explorer

AGAVE is a desktop application for viewing multichannel volume data. Several formats are supported, including OME-ZARR 0.4 and 0.5, OME-TIFF and Zeiss .czi files.

![screenshot](https://github.com/user-attachments/assets/b96618f2-7020-4b93-936e-9b32b795ea83)

## To install AGAVE:

[Install instructions](INSTALL.md)

## How to build from source:

After cloning this repo, initialize the submodules, which contain a couple of dependency libraries:

```
git submodule update --init
```

### For WINDOWS:

Make sure you are in an environment where vsvarsall has been run, e.g. a "VS2026 x64 Native Tools Command Prompt"

**tensorstore** requires:

- Python 3.7 or later
- CMake 3.24 or later
- Perl, for building libaom from source (default). Must be in PATH. Not required if -DTENSORSTORE_USE_SYSTEM_LIBAOM=ON is specified.
- NASM, for building libjpeg-turbo, libaom, and dav1d from source (default). Must be in PATH.Not required if -DTENSORSTORE*USE_SYSTEM*{JPEG,LIBAOM,DAV1D}=ON is specified.
- GNU Patch or equivalent. Must be in PATH.

A convenient way to install Perl, NASM, and GNU Patch is with chocolatey.

```
choco install strawberryperl nasm patch
```

**Install Qt LTS 6.9.3.**
In your favorite Python virtual environment:

```
pip install aqtinstall
aqt install-qt --outputdir C:\Qt windows desktop 6.9.3 win64_msvc2022_64 -m qtwebsockets qtimageformats

```

Use vcpkg (must use target triplet x64-windows) to install the following:

```
vcpkg install spdlog zlib libjpeg-turbo liblzma tiff zstd curl --triplet x64-windows
```

**Build AGAVE**

```
mkdir build
cd build
# (vs 2026)
cmake -DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake -G "Visual Studio 18 2026" -A x64 -DVCPKG_TARGET_TRIPLET=x64-windows ..

# or, example: ninja in separate build dir!
cmake -DCMAKE_TOOLCHAIN_FILE=C:\Users\%USERNAME%\source\repos\vcpkg\scripts\buildsystems\vcpkg.cmake -G "Ninja Multi-Config" -DVCPKG_TARGET_TRIPLET=x64-windows C:\Users\%USERNAME%\source\repos\allen-cell-animated\agave
cmake --build . --target install
```

You may need to adjust the vcpkg path depending on your configuration.

If you encounter issues during your build, check that all of your dependencies are installed and try again. You can also build to the INSTALL target with Visual Studio by opening the project solution (`agave.sln`).

### For MAC OS: (using homebrew)

In your favorite Python virtual environment:

```
pip install aqtinstall
aqt install-qt --outputdir ~/Qt mac desktop 6.9.3 -m qtwebsockets qtimageformats
export Qt6_DIR=~/Qt/6.9.3/macos
# and then:
brew install spdlog libtiff nasm curl

mkdir build
cd build
cmake ..
make
# after make, you should have a runnable agave.app
# or, to build a redistributable bundle:
sudo make install
```

### For LINUX:

Install Qt 6.9.3 in your directory of choice and tell the build where to find it.
In your favorite Python virtual environment:

```

pip install aqtinstall
aqt install-qt --outputdir ~/Qt linux desktop 6.9.3 -m qtwebsockets qtimageformats

# the next line is needed for CMake

export Qt6_DIR=~/Qt/6.9.3/gcc_64

sudo apt install libtiff-dev
sudo apt install libglm-dev
sudo apt install libgl1-mesa-dev
sudo apt install libegl1-mesa-dev
sudo apt install libspdlog-dev
sudo apt install libcurl4-openssl-dev
sudo apt install nasm
sudo apt install libxcb-xkb-dev

mkdir build
cd build
cmake ..
make

```

If cmake fails please refer to the Dockerfile for a more complete list of Linux dependencies.

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
