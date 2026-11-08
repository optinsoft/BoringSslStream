Set-Location $PSScriptRoot

# --- Ensure MSVC (cl.exe) is on PATH ------------------------------------
# Without this, CMake may silently pick gcc/MinGW from PATH and produce
# a dll with a different name / runtime. Prefer MSVC explicitly.

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

    if (-not (Test-Path $vswhere)) {
        throw "MSVC not found. Install Visual Studio with the 'Desktop development with C++' workload, or run this script from 'Developer PowerShell for VS'."
    }

    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath

    if (-not $vsPath) {
        throw "Visual Studio with the C++ workload was not found. Install 'Desktop development with C++' or run this script from 'Developer PowerShell for VS'."
    }

    # Import the VS developer environment (PATH, INCLUDE, LIB for cl.exe).
    $devShell = Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1"

    if (-not (Test-Path $devShell)) {
        throw "Launch-VsDevShell.ps1 was not found in $vsPath."
    }

    & $devShell -Arch amd64 -HostArch amd64 | Out-Null
}

# --- Configure and build (Ninja, single-config) --------------------------

Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue

# Pin the compiler to cl.exe so gcc from PATH is never picked.
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl

cmake --build build

# --- Sanity check: DLL name must match the P/Invoke name -----------------

$dll = Join-Path $PSScriptRoot "build\proxymap_boringssl.dll"

if (-not (Test-Path $dll)) {
    Write-Warning "Expected build\proxymap_boringssl.dll was not produced. Check the build output above."
} else {
    Write-Host "OK: $dll"
}
