[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Debug', 'Development', 'Release')]
    [string]$Configuration,

    [Parameter(Mandatory)]
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputDirectory = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/') + '\'
if ($outputDirectory.StartsWith($repositoryRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputRoot must be outside the source checkout.'
}
$logDirectory = Join-Path $outputDirectory 'Logs'
[IO.Directory]::CreateDirectory($logDirectory) | Out-Null

# Fail early when a required source, model or texture was omitted from the repository.
foreach ($relativeProject in @('FKAlpha.vcxproj', 'externals\DirectXTex\DirectXTex.vcxproj', 'externals\imgui\imgui.vcxproj')) {
    $projectPath = Join-Path $repositoryRoot $relativeProject
    [xml]$project = [IO.File]::ReadAllText($projectPath)
    foreach ($item in $project.SelectNodes('/*[local-name()="Project"]/*[local-name()="ItemGroup"]/*[@Include]')) {
        if ($item -isnot [Xml.XmlElement] -or !$item.HasAttribute('Include')) { continue }
        if ($item.LocalName -notin @('ClCompile', 'ClInclude', 'None', 'FxCompile', 'Text', 'ProjectReference')) { continue }
        $path = Join-Path ([IO.Path]::GetDirectoryName($projectPath)) $item.GetAttribute('Include')
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing project input: $path" }
    }
}
$sources = @(
    Get-ChildItem (Join-Path $repositoryRoot 'Source') -Recurse -File -Include *.cpp, *.h
    Get-ChildItem (Join-Path $repositoryRoot 'Resources\Shaders') -File
)
foreach ($source in $sources) {
    if (Select-String -LiteralPath $source.FullName -Pattern '^(<<<<<<<|=======|>>>>>>>)' -Quiet) {
        throw "Unresolved merge conflict: $($source.FullName)"
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere.exe was not found.' }
$installations = @(& $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json -utf8 | ConvertFrom-Json)
$installation = $installations | Where-Object { [version]$_.installationVersion -ge [version]'18.0' } |
    Sort-Object { [version]$_.installationVersion } -Descending | Select-Object -First 1
if (!$installation) { throw 'Visual Studio 2026 with the C++ v145 toolset is required.' }
$msbuild = Join-Path $installation.installationPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (!(Test-Path -LiteralPath $msbuild)) { throw "MSBuild was not found: $msbuild" }
$applicationProject = Join-Path $repositoryRoot 'FKAlpha.vcxproj'
$buildProperties = @(
    "/p:Configuration=$Configuration"
    '/p:Platform=x64'
    '/p:PreferredToolArchitecture=x64'
    "/p:FKEngineOutputRoot=$outputDirectory"
)

Push-Location $repositoryRoot
try {
    & $msbuild $applicationProject @buildProperties /m:1 /nr:false /nologo '/clp:ErrorsOnly;Summary' /fl "/flp:logfile=$logDirectory\Build-$Configuration.log;verbosity=normal;encoding=UTF-8"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed for $Configuration (exit $LASTEXITCODE)." }

    # Use the Windows SDK selected by the project, rather than a machine-specific SDK path.
    $sdkJson = & $msbuild $applicationProject @buildProperties /nologo /v:quiet '-getProperty:WindowsSdkDir,TargetPlatformVersion'
    if ($LASTEXITCODE -ne 0) { throw 'Could not evaluate the Windows SDK configuration.' }
    $sdk = ($sdkJson -join "`n" | ConvertFrom-Json).Properties
    $dxc = Join-Path $sdk.WindowsSdkDir "bin\$($sdk.TargetPlatformVersion)\x64\dxc.exe"
    if (!(Test-Path -LiteralPath $dxc)) { throw "DXC was not found: $dxc" }
    $shaderOutput = Join-Path $outputDirectory "ShaderChecks\$Configuration"
    [IO.Directory]::CreateDirectory($shaderOutput) | Out-Null
    foreach ($shader in Get-ChildItem (Join-Path $repositoryRoot 'Resources\Shaders') -Filter *.hlsl -File) {
        $profile = if ($shader.Name -like '*.VS.hlsl') { 'vs_6_0' } else { 'ps_6_0' }
        & $dxc -E main -T $profile -Zpr -WX -I $shader.DirectoryName -Fo (Join-Path $shaderOutput ($shader.Name + '.dxil')) $shader.FullName
        if ($LASTEXITCODE -ne 0) { throw "Shader compilation failed: $($shader.Name)" }
    }

    $runtimeDirectory = Join-Path $outputDirectory "Bin\x64\$Configuration"
    foreach ($name in @('FKAlpha.exe', 'dxcompiler.dll', 'dxil.dll', 'Config\imgui.ini')) {
        if (!(Test-Path -LiteralPath (Join-Path $runtimeDirectory $name) -PathType Leaf)) {
            throw "Missing runtime file: $name"
        }
    }
    foreach ($resource in Get-ChildItem (Join-Path $repositoryRoot 'Resources') -Recurse -File | Where-Object Extension -ne '.blend') {
        $relative = [IO.Path]::GetRelativePath((Join-Path $repositoryRoot 'Resources'), $resource.FullName)
        $copy = Join-Path $runtimeDirectory "Resources\$relative"
        if (!(Test-Path -LiteralPath $copy) -or (Get-FileHash -LiteralPath $copy).Hash -ne (Get-FileHash -LiteralPath $resource.FullName).Hash) {
            throw "Missing or outdated runtime resource: $relative"
        }
    }

    Write-Output "${Configuration}: C++ build, 4 shaders and runtime resource checks passed."
} finally {
    Pop-Location
}
