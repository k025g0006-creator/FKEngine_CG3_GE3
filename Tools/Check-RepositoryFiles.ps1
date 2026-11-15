[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..'),
    [string]$ReportPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$policyPath = Join-Path $RepositoryRoot 'Config/RepositoryFilePolicy.json'
if (!(Test-Path -LiteralPath $policyPath -PathType Leaf)) {
    throw "Repository file policy not found: $policyPath"
}
$policy = [IO.File]::ReadAllText($policyPath) | ConvertFrom-Json
$allowedFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $policy.allowedFiles) {
    if ([string]::IsNullOrWhiteSpace($entry.path) -or [string]::IsNullOrWhiteSpace($entry.reason)) {
        throw 'Each allowed file must have an exact path and a reason.'
    }
    $allowedFiles.Add($entry.path.Replace('\', '/')) | Out-Null
}

function Get-TrackedPaths([string[]]$ExtraArguments = @()) {
    $output = @(& git -C $RepositoryRoot -c core.quotepath=false ls-files --cached @ExtraArguments -z)
    if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed.' }
    return ([string]::Join([char]10, $output)).Split([char]0, [StringSplitOptions]::RemoveEmptyEntries)
}

$trackedFiles = @(Get-TrackedPaths)
$ignoredFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($path in @(Get-TrackedPaths -ExtraArguments @('--ignored', '--exclude-standard'))) {
    $ignoredFiles.Add($path) | Out-Null
}
$findings = [Collections.Generic.List[object]]::new()
foreach ($path in $trackedFiles) {
    if ($allowedFiles.Contains($path)) { continue }
    $reasons = [Collections.Generic.List[string]]::new()
    if ($ignoredFiles.Contains($path)) {
        $reasons.Add('Git ignore rules exclude this file, but it is still tracked.')
    }
    if ($path -match '(?i)(^|/)(\.vs|ipch|x64|x86|arm64|build|bin|obj|logs|dumps|NuGetCache|packages|node_modules|__pycache__)(/|$)' -or
        $path -match '(?i)(^|/)Shaders/Compiled(/|$)') {
        $reasons.Add('Generated output or cache directory.')
    }
    $isModel = $path -match '(?i)(^|/)Resources/.+\.obj$'
    if (!$isModel -and $path -match '(?i)\.(obj|o|exe|dll|lib|a|pdb|ilk|idb|pch|iobj|ipdb|exp|cso|dxil|tlog|log|dmp|user|suo|sdf|opensdf|ncb|cache|binlog|nupkg|snupkg|VC\.db|opendb)$') {
        $reasons.Add('Build result, debugger data or personal IDE file.')
    }
    if ($reasons.Count -gt 0) {
        $findings.Add([pscustomobject]@{ path = $path; reasons = @($reasons.ToArray()) })
    }
}
$report = [pscustomobject]@{
    trackedFileCount = $trackedFiles.Count
    findingCount = $findings.Count
    findings = @($findings.ToArray())
}
if ($ReportPath) {
    $absoluteReportPath = [IO.Path]::GetFullPath($ReportPath)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($absoluteReportPath)) | Out-Null
    [IO.File]::WriteAllText($absoluteReportPath, ($report | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
}
if ($findings.Count -gt 0) {
    foreach ($finding in $findings) {
        Write-Output ($finding.path + ' : ' + ($finding.reasons -join ' '))
    }
    throw ("Unnecessary tracked files found: " + $findings.Count + ". Remove generated files from Git tracking; keep required inputs.")
}
Write-Output ("Repository file check passed: " + $trackedFiles.Count + ' tracked files; no unnecessary files found.')
