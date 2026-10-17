<#
.SYNOPSIS
Builds weft into the plugin's DLL and copies it, its import library and weft's public headers into
Source/ThirdParty/WeftLibrary.

.DESCRIPTION
Weft (https://github.com/JiahaoLi01/weft) is built from a checkout of its own (by default the folder `weft` next to the
game project, or WEFT_SOURCE_DIR), with CMake and Visual Studio 2022; the Vulkan SDK's DXC adds its Vulkan backend.
The DLL exports only weft's C ABI. Run it again after the weft checkout changes (a rebase onto weft's main, say).

.EXAMPLE
pwsh Plugins/Weft/Scripts/BuildWeft.ps1 -WeftSource F:/Projects/weft
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
$build = Join-Path $plugin "Intermediate/WeftBuild"
Write-Host "Weft: building $WeftSource ($Config) in $build"
cmake -S (Join-Path $plugin "Native") -B $build -G "Visual Studio 17 2022" -A x64 "-DWEFT_SOURCE_DIR=$WeftSource"
if ($LASTEXITCODE) { throw "cmake configure failed" }
cmake --build $build --config $Config --target weft_unreal --parallel
if ($LASTEXITCODE) { throw "cmake build failed" }

$library = Join-Path $plugin "Source/ThirdParty/WeftLibrary"
$binaries = Join-Path $plugin "Binaries/ThirdParty/Weft/Win64"
New-Item -ItemType Directory -Force (Join-Path $library "include/weft"), (Join-Path $library "lib/Win64"), $binaries | Out-Null
Copy-Item (Join-Path $WeftSource "include/weft/*.h") (Join-Path $library "include/weft") -Force
$out = Join-Path $build $Config
Copy-Item (Join-Path $out "weft.lib") (Join-Path $library "lib/Win64") -Force
Copy-Item (Join-Path $out "weft.dll") $binaries -Force
if (Test-Path (Join-Path $out "weft.pdb")) { Copy-Item (Join-Path $out "weft.pdb") $binaries -Force }
# The effect compiler with weft's shell: a game compiles its UI shader effects (-weft-effect) with them
# (cmake -DSOURCE=<effect.hlsl> -DOUTPUT=<dir>/<name> -P effects/compile_effect.cmake), without weft's source tree.
$effects = Join-Path $library "effects"
New-Item -ItemType Directory -Force $effects | Out-Null
Copy-Item (Join-Path $WeftSource "cmake/compile_effect.cmake"), (Join-Path $WeftSource "cmake/check_effect.cmake"),
    (Join-Path $WeftSource "src/render/shaders/scene.hlsl") $effects -Force
$commit = (git -C $WeftSource rev-parse HEAD).Trim()
$branch = (git -C $WeftSource rev-parse --abbrev-ref HEAD).Trim()
Set-Content -Encoding utf8 (Join-Path $library "WEFT_VERSION") "$commit $branch"
Write-Host "Weft: $branch $commit -> $binaries"
