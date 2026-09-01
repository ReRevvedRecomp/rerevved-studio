param(
    [ValidateSet('win-amd64-debug', 'win-amd64-release', 'linux-amd64-debug')]
    [string]$Preset = 'win-amd64-debug',
    [switch]$SkipAppSmoke
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

Push-Location $repoRoot
try {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

    & ctest --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

    if (-not $SkipAppSmoke) {
        $executableName = if ($env:OS -eq 'Windows_NT') {
            'rerevved-studio.exe'
        }
        else {
            'rerevved-studio'
        }
        $executable = Join-Path $repoRoot "out/build/$Preset/$executableName"
        & $executable --smoke-test (Join-Path $repoRoot 'README.md')
        if ($LASTEXITCODE -ne 0) { throw 'Application smoke test failed.' }
    }

    $sources = Get-ChildItem -Path 'api', 'src', 'tests' -Recurse -File |
        Where-Object { $_.Extension -in '.cpp', '.h' } |
        ForEach-Object { $_.FullName }
    & clang-format --dry-run --Werror $sources
    if ($LASTEXITCODE -ne 0) { throw 'clang-format check failed.' }

    & git diff --check
    if ($LASTEXITCODE -ne 0) { throw 'git diff --check failed.' }
}
finally {
    Pop-Location
}

$smokeStatus = if ($SkipAppSmoke) { 'app-smoke-skipped' } else { 'app-smoke' }
Write-Host "verify: passed configure build tests $smokeStatus format diff-check"
