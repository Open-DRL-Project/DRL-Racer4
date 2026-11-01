param(
    [ValidateSet('firmware', 'configurator', 'flasher', 'prepare', 'export', 'all')]
    [string]$Target = 'all',
    [string]$ArmToolchain = $env:ARM_TOOLCHAIN,
    [string]$Msys = 'C:\msys64',
    [string]$ConfiguratorRuntime = ${env:ProgramFiles(x86)} + '\Betaflight\Betaflight-Configurator',
    [string]$AppVersion,
    [string]$UpdateRepository = 'Open-DRL-Project/DRL-Racer4',
    [switch]$UseGitHubCLI
)

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This build requires Windows.' }
$Root = $PSScriptRoot
$Utf8 = New-Object System.Text.UTF8Encoding($false)
$Version = (Get-Content -LiteralPath "$Root/VERSION" -Raw).Trim()
if ($AppVersion) { $Version = $AppVersion }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must contain major.minor.patch.' }
if ($UpdateRepository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw 'Invalid update repository.' }
$Revision = 'unknown'
$Modified = $true
if (Test-Path -LiteralPath "$Root/.git") {
    $Revision = & git -C $Root rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the source commit.' }
    $Changes = & git -C $Root status --porcelain
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the source status.' }
    $Modified = [bool]$Changes
}

function Invoke-Tool {
    param([string]$File, [string[]]$Arguments, [string]$Directory = $Root)
    Push-Location -LiteralPath $Directory
    try {
        & $File @Arguments | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "$File failed ($LASTEXITCODE)." }
    }
    finally { Pop-Location }
}

function Write-Json {
    param([string]$Path, $Value)
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 20), $Utf8)
}

function Copy-Tree {
    param([string]$Source, [string]$Destination)
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

function Get-Assets {
    param([string]$Folder)
    if (Test-Path -LiteralPath "$Folder/files") {
        Get-ChildItem -LiteralPath "$Folder/files" -Recurse -File | Sort-Object FullName
    }
}

function Get-InputHash {
    param([string]$Folder)
    $Buffer = New-Object IO.MemoryStream
    $Hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $Paths = @("$Folder/drl.patch") + @(Get-Assets $Folder | ForEach-Object FullName)
        foreach ($Path in $Paths) {
            $Bytes = [IO.File]::ReadAllBytes($Path)
            $Buffer.Write($Bytes, 0, $Bytes.Length)
        }
        $Buffer.Position = 0
        return ([BitConverter]::ToString($Hasher.ComputeHash($Buffer))).Replace('-', '').ToLowerInvariant()
    }
    finally { $Hasher.Dispose(); $Buffer.Dispose() }
}

function Get-Source {
    param([string]$Component)
    $Folder = Join-Path $Root $Component
    $Meta = Get-Content -LiteralPath "$Folder/base.json" -Raw | ConvertFrom-Json
    $Relative = if ($Component -eq 'configurator') { 'configurator/build/source' } else { 'firmware/build/betaflight' }
    $Destination = Join-Path $Root $Relative
    $Stamp = "$Destination.prepared.json"
    $Digest = Get-InputHash $Folder
    if (Test-Path -LiteralPath $Destination) {
        if (!(Test-Path -LiteralPath $Stamp)) { throw "$Destination has no preparation record. Move it aside first." }
        $Saved = Get-Content -LiteralPath $Stamp -Raw | ConvertFrom-Json
        if ($Saved.commit -ne $Meta.commit -or $Saved.patch -ne $Digest) {
            throw "$Component inputs changed. Preserve local edits and move the checkout aside before preparing again."
        }
        $Head = & git -C $Destination rev-parse HEAD
        if ($LASTEXITCODE -ne 0 -or $Head -ne $Meta.commit) { throw "$Destination is not at the recorded base commit." }
        return $Destination
    }
    [IO.Directory]::CreateDirectory((Split-Path $Destination -Parent)) | Out-Null
    Invoke-Tool git @('clone', '--depth', '1', '--branch', $Meta.tag, '--no-checkout', $Meta.repository, $Destination)
    Invoke-Tool git @('checkout', '--detach', $Meta.commit) $Destination
    Invoke-Tool git @('apply', '--check', "$Folder/$($Meta.patch)") $Destination
    Invoke-Tool git @('apply', "$Folder/$($Meta.patch)") $Destination
    if (Test-Path -LiteralPath "$Folder/files") { Copy-Tree "$Folder/files" $Destination }
    Write-Json $Stamp @{ commit = $Meta.commit; patch = $Digest }
    return $Destination
}

