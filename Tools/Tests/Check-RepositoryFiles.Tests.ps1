[CmdletBinding()]
param(
    [string]$TemporaryRoot = [IO.Path]::GetTempPath()
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$checker = Join-Path $projectRoot 'Tools/Check-RepositoryFiles.ps1'
$pwsh = Join-Path $PSHOME 'pwsh.exe'
$testRoot = Join-Path ([IO.Path]::GetFullPath($TemporaryRoot)) ('RepositoryFileCheck-' + [Guid]::NewGuid().ToString('N'))
$fixture = Join-Path $testRoot 'Repository'
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$encoding = [Text.UTF8Encoding]::new($false)
function Write-FixtureFile([string]$Path, [string]$Contents) {
    $target = Join-Path $fixture $Path
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    [IO.File]::WriteAllText($target, $Contents, $encoding)
}
function Invoke-Checker([string]$Label, [int]$ExpectedExitCode) {
    $reportPath = Join-Path $testRoot ($Label + '.json')
    $logPath = Join-Path $testRoot ($Label + '.log')
    & $pwsh -NoProfile -File $checker -RepositoryRoot $fixture -ReportPath $reportPath *> $logPath
    if ($LASTEXITCODE -ne $ExpectedExitCode) {
        Get-Content -LiteralPath $logPath
        throw "Unexpected checker exit code for $Label : $LASTEXITCODE"
    }
    return [IO.File]::ReadAllText($reportPath) | ConvertFrom-Json
}
Copy-Item -LiteralPath (Join-Path $projectRoot '.gitignore') -Destination (Join-Path $fixture '.gitignore')
Write-FixtureFile 'Config/RepositoryFilePolicy.json' ([IO.File]::ReadAllText((Join-Path $projectRoot 'Config/RepositoryFilePolicy.json')))
Write-FixtureFile 'Resources/test.obj' 'v 0 0 0'
Write-FixtureFile 'Source/Engine/Debug/Test.cpp' '// Source folders named Debug must remain allowed.'
Write-FixtureFile 'Archive/ImGuiExamples_1.89.4/examples/libs/glfw/lib-vc2010-32/glfw3.lib' 'Fixture for an explicitly allowed third-party library.'
Write-FixtureFile 'x64/Debug/untracked.pdb' 'Ignored local output; it must not be reported.'
& git -C $fixture init --quiet
if ($LASTEXITCODE -ne 0) { throw 'Fixture git init failed.' }
& git -C $fixture add -- .
if ($LASTEXITCODE -ne 0) { throw 'Fixture git add failed.' }
$clean = Invoke-Checker 'clean' 0
if ($clean.findingCount -ne 0) { throw 'Valid models, Debug source or vendor libraries were rejected.' }

Write-FixtureFile 'Generated/日本語 フォルダ/accidental.pdb' 'Accidentally tracked debugger output.'
Write-FixtureFile 'Generated/test.obj' 'A compiler object outside Resources must be rejected.'
& git -C $fixture add -f -- 'Generated/日本語 フォルダ/accidental.pdb' 'Generated/test.obj'
if ($LASTEXITCODE -ne 0) { throw 'Fixture force-add failed.' }
$bad = Invoke-Checker 'bad-files' 1
$badPaths = @($bad.findings | ForEach-Object path)
if ($bad.findingCount -ne 2 -or
    'Generated/日本語 フォルダ/accidental.pdb' -notin $badPaths -or
    'Generated/test.obj' -notin $badPaths) {
    throw 'The checker did not detect both tracked generated files.'
}
& git -C $fixture rm --cached --quiet -- 'Generated/日本語 フォルダ/accidental.pdb' 'Generated/test.obj'
if ($LASTEXITCODE -ne 0) { throw 'Fixture untracking failed.' }
$repaired = Invoke-Checker 'repaired' 0
if ($repaired.findingCount -ne 0) { throw 'Untracked outputs were incorrectly reported.' }
if (!(Test-Path -LiteralPath (Join-Path $fixture 'Generated/日本語 フォルダ/accidental.pdb'))) {
    throw 'Fixture file was unexpectedly deleted.'
}
Write-Output 'Repository file checker tests passed: valid inputs, tracked generated files, Unicode paths, and untracked local output.'
Write-Output ("Test reports: " + $testRoot)
