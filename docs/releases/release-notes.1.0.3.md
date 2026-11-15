# BoringSslStream 1.0.3

Date: 2026-11-15

## Changes

### Per-platform native library builds

- Each platform now builds into its own directory `Native/build/<rid>` instead
  of the shared `Native/build/`:
  - `Native/build/win-x64/` — `proxymap_boringssl.dll` (Windows)
  - `Native/build/linux-x64/` or `Native/build/linux-arm64/` — `libproxymap_boringssl.so` (Linux)
  - `Native/build/osx-arm64/` or `Native/build/osx-x64/` — `libproxymap_boringssl.dylib` (macOS)
- `Native/build.sh` now auto-detects the RID from the platform
  (`Linux-x86_64` → `linux-x64`, `Linux-aarch64` → `linux-arm64`,
  `Darwin-arm64` → `osx-arm64`, `Darwin-x86_64` → `osx-x64`);
  the RID can be overridden: `RID=linux-arm64 ./build.sh`.
- Linux/macOS builds now run in parallel (`cmake --build -j`).
- The script prints the list of built native libraries when finished.

### Windows build (win-x64)

- `Native/build.ps1` now verifies that `cl.exe` actually targets the required
  architecture (`x64`, `x86` or `ARM64`) instead of just being present in PATH —
  otherwise CMake could silently pick gcc/MinGW and produce a DLL with a
  different name / runtime. The desired RID is passed as a parameter:
  `.\build.ps1 -Rid win-arm64`.
- `VsDevShell` is launched with `-SkipAutomaticLocation` so the current
  directory is not changed.
- `cmake` exit codes are now checked — the script exits with a non-zero code
  on configuration or build failure.

### NuGet packaging

- `BoringSslStream.csproj` supports parameterized RIDs for packaging:
  - `MacNativeRuntimeIdentifier` (defaults to `osx-arm64`):
    `dotnet pack -p:MacNativeRuntimeIdentifier=osx-x64`
  - `LinuxNativeRuntimeIdentifier` (defaults to `linux-x64`):
    `dotnet pack -p:LinuxNativeRuntimeIdentifier=linux-arm64`
- The `EnsureNativeLibrary` check now looks for native libraries under
  `Native/build/<rid>`.

### Other

- Added a devcontainer configuration (`.devcontainer/Dockerfile`,
  `devcontainer.json`) for container-based development.
- README updated: per-platform build instructions and the `build/<rid>`
  directory layout.

## Upgrading

- If you build the native libraries manually, point to `Native/build/<rid>`;
  artifacts in the old non-RID `Native/build/` directory are no longer used.
