# BoringSslStream 1.0.4

Date: 2026-11-16

## Changes

### Multi-targeting: .NET 8 and .NET 9

- All projects (`BoringSslStream`, `BoringSslConsole`, `BoringSslStream.Tests`)
  now target both `net8.0` and `net9.0` (`<TargetFrameworks>net8.0;net9.0</TargetFrameworks>`).

### NuGet packaging

- Removed the parameterized pack RIDs introduced in 1.0.3
  (`MacNativeRuntimeIdentifier`, `LinuxNativeRuntimeIdentifier`).
  The package now always bundles the native binaries for all supported RIDs —
  `win-x64`, `linux-x64`, `linux-arm64`, `osx-arm64` — each included
  automatically when the corresponding `Native/build/<rid>` artifact exists.
- `dotnet pack` no longer requires any extra `-p:` properties: it simply picks
  up every native library that has been built.

### Local builds

- `BoringSslStream.csproj` auto-detects the host platform/architecture
  (`_NativeRid`: `win-x64`, `linux-x64`, `linux-arm64`, `osx-arm64`) and copies
  the matching native library flat into the build output directory
  (`CopyToOutputDirectory=PreserveNewest`). `DllImport` probes the output
  directory first, so `dotnet build` / `dotnet test` / `dotnet run` work out of
  the box after the native library is built — no manual DLL copying.
- The `EnsureNativeLibrary` warning target now checks all four RID directories
  and was added to the console and test projects as well.

## Upgrading

- If you pass `-p:MacNativeRuntimeIdentifier=...` or
  `-p:LinuxNativeRuntimeIdentifier=...` to `dotnet pack`, drop those
  properties — packaging is now automatic. Build the native libraries for the
  RIDs you want to ship (`Native/build.ps1` / `Native/build.sh`) and
  `dotnet pack` will bundle whatever is present.
