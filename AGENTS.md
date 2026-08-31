# Project Guidelines

AGAVE (Advanced GPU Accelerated Volume Explorer) is a C++17/Qt6 desktop application for viewing multichannel volumetric image data (OME-ZARR, OME-TIFF, CZI). See [README.md](README.md) for full details.

## Architecture

| Module            | Role                                                                                                                                              |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `agave_app/`      | Qt6 GUI layer — widgets, dialogs, dock panels, OpenGL viewport (`GLView3D`)                                                                       |
| `renderlib/`      | Core rendering engine — image I/O (`io/`), graphics abstraction (`gfxapi/`) with OpenGL and Vulkan backends (`gfxOpenGL/`, `gfxVulkan/`), camera, scene, gesture handling (`gesture/`), JSON serialization (`json/`) |
| `agave_pyclient/` | Python WebSocket client for remote control of AGAVE in server mode                                                                                |
| `test/`           | C++ unit tests (Catch2)                                                                                                                           |
| `webclient/`      | JavaScript client                                                                                                                                 |

`agave_app` depends on `renderlib` for all rendering and data operations. Keep GUI concerns out of `renderlib`. `renderlib` should have no Qt dependencies and be testable in isolation. The Python client and web client communicate with the C++ engine via a binary command protocol defined in `renderlib/command.h` and implemented in `renderlib/command.cpp`. Commands must be added in all three locations to stay in sync (see "Adding a New Command" below).

### Graphics backends (`gfxapi`)

`renderlib/gfxapi/` is a backend-agnostic graphics abstraction so the rest of the
renderer never talks to OpenGL or Vulkan directly. Its central interface is
`gfxApi::Backend`, which owns the GPU device and any backend-global state and
creates the concrete renderers, framebuffers, and contexts. `IGraphicsDevice`
exposes the device-level primitives (shaders and shader programs today; the
surface is deliberately minimal and grows as more primitives move behind it), and
sibling interfaces cover the render window (`IRenderWindow`), gesture/manipulator
drawing (`IGestureRenderer`), `Framebuffer`, `IGLContext`, and `IWindowSurface`.

Concrete implementations live in `gfxOpenGL/` and `gfxVulkan/` (Vulkan is gated
behind `AGAVE_HAS_VULKAN`; `WebGPU` is enumerated in `BackendKind` but not yet
implemented). `renderlib::initialize` creates exactly **one** `Backend` for the
process lifetime, chosen from `InitParams::backendKind`. The `createGraphicsBackend`
function in `renderlib.cpp` is the single place that maps a `BackendKind` onto a
concrete backend — the abstract `gfxapi/` layer must not depend on any concrete
backend, and renderer code should reach GPU functionality through `Backend::device()`
and the `gfxapi` interfaces rather than backend-specific APIs.

Backend bring-up is two-phase: construction creates the instance/context, then a
device is selected. Windowed Vulkan defers device selection to
`Backend::initDeviceForWindow()` once a native surface exists; headless uses EGL
(OpenGL) or `initDeviceHeadless()` (Vulkan). A null surface is only valid in
headless mode.

## Build and Test

Prerequisites and platform-specific setup are in [INSTALL.md](INSTALL.md). Dependencies are fetched via CMake FetchContent (GLM, Catch2) and require Qt 6.9.3 installed on the system.

After cloning, initialize submodules:

```bash
git submodule update --init
```

### macOS (Homebrew)

```bash
brew install spdlog libtiff nasm curl

pip install aqtinstall
aqt install-qt --outputdir ~/Qt mac desktop 6.9.3 -m qtwebsockets qtimageformats
export Qt6_DIR=~/Qt/6.9.3/macos

mkdir build && cd build
cmake .. -G Ninja
cmake --build .
```

### Windows

Run from a **VS2026 x64 Native Tools Command Prompt**. Requires Perl, NASM, and GNU Patch in PATH (install via `choco install strawberryperl nasm patch`).

```powershell
pip install aqtinstall
aqt install-qt --outputdir C:\Qt windows desktop 6.9.3 win64_msvc2022_64 -m qtwebsockets qtimageformats

vcpkg install spdlog zlib libjpeg-turbo liblzma tiff zstd curl --triplet x64-windows

mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>\scripts\buildsystems\vcpkg.cmake -G "Ninja Multi-Config" -DVCPKG_TARGET_TRIPLET=x64-windows ..
cmake --build . --target install
```

### Tests and Analysis

```bash
# C++ tests (Catch2) — run automatically post-build
# Test sources are in test/

# Python client
pip install -e agave_pyclient/[test]
pytest agave_pyclient/tests/

# Static analysis — run clang-tidy on individual files
# The build generates compile_commands.json in the build directory.

# macOS / Linux (requires clang-tidy, e.g. brew install llvm)
clang-tidy -p build renderlib/RenderSettings.cpp

# Windows (from a VS2026 x64 Native Tools Command Prompt)
clang-tidy.exe -p build renderlib\RenderSettings.cpp

# To auto-apply suggested fixes, add --fix:
clang-tidy -p build --fix renderlib/RenderSettings.cpp
```

## Code Style

### C++

- **Standard:** C++17
- **Classes, methods, enums:** PascalCase (`GLView3D`, `RenderSettings`, `GetNoIterations()`)
- **Member variables:** `m_` prefix (`m_Type`, `m_DirtyFlags`, `m_qcamera`)
- **Header guards:** prefer `#pragma once`
- **Include order:** local project headers → standard C++ headers → third-party headers → Qt headers
- **Static analysis:** Run `clang-tidy -p build <file>` on individual source files (the build exports `compile_commands.json`). Add `--fix` to auto-apply suggestions.

