# Development

ReRevved Studio is a C++23 CMake project. Build output stays under `out/build/<preset>`. The first configure downloads the pinned GLFW, Dear ImGui, and dr_mp3 revisions through CMake FetchContent.

## Prerequisites

All supported platforms require:

- CMake 3.25 or newer
- Ninja
- Git
- PowerShell 7 (`pwsh`)
- a C++23-capable Clang toolchain
- clang-format 22.x exactly
- OpenGL and the platform development libraries required by GLFW

Windows presets invoke `clang` and `clang++` from `PATH`. Linux uses `clang-22` and `clang++-22`. The repository script rejects a clang-format major version other than 22.

## Presets

| Preset | Configuration |
|---|---|
| `win-amd64-debug` | Windows AMD64 Debug |
| `win-amd64-release` | Windows AMD64 Release |
| `linux-amd64-debug` | Linux AMD64 Debug |

## Build and test

From a fresh PowerShell session in the repository root, run these Windows
presets:

```powershell
pwsh -File .\scripts\verify.ps1 -Preset win-amd64-debug
pwsh -File .\scripts\verify.ps1 -Preset win-amd64-release
```

For the selected preset, the script performs:

1. CMake configure.
2. Build.
3. CTest with failure output.
4. One hidden GLFW/OpenGL/ImGui application smoke frame using `README.md` as synthetic command-line input.
5. clang-format 22.x dry-run over `api`, `src`, and `tests`.
6. `git diff --check`.

Use `-SkipAppSmoke` only when the caller cannot provide a display or OpenGL context. Do not report a full smoke result when it is used.

## Individual commands

Use these individual commands to diagnose one stage:

```powershell
cmake --preset win-amd64-debug
cmake --build --preset win-amd64-debug
ctest --preset win-amd64-debug
.\out\build\win-amd64-debug\rerevved-studio.exe --smoke-test .\README.md
```

Replace the preset consistently for Release.
To force CMake to regenerate its configure state and rebuild targets:

```powershell
cmake --fresh --preset win-amd64-debug
cmake --build --preset win-amd64-debug --clean-first
ctest --preset win-amd64-debug
```

The application also accepts ordinary file paths as command-line arguments, but retail files are never required for tests or smoke tests.

## Test and content policy

- Tests use only wholly synthetic repository-owned bytes and generated fixtures.
- Do not add retail files, extracted assets, captures, machine paths, private
  evidence, or build output.
- Parser and document tests must remain GPU-free and window-free.
- Preserve exact dependency revisions and review licenses before adding or
  changing a dependency.

Read [Architecture](architecture.md) before changing ownership boundaries and [File format support](file-formats.md) before changing parser behavior. Public contribution rules are in [CONTRIBUTING.md](../CONTRIBUTING.md).
