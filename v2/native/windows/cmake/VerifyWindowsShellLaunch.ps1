[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$OutputRoot,
    [Parameter(Mandatory)] [string]$ExecutableName,
    [Parameter(Mandatory)] [string]$RelocatedRoot
)

$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath($OutputRoot)
$destination = [IO.Path]::GetFullPath($RelocatedRoot)
if ($destination -eq $source -or $destination -eq [IO.Path]::GetPathRoot($destination)) {
    throw "Refusing unsafe relocation directory: $destination"
}
if (-not (Test-Path -LiteralPath (Join-Path $source "bin/$ExecutableName"))) {
    throw "Native executable is missing from $source"
}

if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}
New-Item -ItemType Directory -Path $destination | Out-Null
Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force

$executable = Join-Path $destination "bin/$ExecutableName"
$screenshot = Join-Path $destination 'shell-launch-screenshot.png'
$workingDirectory = [IO.Path]::GetTempPath()
$process = Start-Process -FilePath $executable `
    -ArgumentList @('--hidden', '--screenshot', "`"$screenshot`"") `
    -WorkingDirectory $workingDirectory -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "Windows shell launch exited with code $($process.ExitCode)"
}
if (-not (Test-Path -LiteralPath $screenshot) -or (Get-Item -LiteralPath $screenshot).Length -lt 1024) {
    throw 'Windows shell launch did not produce a valid screenshot.'
}

Write-Host "Verified relocated Windows shell launch from $workingDirectory"
