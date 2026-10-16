# Builds build\ReMystery-<arch>.scr with MSVC. Run from any PowerShell on Windows with
# Visual Studio (or Build Tools) and the "Desktop development with C++" workload installed.
#   pwsh windows/build.ps1 [-Arch x64|arm64]
param([ValidateSet('x64', 'arm64')][string]$Arch = 'x64')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$obj = Join-Path $build "win-$Arch"
New-Item -ItemType Directory -Force -Path $obj | Out-Null

$webview2Version = '1.0.4129.50'
$sdk = Join-Path $build "webview2-$webview2Version"
if (-not (Test-Path (Join-Path $sdk 'build\native\include\WebView2.h'))) {
    $zip = Join-Path $build "webview2-$webview2Version.zip"
    Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$webview2Version" -OutFile $zip
    Expand-Archive -Force $zip $sdk
}

# Import the MSVC environment for the target architecture.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with C++ tools was not found.' }
$hostArch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'amd64' }
$target = if ($Arch -eq 'x64') { 'amd64' } else { 'arm64' }
$vcvarsArch = if ($hostArch -eq $target) { $target } else { "${hostArch}_$target" }
cmd /c "`"$vs\VC\Auxiliary\Build\vcvarsall.bat`" $vcvarsArch >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}

$version = (Get-Content (Join-Path $root 'VERSION')).Trim()
$parts = $version.Split('.')
@"
#define VER_MAJOR $($parts[0])
#define VER_MINOR $($parts[1])
#define VER_PATCH $($parts[2])
#define VER_STRING "$version"
"@ | Set-Content -Encoding ascii (Join-Path $obj 'version.h')
Push-Location (Join-Path $root 'windows')
try {
    rc /nologo "/I$obj" /fo "$obj\remystery.res" remystery.rc
    if ($LASTEXITCODE) { throw 'rc failed' }
    cl /nologo /std:c++17 /O2 /MT /W4 /EHsc /DUNICODE /D_UNICODE "/I$sdk\build\native\include" `
       main.cpp "/Fo$obj\main.obj" "/Fe$build\ReMystery-$Arch.scr" "$obj\remystery.res" `
       "$sdk\build\native\$Arch\WebView2LoaderStatic.lib" `
       user32.lib gdi32.lib ole32.lib shell32.lib advapi32.lib version.lib `
       /link /SUBSYSTEM:WINDOWS /MANIFEST:NO
    if ($LASTEXITCODE) { throw 'cl failed' }
} finally { Pop-Location }
Write-Host "Built build\ReMystery-$Arch.scr"
