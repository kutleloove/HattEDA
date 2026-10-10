param(
    [Parameter(Mandatory)][string]$DeploymentDirectory,
    [Parameter(Mandatory)][string]$QtRootDirectory,
    [Parameter(Mandatory)][string]$LogDirectory,
    [ValidateRange(1, 60)][int]$StartupSeconds = 5
)

$ErrorActionPreference = 'Stop'
$deployment = (Resolve-Path -LiteralPath $DeploymentDirectory).Path
$executable = Join-Path $deployment 'hatteda.exe'
$sourcePlugin = Join-Path $QtRootDirectory 'plugins/platforms/qoffscreen.dll'
$testPlugin = Join-Path $deployment 'plugins/platforms/qoffscreen.dll'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Deployed executable is missing: $executable"
}
if (-not (Test-Path -LiteralPath $sourcePlugin -PathType Leaf)) {
    throw "Smoke test platform plugin is missing: $sourcePlugin"
}
if (Test-Path -LiteralPath $testPlugin) {
    throw "The distribution already contains the test-only offscreen plugin: $testPlugin"
}
New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$stdout = Join-Path $LogDirectory 'hatteda-smoke-out.txt'
$stderr = Join-Path $LogDirectory 'hatteda-smoke-err.txt'
$savedEnvironment = @{}
$isolatedQtVariables = @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH')
foreach ($name in (@('PATH', 'QT_QPA_PLATFORM', 'QT_QPA_FONTDIR') + $isolatedQtVariables)) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$process = $null
$pluginCopied = $false
try {
    Copy-Item -LiteralPath $sourcePlugin -Destination $testPlugin
    $pluginCopied = $true
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QPA_FONTDIR = Join-Path $env:SystemRoot 'Fonts'
    $env:PATH = "$env:SystemRoot/system32;$env:SystemRoot"
    foreach ($name in $isolatedQtVariables) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $process = Start-Process -FilePath $executable -WorkingDirectory $deployment -PassThru `
        -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if ($process.WaitForExit($StartupSeconds * 1000)) {
        Get-Content -LiteralPath $stderr -ErrorAction SilentlyContinue | Write-Host
        throw "hatteda.exe exited during startup (exit code $($process.ExitCode)); see $LogDirectory"
    }
    Write-Host "Deployed Release survived $StartupSeconds seconds with no Qt/MinGW on PATH."
    [pscustomobject]@{ ProcessId = $process.Id; LogDirectory = $LogDirectory }
}
finally {
    try {
        if ($null -ne $process) {
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                $process.WaitForExit()
            }
            $process.Dispose()
        }
    }
    finally {
        foreach ($name in $savedEnvironment.Keys) {
            [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
        }
        if ($pluginCopied) { Remove-Item -LiteralPath $testPlugin -Force }
    }
}