function Prepare-Ghidra {
    $Destination = "$Root/firmware/build/ghidra"
    if (Test-Path -LiteralPath $Destination) {
        if (!(Test-Path -LiteralPath "$Destination/DRL.gpr") -or
            !(Test-Path -LiteralPath "$Destination/DRL.rep")) {
            throw "Incomplete Ghidra project at $Destination. Move it aside before preparing again."
        }
    }
    else {
        Copy-Tree "$Root/firmware/ghidra" $Destination
    }
    Write-Host "Ghidra project: $Destination/DRL.gpr"
}

function Export-Patch {
    param([string]$Component)
    $Source = Get-Source $Component
    $Folder = Join-Path $Root $Component
    Invoke-Tool git @('add', '-A') $Source
    $Exclusions = @()
    foreach ($Asset in @(Get-Assets $Folder)) {
        $Relative = $Asset.FullName.Substring((Join-Path $Folder 'files').Length + 1).Replace('\', '/')
        Copy-Item -LiteralPath "$Source/$Relative" -Destination $Asset.FullName -Force
        $Exclusions += ":(exclude)$Relative"
    }
    # Git writes the patch directly so PowerShell does not change its encoding.
    Invoke-Tool git (@('diff', '--cached', '--binary', '--full-index', "--output=$Folder/drl.patch", '--', '.') + $Exclusions) $Source
    $Meta = Get-Content -LiteralPath "$Folder/base.json" -Raw | ConvertFrom-Json
    Write-Json "$Source.prepared.json" @{ commit = $Meta.commit; patch = (Get-InputHash $Folder) }
    Write-Host "Exported $Folder/drl.patch"
}

function Get-ArmToolchain {
    if (!$ArmToolchain) { $ArmToolchain = "$env:USERPROFILE/.platformio/packages/toolchain-gccarmnoneeabi" }
    $Arm = [IO.Path]::GetFullPath($ArmToolchain)
    $Compiler = "$Arm/bin/arm-none-eabi-gcc.exe"
    if (!(Test-Path -LiteralPath $Compiler)) { throw 'Set -ArmToolchain to the GNU Arm 7 2017 q4 installation folder.' }
    $Version = & $Compiler --version
    if ($LASTEXITCODE -ne 0 -or ($Version -join "`n") -notmatch '7\.2\.1') { throw 'GNU Arm 7.2.1 is required.' }
    return $Arm
}

function Build-Firmware {
    $Arm = Get-ArmToolchain
    if (($Root + $Arm) -match '\s') { throw 'Betaflight requires repository and Arm toolchain paths without spaces.' }
    $Source = Get-Source 'firmware/betaflight'
    $env:ARM_TOOLCHAIN = $Arm
    $env:PATH = "$Msys/usr/bin;$Msys/ucrt64/bin;$env:PATH"
    $TestDirectory = "$Root/firmware/build/tests"
    [IO.Directory]::CreateDirectory($TestDirectory) | Out-Null
    Invoke-Tool "$Msys/ucrt64/bin/gcc.exe" @(
        '-std=c11', '-Wall', '-Wextra', '-Werror', '-static', '-DBRIDGE_HOST_TEST',
        "-I$Root/firmware/ec/src", "-I$Source/src/main/io",
        "$Root/firmware/ec/tests/bridge_test.c", "$Root/firmware/ec/src/bridge.c",
        "$Root/firmware/ec/src/file_forward.c", "$Root/firmware/ec/src/control_forward.c",
        '-o', "$TestDirectory/ec_bridge.exe"
    )
    Invoke-Tool "$TestDirectory/ec_bridge.exe" @()
    Invoke-Tool "$Msys/ucrt64/bin/gcc.exe" @(
        '-std=c11', '-Wall', '-Wextra', '-Werror', '-static',
        "-I$Root/firmware/ec/src", "$Root/firmware/ec/tests/smartaudio_test.c",
        '-o', "$TestDirectory/ec_smartaudio.exe"
    )
    Invoke-Tool "$TestDirectory/ec_smartaudio.exe" @()
    Invoke-Tool "$Msys/ucrt64/bin/gcc.exe" @(
        '-std=c11', '-Wall', '-Wextra', '-Werror', '-static',
        "-I$Root/firmware/ec/src", "$Root/firmware/ec/tests/receiver_tx_test.c",
        '-o', "$TestDirectory/ec_receiver_tx.exe"
    )
    Invoke-Tool "$TestDirectory/ec_receiver_tx.exe" @()
    foreach ($Board in @('RACER4S', 'RACER4F4')) {
        Invoke-Tool "$Msys/usr/bin/make.exe" @("TARGET=$Board", 'UNAME=MINGW64', "ARM_SDK_DIR=$($Arm.Replace('\', '/'))", '-j4') $Source
    }
    foreach ($Script in @('build_ec.py', 'build_ledc.py', 'package.py')) {
        Invoke-Tool python @("$Root/firmware/tools/$Script")
    }
    Copy-Item -LiteralPath "$Root/LICENSE", "$Root/NOTICE" -Destination "$Root/firmware/build/releases" -Force
    Copy-Item -LiteralPath "$Root/flasher/third_party/betaflight.LICENSE" -Destination "$Root/firmware/build/releases/Betaflight-LICENSE.txt" -Force
}

function Build-Configurator {
    $Source = Get-Source 'configurator'
    $Runtime = [IO.Path]::GetFullPath($ConfiguratorRuntime)
    $Package = Get-Content -LiteralPath "$Runtime/package.json" -Raw | ConvertFrom-Json
    if ($Package.version -ne '10.6.0') { throw 'The runtime must be the official Windows Configurator 10.6.0.' }
    $Destination = "$Root/configurator/build/DRL Configurator"
    if (Test-Path -LiteralPath $Destination) {
        $OutputPath = (Resolve-Path -LiteralPath $Destination).Path
        $ExpectedPath = [IO.Path]::GetFullPath((Join-Path $Root 'configurator/build/DRL Configurator'))
        if ($OutputPath -ne $ExpectedPath) { throw 'Unexpected Configurator output path.' }
        $Entries = @(Get-Item -LiteralPath $OutputPath) + @(Get-ChildItem -LiteralPath $OutputPath -Recurse -Force)
        if ($Entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
            throw 'Configurator output contains a linked directory or file.'
        }
        Remove-Item -LiteralPath $OutputPath -Recurse -Force
    }
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    Get-ChildItem -LiteralPath $Runtime -Force | Where-Object { $_.Name -notmatch '^(unins|uninbf)' } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
    Copy-Tree "$Source/src" $Destination
    Copy-Tree "$Source/resources" "$Destination/resources"
    Copy-Tree "$Source/libraries" "$Destination/js/libraries"
    Copy-Tree "$Source/locales" "$Destination/_locales"
    Copy-Item -LiteralPath "$Source/package.json" -Destination "$Destination/package.json" -Force
    Copy-Item -LiteralPath "$Source/LICENSE" -Destination "$Destination/LICENSE" -Force
    Copy-Item -LiteralPath "$Root/LICENSE" -Destination "$Destination/DRL-LICENSE.txt" -Force
    Copy-Item -LiteralPath "$Root/NOTICE" -Destination "$Destination/DRL-NOTICE.txt" -Force
    Write-Json "$Destination/version.json" @{
        version = '10.6.0'
        gitChangesetId = "DRL $Version $($Revision.Substring(0, 7))" + $(if ($Modified) { ' modified' })
        drlVersion = $Version
        sourceCommit = $Revision
        sourceModified = $Modified
        updateRepository = $UpdateRepository
        updateUseGitHubCLI = [bool]$UseGitHubCLI
    }
    $Output = "$Root/configurator/build/output"
    [IO.Directory]::CreateDirectory($Output) | Out-Null
    $Nsis = "$Root/configurator/build/nsis-3.11"
    if (!(Test-Path -LiteralPath "$Nsis/makensis.exe")) {
        $Archive = "$Root/configurator/build/nsis-3.11.zip"
        Invoke-Tool curl.exe @('--fail', '--location', '--silent', '--show-error', '--proto-redir', '=https',
            'https://downloads.sourceforge.net/project/nsis/NSIS%203/3.11/nsis-3.11.zip', '--output', $Archive)
        if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne 'c7d27f780ddb6cffb4730138cd1591e841f4b7edb155856901cdf5f214394fa1') {
            throw 'NSIS download does not match version 3.11.'
        }
        Expand-Archive -LiteralPath $Archive -DestinationPath "$Root/configurator/build" -Force
    }
    Invoke-Tool "$Nsis/makensis.exe" @(
        '/V2', "/DVERSION=$Version", '/DPLATFORM=win64', "/DAPP_SOURCE=$Destination",
        "/DAPP_OUTPUT=$Output/Betaflight-DRL.exe", "$Source/assets/windows/installer.nsi"
    )
    Write-Host "Built $Output/Betaflight-DRL.exe"
}

