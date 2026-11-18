# SkellyTune Realtime 3.3.1 — Windows x64 VST3 preview

This is a **CI preview package for compatibility testing**, not a commercial release. It is produced from a public GitHub repository and is not an access-restricted private download.

## Contents

- SkellyTune Realtime.vst3/ — one Windows x64 VST3 bundle.
- BUILD-INFO.txt — source commit, workflow run, JUCE/SkellCore pins and build time.
- SHA256SUMS.txt — SHA-256 hashes for every packaged file except the checksum manifest itself.
- LICENSE.txt and THIRD_PARTY_NOTICES/ — project and dependency notices.

The GitHub Actions artifact itself is the downloadable ZIP. Its root contains the VST3 bundle and notices; it must not contain a second ZIP nested inside it.

## Installation for testing

1. Close FL Studio and any other host that may have the plugin loaded.
2. Extract the complete artifact ZIP to a temporary folder. Keep the VST3 bundle, installer script, build information, checksum manifest and notices together.
3. Open **PowerShell 7 as Administrator** for the standard all-users VST3 location. The script does not self-elevate.
4. Preview the operation first:

   > .\Install-SkellyTuneMini.ps1 -PackageRoot . -WhatIf

5. After reviewing the validation output, run the explicit install and confirm the PowerShell approval prompt:

   > .\Install-SkellyTuneMini.ps1 -PackageRoot .

The installer rechecks the package manifest, product/version/vendor and VST3 class IDs, bundle layout, and AMD64 PE32+ module. It stages the bundle beside the destination, verifies its tree hash, and only then swaps it into place. If a matching SkellyTune Realtime version already exists, it first creates and hash-verifies a backup under %LOCALAPPDATA%\Skelly\Backups\SkellyTuneMini. It refuses to replace a different or malformed plugin identity. A custom scan directory can be supplied with -Destination.

### Rollback

If a previous version was present during installation, preview and run:

> .\Install-SkellyTuneMini.ps1 -Rollback -WhatIf
>
> .\Install-SkellyTuneMini.ps1 -Rollback

Rollback verifies that the installed bundle still matches the exact hash recorded by the installer and that the previous backup also matches its recorded hash. If the installed bundle changed after installation, rollback refuses to overwrite it. The newer bundle is retained as a reversible backup. A first-time install has no earlier version to restore.

The script never installs automatically during a build. Building or downloading the preview does not change the machine. Avoid duplicate copies with the same VST3 identity in multiple host scan locations.

## Verification boundary

The CI build, editor startup smoke, Windows x64 PE32+ check, strict module metadata check and pluginval strictness-5 check are automated. This package does **not** claim FL Studio scan/load acceptance, musical A/B acceptance, preset/state/automation recall, or target-hardware CPU/latency acceptance. Those require separate real-host testing.

## Commercial licensing

Do not distribute this preview commercially. JUCE 8.0.12 is dual-licensed under AGPLv3 or the JUCE commercial license; the applicable Skelly Audio licensing entitlement has not been verified in this release workflow. Review the included JUCE license notice and obtain the correct licensing approval before any commercial distribution.
