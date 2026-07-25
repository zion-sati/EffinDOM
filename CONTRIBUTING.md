# Contributing to EffinDOM

This repository contains the native and WebAssembly EffinDOM runtime. SDK and
application developers should start with the [runtime quickstart](QUICKSTART.md)
instead.

## Windows development MSIX certificate

Local MSIX packages must be signed by a certificate trusted by the development
machine. From an **elevated PowerShell**, run:

```powershell
.\scripts\setup-windows-development-certificate.ps1
```

The script creates or reuses a self-signed code-signing certificate with these
boundaries:

- The non-exportable private key stays in `Cert:\CurrentUser\My`.
- Only the public certificate is installed in
  `Cert:\LocalMachine\TrustedPeople`; it is not made a root CA.
- The thumbprint is persisted in the user-level
  `EFFINDOM_TEST_CERT_THUMBPRINT` environment variable.
- Non-secret metadata and the public `.cer` are written below
  `%LOCALAPPDATA%\EffinDOM\Signing`.
- The certificate is for local development only. Do not use it for production
  releases or distribute its private key.

Open a new terminal after setup so build and package commands inherit the
thumbprint. Running the setup command again reuses a valid configured
certificate. To replace or remove it:

```powershell
# Replace the configured certificate.
.\scripts\setup-windows-development-certificate.ps1 -Force

# Remove the personal/trusted copies and local metadata.
.\scripts\setup-windows-development-certificate.ps1 -Remove
```

CI and clean-room acceptance should not install this persistent identity. When
`EFFINDOM_TEST_CERT_THUMBPRINT` is absent, the Windows package-acceptance lane
creates and removes an ephemeral certificate instead.

## Validation

Run the affected platform build with tests before opening a pull request. The
repository's platform workflows are the authoritative cross-platform gates;
native package signing additionally requires the relevant operating-system
certificate and installer services.