function Build-Flasher {
    $Tools = "$Msys/ucrt64/bin"
    $env:PATH = "$Tools;$env:PATH"
    $Destination = "$Root/flasher/build"
    $Source = "$Root/flasher"
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    $Arm = Get-ArmToolchain
    Invoke-Tool "$Arm/bin/arm-none-eabi-gcc.exe" @(
        '-Os', '-mthumb', '-mcpu=cortex-m4', '-ffreestanding', '-fno-builtin', '-fno-pic',
        '-Wall', '-Wextra', '-Werror', '-nostdlib', "-Wl,-T,$Source/src/font_applet.ld",
        '-Wl,--gc-sections', "$Source/src/font_applet.c", '-o', "$Destination/font_applet.elf"
    )
    Invoke-Tool "$Arm/bin/arm-none-eabi-objcopy.exe" @(
        '-O', 'binary', "$Destination/font_applet.elf", "$Destination/font_applet.bin"
    )
    $AppletBytes = [IO.File]::ReadAllBytes("$Destination/font_applet.bin")
    $AppletData = ($AppletBytes | ForEach-Object { '0x{0:x2}' -f $_ }) -join ','
    [IO.File]::WriteAllText("$Destination/font_applet.hpp",
        "#pragma once`nnamespace drl { inline constexpr unsigned char FontApplet[] = {$AppletData}; }`n", $Utf8)
    $ResourceVersion = $Version.Replace('.', ',') + ',0'
    $VersionHeader = @"
#define DRL_VERSION "$Version"
#define DRL_VERSION_RESOURCE $ResourceVersion
#define DRL_SOURCE_COMMIT "$Revision"
#define DRL_SOURCE_MODIFIED $([int]$Modified)
#define DRL_UPDATE_REPOSITORY "$UpdateRepository"
#define DRL_UPDATE_USE_GH $([int][bool]$UseGitHubCLI)
"@
    [IO.File]::WriteAllText("$Destination/release.h", $VersionHeader, $Utf8)
    $Archive = "$Source/third_party/stm32cubeprogrammer-2.22.0.zip"
    $ArchiveHash = '8b880b553ceec9328bb20c90aeff3c5d8f459044a942a37d8788e39d1d626280'
    if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $ArchiveHash) {
        throw 'STM32CubeProgrammer archive does not match the bundled version.'
    }
    $Programmer = [IO.Path]::GetFullPath("$Destination/tools/STM32CubeProgrammer")
    $BuildRoot = [IO.Path]::GetFullPath($Destination) + [IO.Path]::DirectorySeparatorChar
    if (!$Programmer.StartsWith($BuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Programmer folder is outside the build folder.'
    }
    if (Test-Path -LiteralPath $Programmer) { Remove-Item -LiteralPath $Programmer -Recurse -Force }
    Expand-Archive -LiteralPath $Archive -DestinationPath $Programmer
    Invoke-Tool "$Tools/windres.exe" @("$Source/src/app.rc", '-I', $Destination, '-O', 'coff', '-o', "$Destination/app.res") "$Source/src"
    $Flags = @('-std=c++17', '-O2', '-static', '-DUNICODE', '-D_UNICODE', '-D_WIN32_WINNT=0x0A00', '-I', $Destination)
    $Libraries = @('bcrypt', 'setupapi', 'uuid', 'ole32', 'shell32', 'comctl32', 'comdlg32', 'uxtheme', 'gdi32', 'gdiplus', 'dwmapi', 'winhttp') | ForEach-Object { "-l$_" }
    Invoke-Tool "$Tools/g++.exe" ($Flags + @('-mwindows', '-municode', "$Source/src/main.cpp", "$Destination/app.res", '-o', "$Destination/DRL Flasher.exe") + $Libraries)
    foreach ($Program in @(@('pack', 'src/pack.cpp'), @('tests', 'tests/core_tests.cpp'))) {
        Invoke-Tool "$Tools/g++.exe" ($Flags + @("$Source/$($Program[1])", '-lbcrypt', '-o', "$Destination/$($Program[0]).exe"))
    }
    Invoke-Tool "$Destination/pack.exe" @("$Root/firmware", "$Destination/builds/local")
    Get-ChildItem -LiteralPath "$Source/releases" -Filter '*.zip' | ForEach-Object {
        Expand-Archive -LiteralPath $_.FullName -DestinationPath "$Destination/builds/$($_.BaseName)" -Force
    }
    Invoke-Tool "$Destination/tests.exe" @("$Root/firmware", "$Destination/builds/local")
    Invoke-Tool "$Tools/g++.exe" ($Flags + @("$Source/tests/recovery_tests.cpp", '-lbcrypt', '-o', "$Destination/recovery_tests.exe"))
    Invoke-Tool "$Destination/recovery_tests.exe" @("$Root/firmware", "$Destination/builds/local")
    Invoke-Tool "$Tools/g++.exe" ($Flags + @("$Source/tests/report_tests.cpp", '-o', "$Destination/report_tests.exe") + $Libraries)
    Invoke-Tool "$Destination/report_tests.exe" @("$Destination/builds/local")
    Invoke-Tool "$Tools/g++.exe" ($Flags + @("$Source/tests/runtime_tests.cpp", '-o', "$Destination/runtime_tests.exe") + $Libraries)
    $RuntimePath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        Invoke-Tool "$Destination/runtime_tests.exe" @()
    }
    finally { $env:PATH = $RuntimePath }
    [IO.Directory]::CreateDirectory("$Destination/licenses") | Out-Null
    Copy-Item -LiteralPath "$Source/third_party/json.LICENSE" -Destination "$Destination/licenses/json.LICENSE" -Force
    Copy-Item -LiteralPath "$Source/third_party/betaflight.LICENSE" -Destination "$Destination/licenses/betaflight.LICENSE" -Force
    Copy-Item -LiteralPath "$Root/LICENSE", "$Root/NOTICE" -Destination $Destination -Force
    [IO.Directory]::CreateDirectory("$Source/dist") | Out-Null
    $PackageFiles = @("$Destination/DRL Flasher.exe", "$Destination/builds", "$Destination/tools", "$Destination/licenses", "$Destination/LICENSE", "$Destination/NOTICE")
    Compress-Archive -LiteralPath $PackageFiles -DestinationPath "$Source/dist/DRL Flasher.zip" -CompressionLevel Optimal -Force
    Write-Host "Built $Destination/DRL Flasher.exe"
    Write-Host "Packaged $Source/dist/DRL Flasher.zip"
}

$PreviousPath = $env:PATH
$PreviousArm = $env:ARM_TOOLCHAIN
try {
    switch ($Target) {
        'prepare' {
            Get-Source 'firmware/betaflight' | Out-Host
            Get-Source 'configurator' | Out-Host
            Prepare-Ghidra
        }
        'export' { Export-Patch 'firmware/betaflight'; Export-Patch 'configurator' }
        'firmware' { Build-Firmware }
        'configurator' { Build-Configurator }
        'flasher' { Build-Flasher }
        'all' { Build-Firmware; Build-Configurator; Build-Flasher }
    }
}
finally {
    $env:PATH = $PreviousPath
    $env:ARM_TOOLCHAIN = $PreviousArm
}
