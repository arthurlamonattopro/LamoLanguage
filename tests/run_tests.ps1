$ErrorActionPreference = "Stop"

function Assert-Success($ExitCode, $Context) {
    if ($ExitCode -ne 0) {
        throw "$Context failed with exit code $ExitCode"
    }
}

function Assert-Failure($ExitCode, $Context) {
    if ($ExitCode -eq 0) {
        throw "$Context unexpectedly succeeded"
    }
}

function Invoke-Lamo {
    param(
        [string[]]$Arguments,
        [switch]$CaptureOutput
    )

    $binary = Join-Path $PSScriptRoot "..\\lamo.exe"
    if (-not (Test-Path $binary)) {
        $binary = Join-Path $PSScriptRoot "..\\lamo"
    }

    if ($CaptureOutput) {
        $oldPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $output = & $binary @Arguments 2>&1
        $ErrorActionPreference = $oldPreference
        return @{
            ExitCode = $LASTEXITCODE
            Output = ($output -join "`n")
        }
    }

    & $binary @Arguments
    return @{
        ExitCode = $LASTEXITCODE
        Output = ""
    }
}

$validFiles = Get-ChildItem (Join-Path $PSScriptRoot "fixtures\\valid") -Filter *.lamo | Sort-Object Name
foreach ($file in $validFiles) {
    $result = Invoke-Lamo -Arguments @("check", $file.FullName)
    Assert-Success $result.ExitCode "check $($file.Name)"
}

$invalidFiles = Get-ChildItem (Join-Path $PSScriptRoot "fixtures\\invalid") -Filter *.lamo | Sort-Object Name
foreach ($file in $invalidFiles) {
    $result = Invoke-Lamo -Arguments @("check", $file.FullName) -CaptureOutput
    Assert-Failure $result.ExitCode "invalid check $($file.Name)"
}

$runResult = Invoke-Lamo -Arguments @("run", (Join-Path $PSScriptRoot "fixtures\\valid\\e2e_run.lamo")) -CaptureOutput
Assert-Success $runResult.ExitCode "run e2e_run.lamo"

$expectedRunOutput = @(
    "fib",
    "8",
    "6",
    "1",
    "1"
) -join "`n"

if ($runResult.Output -notmatch [regex]::Escape($expectedRunOutput)) {
    throw "unexpected program output:`n$($runResult.Output)"
}

$buildOutput = Join-Path $PSScriptRoot "artifacts\\lamo_sample"
New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot "artifacts") | Out-Null
$buildResult = Invoke-Lamo -Arguments @("build", (Join-Path $PSScriptRoot "fixtures\\valid\\e2e_run.lamo"), "-o", $buildOutput)
Assert-Success $buildResult.ExitCode "build e2e_run.lamo"

$helpResult = Invoke-Lamo -Arguments @("help") -CaptureOutput
Assert-Success $helpResult.ExitCode "help"
if ($helpResult.Output -notmatch "run <file.lamo>") {
    throw "help output did not include CLI usage"
}

$versionResult = Invoke-Lamo -Arguments @("version") -CaptureOutput
Assert-Success $versionResult.ExitCode "version"
if ($versionResult.Output.Trim() -ne "2.0") {
    throw "unexpected version output: $($versionResult.Output)"
}

$crlfPath = Join-Path $PSScriptRoot "fixtures\\generated_crlf.lamo"
$crlfSource = @(
    "let value = 5;",
    "fn inc(n) {",
    "    return n + 1;",
    "}",
    "print(inc(value));"
) -join "`r`n"
[System.IO.File]::WriteAllText($crlfPath, $crlfSource)
$crlfResult = Invoke-Lamo -Arguments @("check", $crlfPath)
Assert-Success $crlfResult.ExitCode "check generated CRLF fixture"
Remove-Item $crlfPath -Force

$multiDir = Join-Path $PSScriptRoot "fixtures\\generated_multi"
New-Item -ItemType Directory -Force -Path $multiDir | Out-Null
$multiLibPath = Join-Path $multiDir "math.lamo"
$multiMainPath = Join-Path $multiDir "main.lamo"

[System.IO.File]::WriteAllText($multiLibPath, @"
fn add(a, b) {
    return a + b;
}
"@)

[System.IO.File]::WriteAllText($multiMainPath, @'
import "math.lamo";

let base = 5;
print(add(base, 7));
'@)

$multiCheckResult = Invoke-Lamo -Arguments @("check", $multiMainPath) -CaptureOutput
Assert-Success $multiCheckResult.ExitCode "check imported multi-file program"
if ($multiCheckResult.Output -notmatch [regex]::Escape("check passed: $multiMainPath")) {
    throw "unexpected multi-file check output:`n$($multiCheckResult.Output)"
}

$multiRunResult = Invoke-Lamo -Arguments @("run", $multiMainPath) -CaptureOutput
Assert-Success $multiRunResult.ExitCode "run imported multi-file program"
if ($multiRunResult.Output -notmatch "(?m)^12$") {
    throw "unexpected multi-file run output:`n$($multiRunResult.Output)"
}

$multiBuildOutput = Join-Path $PSScriptRoot "artifacts\\lamo_multi"
$multiBuildResult = Invoke-Lamo -Arguments @("build", $multiMainPath, "-o", $multiBuildOutput)
Assert-Success $multiBuildResult.ExitCode "build imported multi-file program"

Remove-Item $multiLibPath, $multiMainPath -Force
Remove-Item $multiDir -Force

$cycleDir = Join-Path $PSScriptRoot "fixtures\\generated_cycle"
New-Item -ItemType Directory -Force -Path $cycleDir | Out-Null
$cycleAPath = Join-Path $cycleDir "a.lamo"
$cycleBPath = Join-Path $cycleDir "b.lamo"

[System.IO.File]::WriteAllText($cycleAPath, @'
import "b.lamo";

fn alpha() {
    return 1;
}
'@)

[System.IO.File]::WriteAllText($cycleBPath, @'
import "a.lamo";

fn beta() {
    return 2;
}
'@)

$cycleCheckResult = Invoke-Lamo -Arguments @("check", $cycleAPath) -CaptureOutput
Assert-Failure $cycleCheckResult.ExitCode "check imported cycle program"
if ($cycleCheckResult.Output -notmatch "import cycle detected") {
    throw "missing import cycle diagnostic:`n$($cycleCheckResult.Output)"
}

Remove-Item $cycleAPath, $cycleBPath -Force
Remove-Item $cycleDir -Force

Write-Host "All tests passed."
