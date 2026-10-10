param([Parameter(Mandatory)][string]$PackageScript)
$ErrorActionPreference = 'Stop'
$temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$root = Join-Path $temporary ('hatteda-package-' + [Guid]::NewGuid().ToString('N'))
function Assert-True($condition, $message) {
    if (-not $condition) { throw $message }
}
function Expect-Failure($action, $text) {
    try { & $action; throw 'Expected a packaging failure.' }
    catch { Assert-True ($_.Exception.Message.Contains($text)) $_.Exception.Message }
}
try {
    $deployment = Join-Path $root 'deployed'
    $output = Join-Path $root 'packages'
    New-Item -ItemType Directory -Path (Join-Path $deployment 'plugins/platforms') -Force | Out-Null
    foreach ($file in @('hatteda.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
            'plugins/platforms/qwindows.dll', 'LICENSE', 'README.md', 'THIRD_PARTY_LICENSES.md', '.hidden')) {
        [IO.File]::WriteAllText((Join-Path $deployment $file), "fixture $file")
    }
    $arguments = @{ DeploymentDirectory = $deployment; OutputDirectory = $output;
        Version = '0.1.0-candidate'; SourceCommit = ('a' * 40); QtVersion = '6.11.2'; CompilerVersion = '13.1.0' }
    Expect-Failure { & $PackageScript @arguments -OutputDirectory (Join-Path $deployment 'nested') } 'outside'
    $plugin = Join-Path $deployment 'plugins/platforms/qoffscreen.dll'
    [IO.File]::WriteAllText($plugin, 'test only')
    Expect-Failure { & $PackageScript @arguments } 'offscreen'
    Remove-Item -LiteralPath $plugin
    $license = Join-Path $deployment 'LICENSE'
    Remove-Item -LiteralPath $license
    Expect-Failure { & $PackageScript @arguments } 'LICENSE'
    [IO.File]::WriteAllText($license, 'license fixture')
    New-Item -ItemType Directory -Path (Join-Path $deployment 'autorouter') | Out-Null
    Expect-Failure { & $PackageScript @arguments } 'core bundle only'
    Remove-Item -LiteralPath (Join-Path $deployment 'autorouter')
    $result = & $PackageScript @arguments
    $manifest = Get-Content -LiteralPath $result.Manifest -Raw | ConvertFrom-Json
    Assert-True ($manifest.sourceCommit -eq ('a' * 40)) 'Source commit mismatch.'
    Assert-True ($manifest.files.Count -eq 9) 'Manifest must list every payload file, including hidden files.'
    foreach ($file in $manifest.files) {
        Assert-True ($file.sha256 -eq (Get-FileHash -LiteralPath (Join-Path $deployment $file.path)).Hash.ToLowerInvariant()) 'Payload hash mismatch.'
    }
    $checksum = Get-Content -LiteralPath $result.Checksum -Raw
    Assert-True ($checksum.StartsWith((Get-FileHash -LiteralPath $result.Archive).Hash.ToLowerInvariant() + '  ')) 'Archive checksum mismatch.'
    $unpacked = Join-Path $root 'unpacked'
    [IO.Compression.ZipFile]::ExtractToDirectory($result.Archive, $unpacked)
    Assert-True (Test-Path -LiteralPath (Join-Path $unpacked '.hidden')) 'ZIP omitted a hidden file.'
    Assert-True (Test-Path -LiteralPath (Join-Path $unpacked 'release-manifest.json')) 'ZIP omitted the manifest.'
    Expect-Failure { & $PackageScript @arguments } 'overwrite'
    Write-Host 'Release packaging regressions passed.'
} finally {
    $resolvedRoot = [IO.Path]::GetFullPath($root)
    if (-not $resolvedRoot.StartsWith($temporary, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Unsafe temporary cleanup path.'
    }
    if (Test-Path -LiteralPath $resolvedRoot) { Remove-Item -LiteralPath $resolvedRoot -Recurse -Force }
}
