param(
    [Parameter(Mandatory)][string]$DeploymentDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$')][string]$Version,
    [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{40}$')][string]$SourceCommit,
    [Parameter(Mandatory)][string]$QtVersion,
    [Parameter(Mandatory)][string]$CompilerVersion
)

$ErrorActionPreference = 'Stop'
$deployment = (Resolve-Path -LiteralPath $DeploymentDirectory).Path.TrimEnd('\', '/')
$output = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\', '/')
if ($output.Equals($deployment, [StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($deployment + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package output must be outside the deployment directory.'
}
$required = @('hatteda.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
    'plugins/platforms/qwindows.dll', 'LICENSE', 'README.md', 'THIRD_PARTY_LICENSES.md')
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $deployment $name) -PathType Leaf)) {
        throw "Required release file is missing: $name"
    }
}
if (Test-Path -LiteralPath (Join-Path $deployment 'plugins/platforms/qoffscreen.dll')) {
    throw 'The offscreen test plugin must be removed before packaging.'
}
# Router distributions need corresponding source/runtime notices. This CI package deliberately
# omits optional binaries; use the separately documented router packaging process when bundling.
if (Test-Path -LiteralPath (Join-Path $deployment 'autorouter')) {
    throw 'This package script supports the core bundle only; optional router bundles need separate source/notices validation.'
}
$manifestPath = Join-Path $deployment 'release-manifest.json'
$archiveName = "HattEDA-$Version-windows-x64.zip"
$archivePath = Join-Path $output $archiveName
$checksumPath = $archivePath + '.sha256'
foreach ($path in @($manifestPath, $archivePath, $checksumPath)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite existing release output: $path" }
}
$entries = @(Get-ChildItem -LiteralPath $deployment -Recurse -Force)
if ($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
    throw 'Release contents must not contain symbolic links or reparse points.'
}
$files = @($entries | Where-Object { -not $_.PSIsContainer } | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($deployment.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$manifest = [ordered]@{
    schemaVersion = 1
    version = $Version
    sourceCommit = $SourceCommit.ToLowerInvariant()
    platform = 'windows-x64'
    qtVersion = $QtVersion
    compilerVersion = $CompilerVersion
    optionalRouterBundled = $false
    files = $files
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$createdManifest = $false
$createdArchive = $false
try {
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
    $createdManifest = $true
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $createdArchive = $true
    [IO.Compression.ZipFile]::CreateFromDirectory($deployment, $archivePath)
    $checksum = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($checksumPath, "$checksum  $archiveName`n", [Text.UTF8Encoding]::new($false))
    [PSCustomObject]@{ Archive = $archivePath; Checksum = $checksumPath; Manifest = $manifestPath }
} catch {
    if ($createdManifest) { Remove-Item -LiteralPath $manifestPath -Force }
    if ($createdArchive -and (Test-Path -LiteralPath $archivePath)) { Remove-Item -LiteralPath $archivePath -Force }
    if (Test-Path -LiteralPath $checksumPath) { Remove-Item -LiteralPath $checksumPath -Force }
    throw
}
