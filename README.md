# ShaderLab

ShaderLab is a Windows-only Vulkan 1.4 shader authoring tool. The implementation follows the staged plan in [`doc/implementation_plan.md`](doc/implementation_plan.md); M1 is currently in progress.

## Prerequisites

- Windows 10/11 x64
- Visual Studio 2022 with the MSVC x64 C++ workload
- CMake 3.25+
- Vulkan SDK 1.4+
- A local [vcpkg](https://github.com/microsoft/vcpkg) checkout

Set `VCPKG_ROOT` to the vcpkg checkout, then configure, build, and test:

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset windows-msvc
cmake --build --preset debug
ctest --preset debug
```

For a non-interactive Vulkan initialization check (hidden window, exits after device creation):

```powershell
$env:SHADERLAB_SMOKE_TEST = '1'
.\build\Debug\shaderlab.exe
Remove-Item Env:SHADERLAB_SMOKE_TEST
```

Dependencies are pinned through the `builtin-baseline` in `vcpkg.json`. Build outputs and the manifest-mode installed tree remain local and are ignored by Git.

## Documentation

- [`doc/implementation_plan.md`](doc/implementation_plan.md): milestone scope and commit policy
- [`doc/progress.md`](doc/progress.md): verified progress and evidence
- [`doc/handover.md`](doc/handover.md): continuation notes and invariants

