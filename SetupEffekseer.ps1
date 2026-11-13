param(
    [Parameter(Mandatory)]
    [string]$SdkSource,
    [string]$CMake = 'cmake'
)

$ErrorActionPreference = 'Stop'
$SdkSource = (Resolve-Path -LiteralPath $SdkSource).Path
if (!(Test-Path -LiteralPath (Join-Path $SdkSource 'src/EffekseerRendererDX12/CMakeLists.txt'))) {
    throw 'Specify the EffekseerForCpp folder containing CMakeLists.txt and src.'
}

if ($CMake -eq 'cmake' -and !(Get-Command cmake -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $CMake = & $vswhere -latest -products '*' -find 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' |
        Select-Object -First 1
    if (!$CMake) {
        throw 'CMake was not found. Pass its path with -CMake.'
    }
}

$build = Join-Path $PSScriptRoot 'x64/EffekseerRuntimeBuild'
$install = Join-Path $PSScriptRoot 'x64/EffekseerSDK'
$configureArgs = @(
    '-S', $SdkSource, '-B', $build, '-G', 'Visual Studio 18 2026', '-A', 'x64',
    '-DBUILD_EXAMPLES=OFF', '-DBUILD_DX9=OFF', '-DBUILD_DX11=OFF', '-DBUILD_GL=OFF', '-DBUILD_VULKAN=OFF',
    '-DUSE_MSVC_RUNTIME_LIBRARY_DLL=ON', '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW', '-DCMAKE_DEBUG_POSTFIX=d',
    "-DCMAKE_INSTALL_PREFIX=$install"
)
& $CMake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'Effekseer configuration failed.' }

foreach ($configuration in 'Debug', 'Release') {
    & $CMake --build $build --config $configuration --target INSTALL --parallel 5 -- /p:TrackFileAccess=false
    if ($LASTEXITCODE -ne 0) { throw "Effekseer $configuration build failed." }
}

# Effekseer 1.80.8 installs a header that refers to the source-tree layout.
$header = Join-Path $install 'include/EffekseerRendererLLGI/Common.h'
$sourceInclude = '../EffekseerRendererCommon/EffekseerRendererCommon/EffekseerRenderer.Renderer.h'
$installedInclude = 'EffekseerRendererCommon/EffekseerRenderer.Renderer.h'
$contents = [IO.File]::ReadAllText($header)
if ($contents.Contains($sourceInclude)) {
    [IO.File]::WriteAllText($header, $contents.Replace($sourceInclude, $installedInclude))
}
