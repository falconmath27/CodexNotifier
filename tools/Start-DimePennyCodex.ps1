[CmdletBinding()]
param(
    [string] $BridgePath = (Join-Path $PSScriptRoot '../build/bridge/DimePennyInteractive.exe'),
    [string] $CodexCommand = 'codex',
    [string] $SessionId = '',
    [string] $Endpoint = '',
    [switch] $PrintHooks,
    [string[]] $CodexArguments = @()
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $BridgePath) -and (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'DimePennyInteractive.exe'))) {
    $BridgePath = Join-Path $PSScriptRoot 'DimePennyInteractive.exe'
}
$bridge = (Resolve-Path -LiteralPath $BridgePath).Path
if ($PrintHooks) {
    & $bridge --print-hooks
    return
}
if ((Get-Location).Provider.Name -ne 'FileSystem') { throw 'Start from a project folder.' }
$codex = Get-Command $CodexCommand -CommandType Application,ExternalScript -ErrorAction Stop | Select-Object -First 1
$previousEndpoint = $env:DIMEPENNY_HOOK_ENDPOINT
$previousToken = $env:DIMEPENNY_HOOK_TOKEN
$lease = New-Object System.Diagnostics.Process
$lease.StartInfo.FileName = $bridge
$lease.StartInfo.Arguments = "--attach $PID"
if ($Endpoint) {
    if ($Endpoint -notmatch '^[a-zA-Z0-9_-]{1,128}$') { throw 'Invalid local endpoint name.' }
    $lease.StartInfo.Arguments += " --endpoint $Endpoint"
}
$lease.StartInfo.UseShellExecute = $false
$lease.StartInfo.CreateNoWindow = $true
$lease.StartInfo.RedirectStandardInput = $true
$lease.StartInfo.RedirectStandardOutput = $true
$lease.StartInfo.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false)
$started = $false
try {
    if (-not $lease.Start()) { throw 'Could not start the Dime&Penny bridge.' }
    $started = $true
    $request = @{ project = (Get-Location).ProviderPath; sessionId = $SessionId } | ConvertTo-Json -Compress
    # Windows PowerShell 5.1 lacks ProcessStartInfo.StandardInputEncoding.
    # Write UTF-8 bytes directly so non-ASCII project paths are preserved.
    $bytes = (New-Object System.Text.UTF8Encoding($false)).GetBytes($request)
    $lease.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
    $lease.StandardInput.Close()
    $read = $lease.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(8000)) { throw 'Dime&Penny registration timed out.' }
    $registration = $read.Result | ConvertFrom-Json
    if (-not $registration.ok) { throw "Dime&Penny: $($registration.error)" }
    $env:DIMEPENNY_HOOK_ENDPOINT = $registration.endpoint
    $env:DIMEPENNY_HOOK_TOKEN = $registration.token
    Write-Host "Dime&Penny session: $($registration.sessionId)"
    Write-Host 'Pet waits for trusted Codex hooks. Review /hooks if it stays Connecting.'
    # Codex runs in THIS console, with normal stdin/stdout and its normal policy.
    # No piping, transcript inspection, output parsing or permission overrides.
    & $codex.Source @CodexArguments
    if ($lease.HasExited) { Write-Warning 'Dime&Penny disconnected; relaunch this wrapper to reconnect the pet.' }
} finally {
    $env:DIMEPENNY_HOOK_ENDPOINT = $previousEndpoint
    $env:DIMEPENNY_HOOK_TOKEN = $previousToken
    if ($started -and -not $lease.HasExited) { $lease.Kill(); $lease.WaitForExit(3000) | Out-Null }
    $lease.Dispose()
}
