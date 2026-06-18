# Local CI for worker/sunshine — mirrors .github/workflows/ci.yml
param(
    [ValidateSet('default', 'all', 'test', 'build', 'configure')]
    [string]$Stage = 'default',
    [switch]$SkipBuild,
    [switch]$SkipSubmodules,
    [string]$BuildDir = 'build'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message"
}

function Ensure-Msys2 {
    if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
        throw "MSYS2 MINGW64 toolchain not found. Open an MSYS2 MINGW64 shell or add gcc to PATH."
    }
}

function Initialize-Submodules {
    if ($SkipSubmodules) { return }
    $marker = Join-Path $Root 'third-party/build-deps/dist/Windows-AMD64/lib/libavcodec.a'
    if (-not (Test-Path $marker)) {
        Write-Step 'initializing git submodules'
        git submodule update --init --recursive
    }
}

function Invoke-Configure {
    Ensure-Msys2
    Initialize-Submodules
    Write-Step 'cmake configure'
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=Release -G Ninja -DBUILD_TESTS=ON
}

function Invoke-Tests {
    if (-not (Get-Command gcc -ErrorAction SilentlyContinue) -and -not (Get-Command clang++ -ErrorAction SilentlyContinue)) {
        throw "A C++ compiler is required for unit tests."
    }
    $isMsys = $env:MSYSTEM -match 'MINGW|MSYS'
    if (-not $isMsys) {
        Write-Step 'standalone IVSHMEM protocol tests (non-Windows)'
        $standalone = "${BuildDir}-standalone"
        cmake -S tests/standalone -B $standalone
        cmake --build $standalone
        & (Join-Path $standalone 'test_ivshmem_protocol.exe')
        if (-not $?) { & (Join-Path $standalone 'test_ivshmem_protocol') }
        return
    }
    if (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) {
        Invoke-Configure
    }
    Write-Step 'build unit tests'
    cmake --build $BuildDir --target test_ivshmem_protocol
    Write-Step 'run IVSHMEM protocol unit tests'
    $exe = Join-Path $BuildDir 'tests/test_ivshmem_protocol.exe'
    if (-not (Test-Path $exe)) {
        $exe = Join-Path $BuildDir 'tests/test_ivshmem_protocol'
    }
    if (-not (Test-Path $exe)) {
        Push-Location $BuildDir
        ctest --output-on-failure -R ivshmem_protocol
        Pop-Location
        return
    }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "unit tests failed with exit code $LASTEXITCODE" }
}

function Invoke-Build {
    if ($env:MSYSTEM -notmatch 'MINGW|MSYS') {
        Write-Step 'skip sunshine.exe build on non-Windows'
        return
    }
    Ensure-Msys2
    if ($SkipBuild) {
        Write-Step 'skip build (SkipBuild)'
        return
    }
    if (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) {
        Invoke-Configure
    }
    Write-Step 'build sunshine'
    cmake --build $BuildDir --target sunshine
}

switch ($Stage) {
    'configure' { Invoke-Configure }
    'test' { Invoke-Tests }
    'build' { Invoke-Configure; Invoke-Build }
    'all' { Invoke-Tests; Invoke-Build }
    default { Invoke-Tests; Invoke-Build }
}

Write-Step 'local CI finished OK'
