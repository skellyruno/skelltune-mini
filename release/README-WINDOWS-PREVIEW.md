# SkellyTune Realtime 3.3.1 — Windows x64 VST3 preview

This is a **CI preview package for compatibility testing**, not a commercial release. It is produced from a public GitHub repository and is not an access-restricted private download.

## Contents

- SkellyTune Realtime.vst3/ — one Windows x64 VST3 bundle.
- BUILD-INFO.txt — source commit, workflow run, JUCE/SkellCore pins and build time.
- SHA256SUMS.txt — SHA-256 hashes for every packaged file except the checksum manifest itself.
- LICENSE.txt and THIRD_PARTY_NOTICES/ — project and dependency notices.

The GitHub Actions artifact itself is the downloadable ZIP. Its root contains the VST3 bundle and notices; it must not contain a second ZIP nested inside it.

## Installation for testing

Copy the entire SkellyTune Realtime.vst3 folder into a VST3 scan directory used by your host, then rescan plugins. Keep the existing installed version until the new copy has been checked. To roll back, remove only this preview bundle and restore the previously retained version.

## Verification boundary

The CI build, editor startup smoke, Windows x64 PE32+ check, strict module metadata check and pluginval strictness-5 check are automated. This package does **not** claim FL Studio scan/load acceptance, musical A/B acceptance, preset/state/automation recall, or target-hardware CPU/latency acceptance. Those require separate real-host testing.

## Commercial licensing

Do not distribute this preview commercially. JUCE 8.0.12 is dual-licensed under AGPLv3 or the JUCE commercial license; the applicable Skelly Audio licensing entitlement has not been verified in this release workflow. Review the included JUCE license notice and obtain the correct licensing approval before any commercial distribution.
