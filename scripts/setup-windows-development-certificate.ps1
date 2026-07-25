[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidateRange(1, 10)]
    [int]$ValidYears = 3,
    [switch]$Force,
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -eq 'Core') {
    $windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $windowsPowerShellArguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $PSCommandPath,
        '-ValidYears', $ValidYears
    )
    if ($Force) {
        $windowsPowerShellArguments += '-Force'
    }
    if ($Remove) {
        $windowsPowerShellArguments += '-Remove'
    }
    & $windowsPowerShell @windowsPowerShellArguments
    exit $LASTEXITCODE
}

$subject = 'CN=EffinDOM Packaging Test'
$friendlyName = 'EffinDOM Development MSIX Signing'
$environmentVariable = 'EFFINDOM_TEST_CERT_THUMBPRINT'
$personalStore = 'Cert:\CurrentUser\My'
$trustedStore = 'Cert:\LocalMachine\TrustedPeople'
$metadataDirectory = Join-Path $env:LOCALAPPDATA 'EffinDOM\Signing'
$metadataPath = Join-Path $metadataDirectory 'development-certificate.json'
$publicCertificatePath = Join-Path $metadataDirectory 'effindom-development-code-signing.cer'

if ($PSVersionTable.PSEdition -eq 'Core' -and -not $IsWindows) {
    throw 'This script must be run on Windows.'
}

$isAdministrator = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)
if (-not $isAdministrator) {
    throw 'Run this script from an elevated PowerShell so it can update LocalMachine\TrustedPeople.'
}

if (-not (Get-PSDrive -Name Cert -ErrorAction SilentlyContinue)) {
    New-PSDrive -Name Cert -PSProvider Certificate -Root '\' -Scope Script | Out-Null
}

function Get-ConfiguredThumbprint {
    $value = [Environment]::GetEnvironmentVariable($environmentVariable, 'User')
    if ($value) {
        return ($value -replace '\s', '').ToUpperInvariant()
    }
    return $null
}

function Remove-DevelopmentCertificate {
    param([string]$Thumbprint)

    if (-not $Thumbprint) {
        return
    }
    foreach ($store in @($trustedStore, $personalStore)) {
        $path = Join-Path $store $Thumbprint
        if ((Test-Path $path) -and $PSCmdlet.ShouldProcess($path, 'Remove EffinDOM development certificate')) {
            Remove-Item $path -Force
        }
    }
}

$configuredThumbprint = Get-ConfiguredThumbprint
if ($Remove) {
    Remove-DevelopmentCertificate -Thumbprint $configuredThumbprint
    [Environment]::SetEnvironmentVariable($environmentVariable, $null, 'User')
    Remove-Item $metadataPath, $publicCertificatePath -Force -ErrorAction SilentlyContinue
    Write-Host 'EffinDOM development signing certificate configuration removed.' -ForegroundColor Green
    exit 0
}

$certificate = $null
if ($configuredThumbprint -and -not $Force) {
    $configuredPath = Join-Path $personalStore $configuredThumbprint
    if (Test-Path $configuredPath) {
        $candidate = Get-Item $configuredPath
        if ($candidate.HasPrivateKey -and $candidate.NotAfter -gt (Get-Date).AddDays(30)) {
            $certificate = $candidate
        }
    }
}

if ($Force -and $configuredThumbprint) {
    Remove-DevelopmentCertificate -Thumbprint $configuredThumbprint
    $certificate = $null
}

if (-not $certificate) {
    $certificate = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $subject `
        -FriendlyName $friendlyName `
        -CertStoreLocation $personalStore `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -HashAlgorithm SHA256 `
        -KeyExportPolicy NonExportable `
        -NotAfter (Get-Date).AddYears($ValidYears)
}

New-Item -ItemType Directory -Path $metadataDirectory -Force | Out-Null
Export-Certificate -Cert $certificate -FilePath $publicCertificatePath -Force | Out-Null

$trustedPath = Join-Path $trustedStore $certificate.Thumbprint
if (-not (Test-Path $trustedPath)) {
    Import-Certificate `
        -FilePath $publicCertificatePath `
        -CertStoreLocation $trustedStore | Out-Null
}

[Environment]::SetEnvironmentVariable($environmentVariable, $certificate.Thumbprint, 'User')
$env:EFFINDOM_TEST_CERT_THUMBPRINT = $certificate.Thumbprint

[ordered]@{
    subject = $certificate.Subject
    thumbprint = $certificate.Thumbprint
    notAfter = $certificate.NotAfter.ToUniversalTime().ToString('o')
    personalStore = $personalStore
    trustedStore = $trustedStore
    publicCertificate = $publicCertificatePath
} | ConvertTo-Json | Set-Content -Path $metadataPath -Encoding utf8

Write-Host ''
Write-Host 'EffinDOM development MSIX signing is configured.' -ForegroundColor Green
Write-Host "Thumbprint: $($certificate.Thumbprint)"
Write-Host "Expires:    $($certificate.NotAfter)"
Write-Host "Metadata:   $metadataPath"
Write-Host ''
Write-Host 'Open a new terminal before running packaging commands so it inherits:'
Write-Host "$environmentVariable=$($certificate.Thumbprint)"
Write-Host 'The private key remains non-exportable in the current user certificate store.'
