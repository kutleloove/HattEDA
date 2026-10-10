param(
    [Parameter(Mandatory)][string]$FakeExecutable,
    [Parameter(Mandatory)][string]$SmokeScript
)
$ErrorActionPreference = 'Stop'
function Assert($Condition, $Message) {
    if (-not $Condition) { throw $Message }
}
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('hatteda-smoke-' + [guid]::NewGuid())
$deploy = Join-Path $testRoot 'dist'
$qt = Join-Path $testRoot 'qt'
$logs = Join-Path $testRoot 'logs'
$plugin = Join-Path $deploy 'plugins/platforms/qoffscreen.dll'
$saved = @{}
foreach ($name in @('PATH', 'QT_QPA_PLATFORM', 'QT_QPA_FONTDIR', 'HATTEDA_SMOKE_FAKE_EXIT')) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    New-Item -ItemType Directory -Path "$deploy/plugins/platforms", "$qt/plugins/platforms" -Force | Out-Null
    Copy-Item -LiteralPath $FakeExecutable -Destination "$deploy/hatteda.exe"
    Set-Content -LiteralPath "$qt/plugins/platforms/qoffscreen.dll" -Value 'test fixture'
    $env:QT_QPA_PLATFORM = 'sentinel-platform'
    $env:QT_QPA_FONTDIR = 'sentinel-fonts'
    $env:HATTEDA_SMOKE_FAKE_EXIT = $null
    $result = & $SmokeScript -DeploymentDirectory $deploy -QtRootDirectory $qt `
        -LogDirectory $logs -StartupSeconds 1
    Assert (-not (Test-Path -LiteralPath $plugin)) 'Test plugin leaked into successful package'
    Assert ($null -eq (Get-Process -Id $result.ProcessId -ErrorAction SilentlyContinue)) 'Smoke process leaked'
    Assert ($env:PATH -eq $saved['PATH']) 'PATH was not restored'
    Assert ($env:QT_QPA_PLATFORM -eq 'sentinel-platform') 'Platform was not restored'
    Assert ($env:QT_QPA_FONTDIR -eq 'sentinel-fonts') 'Fonts were not restored'

    $env:HATTEDA_SMOKE_FAKE_EXIT = '1'
    $failure = ''
    try {
        & $SmokeScript -DeploymentDirectory $deploy -QtRootDirectory $qt `
            -LogDirectory $logs -StartupSeconds 1 | Out-Null
    }
    catch { $failure = $_.Exception.Message }
    Assert ($failure -match 'exit code 23') 'Early exit was not reported'
    Assert (-not (Test-Path -LiteralPath $plugin)) 'Test plugin leaked after failed startup'
    Assert ((Get-Content -Raw "$logs/hatteda-smoke-err.txt") -match 'intentional startup failure') 'Diagnostic log missing'
    Assert ($env:PATH -eq $saved['PATH']) 'PATH was not restored after failure'
    Assert ($env:QT_QPA_PLATFORM -eq 'sentinel-platform') 'Platform was not restored after failure'
    Assert ($env:QT_QPA_FONTDIR -eq 'sentinel-fonts') 'Fonts were not restored after failure'

    Set-Content -LiteralPath $plugin -Value 'existing plugin'
    $failure = ''
    try {
        & $SmokeScript -DeploymentDirectory $deploy -QtRootDirectory $qt `
            -LogDirectory $logs -StartupSeconds 1 | Out-Null
    }
    catch { $failure = $_.Exception.Message }
    Assert ($failure -match 'already contains') 'Existing plugin was not rejected'
    Assert ((Get-Content -LiteralPath $plugin) -eq 'existing plugin') 'Existing plugin was changed'

    Remove-Item -LiteralPath $plugin
    Set-Content -LiteralPath "$deploy/hatteda.exe" -Value 'invalid executable fixture'
    $failure = ''
    try {
        & $SmokeScript -DeploymentDirectory $deploy -QtRootDirectory $qt `
            -LogDirectory $logs -StartupSeconds 1 | Out-Null
    }
    catch { $failure = $_.Exception.Message }
    Assert ($failure.Length -gt 0) 'An invalid executable was accepted'
    Assert (-not (Test-Path -LiteralPath $plugin)) 'Test plugin leaked after launch failure'
    Assert ($env:PATH -eq $saved['PATH']) 'PATH was not restored after launch failure'
    Assert ($env:QT_QPA_PLATFORM -eq 'sentinel-platform') 'Platform was not restored after launch failure'
    Assert ($env:QT_QPA_FONTDIR -eq 'sentinel-fonts') 'Fonts were not restored after launch failure'
    Write-Host 'Release deployment smoke regression tests passed.'
}
finally {
    foreach ($name in $saved.Keys) {
        [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process')
    }
    $resolvedRoot = [IO.Path]::GetFullPath($testRoot)
    $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Test cleanup path is outside TEMP: $resolvedRoot"
    }
    Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
}
