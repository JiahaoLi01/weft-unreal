<#
.SYNOPSIS
Builds weft into the plugin's two DLLs and copies them, their import library and weft's public headers into the plugin.

.DESCRIPTION
Weft (https://github.com/JiahaoLi01/weft) is built from a checkout of its own (by default the folder `weft` next to the
game project, or WEFT_SOURCE_DIR), with CMake and Visual Studio 2022; the Vulkan SDK's DXC adds its Vulkan backend.
Each DLL exports only weft's C ABI, the same functions in both (weft's docs/devtools.md):

  Binaries/ThirdParty/Weft/Win64/Dev/weft.dll      weft's dev build (WEFT_DEVTOOLS=ON): its developer tools, off until
                                                   asked for. The editor and every configuration but Shipping.
  Binaries/ThirdParty/Weft/Win64/Release/weft.dll  weft's release build (WEFT_DEVTOOLS=OFF): no developer tools, no
                                                   sockets. Shipping (Source/ThirdParty/WeftLibrary/WeftLibrary.Build.cs
                                                   says when Shipping takes the dev build instead).

Run it again after the weft checkout changes (a rebase onto weft's main, say).

.EXAMPLE
powershell -File Plugins/Weft/Scripts/BuildWeft.ps1 -WeftSource F:/Projects/weft
#>
param(
    [string]$WeftSource = $env:WEFT_SOURCE_DIR,
    [ValidateSet("Release", "RelWithDebInfo")]
    [string]$Config = "Release"
)
# Native tools write warnings to stderr; their exit codes say whether they failed.
$ErrorActionPreference = "Continue"
$plugin = Split-Path -Parent $PSScriptRoot
if (-not $WeftSource) {
    # The plugin sits in <project>/Plugins/Weft: weft is looked for beside the project.
    $WeftSource = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $plugin))) "weft"
}
$WeftSource = (Resolve-Path $WeftSource -ErrorAction Stop).Path
if (-not (Test-Path (Join-Path $WeftSource "include/weft/weft.h"))) {
    throw "$WeftSource is not a weft checkout"
}

$library = Join-Path $plugin "Source/ThirdParty/WeftLibrary"
$binaries = Join-Path $plugin "Binaries/ThirdParty/Weft/Win64"
New-Item -ItemType Directory -Force (Join-Path $library "include/weft"), (Join-Path $library "lib/Win64") | Out-Null

# The two builds, each in a CMake build folder of its own: name, folder, WEFT_DEVTOOLS.
$builds = @(
    @{ Name = "Release"; Folder = "Intermediate/WeftBuild"; DevTools = "OFF" },
    @{ Name = "Dev"; Folder = "Intermediate/WeftBuildDev"; DevTools = "ON" }
)
foreach ($b in $builds) {
    $build = Join-Path $plugin $b.Folder
    Write-Host "Weft: building $WeftSource ($Config, $($b.Name): WEFT_DEVTOOLS=$($b.DevTools)) in $build"
    cmake -S (Join-Path $plugin "Native") -B $build -G "Visual Studio 17 2022" -A x64 "-DWEFT_SOURCE_DIR=$WeftSource" "-DWEFT_DEVTOOLS=$($b.DevTools)"
    if ($LASTEXITCODE) { throw "cmake configure failed ($($b.Name))" }
    cmake --build $build --config $Config --target weft_unreal --parallel
    if ($LASTEXITCODE) { throw "cmake build failed ($($b.Name))" }
    $out = Join-Path $build $Config
    $target = Join-Path $binaries $b.Name
    New-Item -ItemType Directory -Force $target | Out-Null
    Copy-Item (Join-Path $out "weft.dll") $target -Force
    if (Test-Path (Join-Path $out "weft.pdb")) { Copy-Item (Join-Path $out "weft.pdb") $target -Force }
    if ($b.Name -eq "Release") {
        # One import library for both: their exports are the same list, written from the same headers.
        Copy-Item (Join-Path $out "weft.lib") (Join-Path $library "lib/Win64") -Force
    }
}

# The DLL of the plugin's first layout (one build, in Win64 itself) is not loaded any more.
foreach ($old in @("weft.dll", "weft.pdb")) {
    $path = Join-Path $binaries $old
    if (Test-Path $path) {
        Remove-Item $path -Force -ErrorAction SilentlyContinue
        if (Test-Path $path) { Write-Warning "Weft: $path is in use; delete it once nothing loads it." }
    }
}

Copy-Item (Join-Path $WeftSource "include/weft/*.h") (Join-Path $library "include/weft") -Force
# The effect compiler with weft's shell: a game compiles its UI shader effects (-weft-effect) with them
# (cmake -DSOURCE=<effect.hlsl> -DOUTPUT=<dir>/<name> -P effects/compile_effect.cmake), without weft's source tree.
$effects = Join-Path $library "effects"
New-Item -ItemType Directory -Force $effects | Out-Null
Copy-Item (Join-Path $WeftSource "cmake/compile_effect.cmake"), (Join-Path $WeftSource "cmake/check_effect.cmake"),
    (Join-Path $WeftSource "src/render/shaders/scene.hlsl") $effects -Force
$commit = (git -C $WeftSource rev-parse HEAD).Trim()
$branch = (git -C $WeftSource rev-parse --abbrev-ref HEAD).Trim()
Set-Content -Encoding utf8 (Join-Path $library "WEFT_VERSION") "$commit $branch"
Write-Host "Weft: $branch $commit -> $binaries (Dev, Release)"
