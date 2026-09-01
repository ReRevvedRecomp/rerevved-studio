param(
    [ValidateSet('win-amd64-debug', 'win-amd64-release', 'linux-amd64-debug')]
    [string]$Preset = 'win-amd64-debug',
    [switch]$SkipAppSmoke
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$requiredClangFormatMajor = 22
$clangFormat = Get-Command clang-format -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $clangFormat) {
    throw "clang-format $requiredClangFormatMajor.x is required but was not found in PATH."
}
$versionOutput = @(& $clangFormat.Source --version 2>&1)
$versionExitCode = $LASTEXITCODE
$versionText = ($versionOutput -join ' ').Trim()
$versionMatch = [regex]::Match($versionText, '(?i)\bclang-format version (?<version>\d+\.\d+\.\d+)(?=\s|$|\()')
if ($versionExitCode -ne 0) {
    throw "clang-format $requiredClangFormatMajor.x version query failed at $($clangFormat.Source): $versionText"
}
if (-not $versionMatch.Success -or -not $versionMatch.Groups['version'].Value.StartsWith("$requiredClangFormatMajor.")) {
    $reportedVersion = if ($versionMatch.Success) { $versionMatch.Groups['version'].Value } else { 'unknown' }
    throw "clang-format $requiredClangFormatMajor.x is required; found $reportedVersion at $($clangFormat.Source)."
}

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
    & $clangFormat.Source --dry-run --Werror $sources
    if ($LASTEXITCODE -ne 0) { throw 'clang-format check failed.' }

    & git diff --check
    if ($LASTEXITCODE -ne 0) { throw 'git diff --check failed.' }
}
finally {
    Pop-Location
}

$smokeStatus = if ($SkipAppSmoke) { 'app-smoke-skipped' } else { 'app-smoke' }
Write-Host "verify: passed configure build tests $smokeStatus format diff-check"