### Python

- PEP 8 / snake_case
- Tooling: - `ruff check`, `ruff format`.

## Conventions

- Versioning is managed with `tbump` — run `tbump <version>` to bump across all components
- Contribution workflow and PR process: [CONTRIBUTING.md](CONTRIBUTING.md)

## Adding a New Command

Commands are the binary protocol connecting the C++ engine, Python client, and web client. Every command must be added with the following steps to stay in sync.

### 1. `renderlib/command.h` — declare data struct + command class

```cpp
// Data struct (plain POD)
struct SetFooCommandD
{
  float m_x;
  int32_t m_mode;
};

// CMDDECL(ClassName, UniqueID, "python_name", argTypes)
// Use the next available integer ID.
CMDDECL(SetFooCommand, 52, "set_foo",
        CMD_ARGS({ CommandArgType::F32, CommandArgType::I32 }));
```

### 2. `renderlib/command.cpp` — implement `execute`, `parse`, `write`, `toPythonString`

```cpp
void SetFooCommand::execute(ExecutionContext* c)
{
  c->m_appScene->m_foo = m_data.m_x;
  c->m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

SetFooCommand* SetFooCommand::parse(ParseableStream* c)
{
  SetFooCommandD data;
  data.m_x = c->parseFloat32();
  data.m_mode = c->parseInt32();
  return new SetFooCommand(data);
}

size_t SetFooCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  bytesWritten += o->writeInt32(m_data.m_mode);
  return bytesWritten;
}

std::string SetFooCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(" << m_data.m_x << ", " << m_data.m_mode << ")";
  return ss.str();
}
```

### 3. `agave_app/commandBuffer.cpp` — register in the switch

Add `CMD_CASE(SetFooCommand);` in the `processBuffer()` switch statement.

### 4. `test/test_commands.cpp` — round-trip test

```cpp
SECTION("SetFooCommand")
{
  SetFooCommandD data = { 1.5f, 3 };
  auto cmd = testcodec<SetFooCommand, SetFooCommandD>(data);
  REQUIRE(cmd->toPythonString() == "set_foo(1.5, 3)");
  REQUIRE(cmd->m_data.m_x == data.m_x);
  REQUIRE(cmd->m_data.m_mode == data.m_mode);
}
```

### 5. `agave_pyclient/agave_pyclient/commandbuffer.py` — add to `COMMANDS` dict

```python
"SET_FOO": [52, "F32", "I32"],
```

### 6. `agave_pyclient/agave_pyclient/agave.py` — add method to `AgaveRenderer`

```python
def set_foo(self, x: float, mode: int):
    self.cb.add_command("SET_FOO", x, mode)
```

### 7. `webclient/src/commandbuffer.ts` — add to `COMMANDS` object

```typescript
SET_FOO: [52, "F32", "I32"],
```

### 8. `webclient/src/agave.ts` — add method to `AgaveClient`

```typescript
set_foo(x: number, mode: number) {
  this.cb.addCommand("SET_FOO", x, mode);
}
```

Steps 9–11 also make the command available in `agave_pyvk`, the in-process,
headless Vulkan package. It does **not** serialize command buffers: `AgaveRenderer`
passes typed arguments straight into `renderlib` through nanobind, and the C++ side
dispatches on the integer command ID via `PythonRenderer`. Because that dispatch is
data-driven, `agave_pyvk/src/bindings.cpp` is generic and needs **no** per-command
changes.

### 9. `renderlib/PythonRenderer.cpp` — register in both switches

```cpp
// In commandArgumentTypes(): maps ID -> argument type list.
COMMAND_ARGUMENT_TYPES(52, SetFooCommand);

// In execute(): constructs the command from positional args and runs it.
// The count and the arg<T>(args, N) order/types must match the command's CMD_ARGS.
EXECUTE_COMMAND(52, 2, SetFooCommand, SetFooCommandD{ arg<float>(args, 0), arg<int32_t>(args, 1) });
```

### 10. `agave_pyvk/agave_pyvk/commandbuffer.py` — add to `COMMANDS`

This module keeps only the ID (no argument-type list); it exists for API/source
parity with `agave_pyclient`.

```python
"SET_FOO": 52,
```

### 11. `agave_pyvk/agave_pyvk/agave.py` — add method to `AgaveRenderer`

Mirror the `agave_pyclient` method, but dispatch through `self._execute`, which
calls the native renderer synchronously and returns its result.

```python
def set_foo(self, x: float, mode: int):
    return self._execute("SET_FOO", x, mode)
```

**Key rules:**

- The integer ID must be unique and match across all locations, including
  `renderlib/command.h` and both `PythonRenderer` switches
- Argument types are `F32`, `I32`, `S` (string), `F32A` (float array), `I32A` (int array)
- Python method name uses snake_case; `COMMANDS` dict key is UPPERCASE
- `parse()`/`write()` field order must match the `CMD_ARGS` type list exactly, as
  must the `EXECUTE_COMMAND` arg count and `arg<T>()` order/types
- `agave_pyvk/src/bindings.cpp` is generic — do not add per-command code there
- The public method must exist in **both** `agave_pyclient/agave.py` and
  `agave_pyvk/agave.py`. `test_public_api_matches_pyclient` in
  `agave_pyvk/tests/test_api.py` enforces that every `agave_pyclient` method is
  also present on `agave_pyvk` (which only adds `load_array`)
