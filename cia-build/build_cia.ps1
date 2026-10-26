$ErrorActionPreference = 'Stop'

$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ciaRoot = $PSScriptRoot
$tools = Join-Path $ciaRoot 'tools'
$meta = Join-Path $ciaRoot 'meta'
$work = Join-Path $ciaRoot 'work'
$output = Join-Path $project 'twviewer.cia'

$devkitProRoot = 'C:\devkitPro'
$gcc = Join-Path $devkitProRoot 'devkitARM\bin\arm-none-eabi-gcc.exe'
$libctru = Join-Path $devkitProRoot 'libctru'
$portlibs = Join-Path $devkitProRoot 'portlibs\3ds'

function Find-Tool([string]$name) {
    $local = Join-Path $tools $name
    if (Test-Path -LiteralPath $local) { return $local }
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

$makerom = Find-Tool 'makerom.exe'
$bannertool = Find-Tool 'bannertool.exe'
$missing = @()
if (-not (Test-Path -LiteralPath $gcc)) { $missing += 'devkitARM arm-none-eabi-gcc.exe' }
if (-not $makerom) { $missing += 'makerom.exe' }
if (-not $bannertool) { $missing += 'bannertool.exe' }
if ($missing.Count) {
    throw "Missing tools: $($missing -join ', '). CIA packagers belong in $tools."
}

New-Item -ItemType Directory -Force -Path $work | Out-Null
$flags = @(
    '-g', '-Wall', '-O2', '-fomit-frame-pointer', '-ffunction-sections',
    '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
    '-D__3DS__', '-DNEW3DS',
    ('-I' + (Join-Path $project 'include')),
    ('-I' + (Join-Path $project 'library\include')),
    ('-I' + (Join-Path $libctru 'include')),
    ('-I' + (Join-Path $portlibs 'include'))
)

foreach ($unit in @('main', 'audio', 'video')) {
    & $gcc @flags '-c' (Join-Path $project "source\$unit.c") '-o' (Join-Path $work "$unit.o")
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed for $unit.c ($LASTEXITCODE)" }
}

$elf = Join-Path $work 'twviewer.elf'
$linkArgs = @(
    '-specs=3dsx.specs', '-g', '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
    ('-Wl,-Map,' + (Join-Path $work 'twviewer.map')),
    (Join-Path $work 'main.o'), (Join-Path $work 'audio.o'), (Join-Path $work 'video.o'),
    ('-L' + (Join-Path $project 'library\lib')),
    ('-L' + (Join-Path $libctru 'lib')),
    ('-L' + (Join-Path $portlibs 'lib')),
    '-lcitro2d', '-lcitro3d', '-lavcodec', '-lavutil', '-lswresample', '-lx264', '-lmp3lame', '-ldav1d',
    '-lcurl', '-lmbedtls', '-lmbedx509', '-lmbedcrypto', '-lz', '-lctru', '-lm',
    '-o', $elf
)
& $gcc @linkArgs
if ($LASTEXITCODE -ne 0) { throw "ELF link failed ($LASTEXITCODE)" }

$banner = Join-Path $work 'banner.bnr'
$icon = Join-Path $work 'icon.icn'
& $bannertool makebanner -i (Join-Path $meta 'banner.png') -a (Join-Path $meta 'banner.wav') -o $banner
if ($LASTEXITCODE -ne 0) { throw "bannertool makebanner failed ($LASTEXITCODE)" }
& $bannertool makesmdh -s 'TwViewer' -l 'Twitch viewer for New 3DS' -p 'gamerboyrts' -i (Join-Path $meta 'icon.png') -o $icon
if ($LASTEXITCODE -ne 0) { throw "bannertool makesmdh failed ($LASTEXITCODE)" }

Push-Location $project
try {
    & $makerom -f cia -o $output -target t -exefslogo `
        -elf $elf -rsf (Join-Path $ciaRoot 'app.rsf') -banner $banner -icon $icon `
        '-DAPP_TITLE=TwViewer' '-DAPP_PRODUCT_CODE=CTR-TWVR' '-DAPP_UNIQUE_ID=0xECA11' `
        '-DAPP_ROMFS=romfs' '-DAPP_VERSION_MAJOR=1' -ver 1
    if ($LASTEXITCODE -ne 0) { throw "makerom failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}

$result = Get-Item -LiteralPath $output
Write-Output "Built $($result.FullName) ($($result.Length) bytes)"
