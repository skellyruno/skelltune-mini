[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string]$PackageRoot,
    [string]$Destination,
    [string]$BackupRoot,
    [switch]$Rollback,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:ProductName = 'SkellyTune Realtime'
$script:BundleName = 'SkellyTune Realtime.vst3'

function Assert-NoReparsePoints {
    param([Parameter(Mandatory)][string]$Path)
    $root = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($root.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing a reparse-point root: $Path"
    }
    $pending = [System.Collections.Generic.Stack[string]]::new()
    $pending.Push($root.FullName)
    while ($pending.Count -gt 0) {
        $current = $pending.Pop()
        foreach ($item in (Get-ChildItem -LiteralPath $current -Force -ErrorAction Stop)) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing a package or install tree containing a reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
}

function Get-TreeSha256 {
    param([Parameter(Mandatory)][string]$Path)
    $root = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Bundle directory missing: $root" }
    Assert-NoReparsePoints -Path $root
    $files = @(Get-ChildItem -LiteralPath $root -File -Recurse -Force | Sort-Object -Property FullName)
    if ($files.Count -eq 0) { throw "Bundle contains no files: $root" }
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $lines.Add("$hash  $relative")
    }
    $payload = [Text.Encoding]::UTF8.GetBytes(($lines -join [Environment]::NewLine) + [Environment]::NewLine)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($payload))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Test-PeX64 {
    param([Parameter(Mandatory)][string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 512 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "VST3 module is not a valid Windows PE file: $Path"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($peOffset -lt 64 -or $peOffset + 26 -gt $bytes.Length) { throw "Invalid PE header offset: $Path" }
    if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        throw "PE signature missing: $Path"
    }
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    $magic = [BitConverter]::ToUInt16($bytes, $peOffset + 24)
    if ($machine -ne 0x8664 -or $magic -ne 0x20b) {
        throw "VST3 module must be AMD64 PE32+ (machine 0x8664, magic 0x20b): $Path"
    }
}

function Read-ModuleIdentity {
    param(
        [Parameter(Mandatory)][string]$BundlePath,
        [string]$ExpectedVersion
    )
    Assert-NoReparsePoints -Path $BundlePath
    $moduleInfoPath = Join-Path $BundlePath 'Contents/Resources/moduleinfo.json'
    $modulePath = Join-Path $BundlePath 'Contents/x86_64-win/SkellyTune Realtime.vst3'
    if (-not (Test-Path -LiteralPath $moduleInfoPath -PathType Leaf)) { throw "VST3 moduleinfo.json missing: $BundlePath" }
    if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) { throw "Expected AMD64 VST3 module missing: $BundlePath" }
    $document = [IO.File]::ReadAllText($moduleInfoPath) | ConvertFrom-Json -ErrorAction Stop
    if ($document.Name -ne $script:ProductName) { throw "VST3 identity is not $($script:ProductName)" }
    $version = [string]$document.Version
    if ([string]::IsNullOrWhiteSpace($version)) { throw 'VST3 moduleinfo has no version' }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedVersion) -and $version -ne $ExpectedVersion) {
        throw "VST3 moduleinfo version $version does not match package version $ExpectedVersion"
    }
    if ($null -eq $document.'Factory Info' -or $document.'Factory Info'.Vendor -ne 'Skelly Audio') {
        throw 'VST3 moduleinfo vendor must be Skelly Audio'
    }
    $classes = @($document.Classes)
    if ($classes.Count -lt 2) { throw 'VST3 moduleinfo must contain audio and controller classes' }
    $ids = [System.Collections.Generic.List[string]]::new()
    $categories = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($class in $classes) {
        $cid = [string]$class.CID
        if ($cid -notmatch '^[0-9A-Fa-f]{32}$') { throw 'VST3 class ID is not 32 hexadecimal characters' }
        if ($class.Name -ne $script:ProductName -or $class.Vendor -ne 'Skelly Audio' -or
            [string]$class.Version -ne $version) {
            throw 'VST3 class name, vendor or version does not match the product identity'
        }
        $ids.Add($cid.ToLowerInvariant())
        if ($class.Category -is [string]) { [void]$categories.Add([string]$class.Category) }
    }
    if (($ids | Select-Object -Unique).Count -ne $ids.Count) { throw 'VST3 moduleinfo contains duplicate class IDs' }
    foreach ($required in @('Audio Module Class', 'Component Controller Class')) {
        if (-not $categories.Contains($required)) { throw "VST3 moduleinfo is missing class category: $required" }
    }
    Test-PeX64 -Path $modulePath
    return [pscustomobject]@{ Version = $version; ClassIds = @($ids.ToArray()) }
}

function Test-ChecksumManifest {
    param([Parameter(Mandatory)][string]$PackageRoot)
    $manifestPath = Join-Path $PackageRoot 'SHA256SUMS.txt'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'SHA256SUMS.txt is missing' }
    $expected = @{}
    foreach ($line in (Get-Content -LiteralPath $manifestPath)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') { throw "Malformed checksum line: $line" }
        $relative = $Matches[2].Replace('\', '/')
        $parts = $relative.Split('/')
        if ([IO.Path]::IsPathRooted($relative) -or $parts -contains '..' -or $parts -contains '') {
            throw "Unsafe checksum path: $relative"
        }
        if ($expected.ContainsKey($relative)) { throw "Duplicate checksum path: $relative" }
        $expected[$relative] = $Matches[1].ToLowerInvariant()
    }
    $actualFiles = @(Get-ChildItem -LiteralPath $PackageRoot -File -Recurse -Force |
        Where-Object { $_.FullName -ne $manifestPath })
    if ($actualFiles.Count -ne $expected.Count) { throw 'Checksum manifest does not cover every package file exactly once' }
    foreach ($file in $actualFiles) {
        $relative = [IO.Path]::GetRelativePath($PackageRoot, $file.FullName).Replace('\', '/')
        if (-not $expected.ContainsKey($relative)) { throw "Package file is not covered by SHA256SUMS.txt: $relative" }
        $actual = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $expected[$relative]) { throw "SHA-256 mismatch: $relative" }
    }
}

function Test-PreviewPackage {
    param([Parameter(Mandatory)][string]$PackageRoot)
    $root = [IO.Path]::GetFullPath($PackageRoot)
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Package root not found: $root" }
    Assert-NoReparsePoints -Path $root
    foreach ($required in @('BUILD-INFO.txt', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES', 'SHA256SUMS.txt')) {
        $path = Join-Path $root $required
        if (-not (Test-Path -LiteralPath $path)) { throw "Required preview package content missing: $required" }
    }
    $bundles = @(Get-ChildItem -LiteralPath $root -Directory -Recurse -Filter '*.vst3')
    if ($bundles.Count -ne 1 -or $bundles[0].Name -ne $script:BundleName) {
        throw "Expected exactly one $($script:BundleName) bundle; found $($bundles.Count)"
    }
    $bundle = $bundles[0].FullName
    $modules = @(Get-ChildItem -LiteralPath $bundle -File -Recurse -Filter '*.vst3')
    if ($modules.Count -ne 1) { throw "Expected exactly one VST3 module binary; found $($modules.Count)" }
    $relativeModule = [IO.Path]::GetRelativePath($bundle, $modules[0].FullName)
    $expectedModule = [IO.Path]::Combine('Contents', 'x86_64-win', $script:BundleName)
    if (-not [string]::Equals($relativeModule, $expectedModule, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected VST3 module location: $relativeModule"
    }
    $buildInfo = [IO.File]::ReadAllText((Join-Path $root 'BUILD-INFO.txt'))
    if ($buildInfo -notmatch '(?m)^Product:\s*SkellyTune Realtime\s*$') { throw 'BUILD-INFO product identity mismatch' }
    $versionMatch = [regex]::Match($buildInfo, '(?m)^Version:\s*([0-9]+(?:\.[0-9]+){1,2})\s*$')
    if (-not $versionMatch.Success) { throw 'BUILD-INFO version is missing or malformed' }
    $identity = Read-ModuleIdentity -BundlePath $bundle -ExpectedVersion $versionMatch.Groups[1].Value
    if ($buildInfo -notmatch '(?m)^Target:\s*Windows x64 VST3\s*$') { throw 'BUILD-INFO target is not Windows x64 VST3' }
    Test-ChecksumManifest -PackageRoot $root
    return [pscustomobject]@{
        PackageRoot = $root
        BundlePath = $bundle
        Version = $identity.Version
        TreeSha256 = Get-TreeSha256 -Path $bundle
    }
}

function Test-InstalledBundle {
    param([Parameter(Mandatory)][string]$BundlePath)
    $identity = Read-ModuleIdentity -BundlePath $BundlePath
    return [pscustomobject]@{
        Version = $identity.Version
        TreeSha256 = Get-TreeSha256 -Path $BundlePath
    }
}

function Write-StateFile {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)]$State)
    $json = ConvertTo-Json -InputObject $State -Depth 8
    [IO.File]::WriteAllText($Path, $json, [Text.UTF8Encoding]::new($false))
}

function Get-RollbackPlan {
    param([Parameter(Mandatory)][string]$BackupRoot, [string]$Destination)
    $backupFull = [IO.Path]::GetFullPath($BackupRoot)
    if (-not (Test-Path -LiteralPath $backupFull -PathType Container)) { throw 'Install backup root is missing' }
    Assert-NoReparsePoints -Path $backupFull
    $statePath = Join-Path $backupFull 'active-install.json'
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) { throw 'No SkellyTune Mini install history exists in this backup root' }
    $state = [IO.File]::ReadAllText($statePath) | ConvertFrom-Json -ErrorAction Stop
    if ($state.Schema -ne 'skellytune-mini-install/1' -or $state.Product -ne $script:ProductName) {
        throw 'Install state file has an unsupported schema or product identity'
    }
    $recordedDestination = [IO.Path]::GetFullPath([string]$state.Destination)
    if (-not (Test-Path -LiteralPath $recordedDestination -PathType Container)) { throw 'Recorded VST3 destination is missing' }
    $destinationItem = Get-Item -LiteralPath $recordedDestination -Force
    if (($destinationItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Recorded VST3 destination is a reparse point' }
    if (-not [string]::IsNullOrWhiteSpace($Destination) -and
        -not [string]::Equals([IO.Path]::GetFullPath($Destination), $recordedDestination, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Requested destination does not match the recorded installation path'
    }
    $target = Join-Path $recordedDestination $script:BundleName
    if (-not [string]::Equals([IO.Path]::GetFullPath([string]$state.InstalledBundle), [IO.Path]::GetFullPath($target), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Recorded installation bundle path is inconsistent'
    }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw 'Installed VST3 bundle is missing; refusing rollback' }
    $current = Test-InstalledBundle -BundlePath $target
    if ($current.TreeSha256 -ne [string]$state.InstalledSha256) {
        throw 'Installed VST3 changed since this script installed it; refusing to overwrite user or host changes'
    }
    $previousBundle = [string]$state.PreviousBundle
    if ([string]::IsNullOrWhiteSpace($previousBundle) -or -not (Test-Path -LiteralPath $previousBundle -PathType Container)) {
        throw 'No previous version backup is recorded; there is nothing to roll back to'
    }
    $previousFull = [IO.Path]::GetFullPath($previousBundle)
    $backupPrefix = $backupFull
    if (-not $backupPrefix.EndsWith([IO.Path]::DirectorySeparatorChar)) {
        $backupPrefix += [IO.Path]::DirectorySeparatorChar
    }
    if (-not $previousFull.StartsWith($backupPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Previous-version backup path is outside the configured SkellyTune Mini backup root'
    }
    $previous = Test-InstalledBundle -BundlePath $previousFull
    if ($previous.TreeSha256 -ne [string]$state.PreviousSha256) { throw 'Previous-version backup hash does not match install history' }
    return [pscustomobject]@{
        State = $state
        StatePath = $statePath
        Destination = $recordedDestination
        Target = $target
        CurrentVersion = $current.Version
        CurrentSha256 = $current.TreeSha256
        PreviousBundle = $previousBundle
        PreviousVersion = $previous.Version
        PreviousSha256 = $previous.TreeSha256
    }
}

function Invoke-InstallInternal {
    param([string]$PackageRoot, [string]$Destination, [string]$BackupRoot)
    $package = Test-PreviewPackage -PackageRoot $PackageRoot
    $destinationFull = [IO.Path]::GetFullPath($Destination)
    $backupFull = [IO.Path]::GetFullPath($BackupRoot)
    if (Test-Path -LiteralPath $destinationFull) {
        Assert-NoReparsePoints -Path $destinationFull
    } else {
        $null = New-Item -ItemType Directory -Path $destinationFull -Force
    }
    if (Test-Path -LiteralPath $backupFull) {
        Assert-NoReparsePoints -Path $backupFull
    } else {
        $null = New-Item -ItemType Directory -Path $backupFull -Force
    }
    $target = Join-Path $destinationFull $script:BundleName
    $id = [guid]::NewGuid().ToString('N')
    $previousBundle = $null
    $previousHash = $null
    $previousVersion = $null
    if (Test-Path -LiteralPath $target) {
        if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw 'A non-directory item already occupies the VST3 destination' }
        $previous = Test-InstalledBundle -BundlePath $target
        $previousHash = $previous.TreeSha256
        $previousVersion = $previous.Version
        if ($previousHash -eq $package.TreeSha256) {
            Write-Host "Already installed: $($package.Version). No files changed."
            return
        }
        $backupDir = Join-Path $backupFull ("previous-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + "-" + $id)
        $null = New-Item -ItemType Directory -Path $backupDir
        try {
            Copy-Item -LiteralPath $target -Destination $backupDir -Recurse
            $previousBundle = Join-Path $backupDir $script:BundleName
            if ((Get-TreeSha256 -Path $previousBundle) -ne $previousHash) {
                throw 'Previous VST3 backup verification failed; original installation was not touched'
            }
        } catch {
            if (Test-Path -LiteralPath $backupDir) { Remove-Item -LiteralPath $backupDir -Recurse -Force }
            throw
        }
    }

    $stageRoot = Join-Path $destinationFull ('.skellytunemini-stage-' + $id)
    $swapPath = Join-Path $destinationFull ('.skellytunemini-previous-' + $id)
    $statePath = Join-Path $backupFull 'active-install.json'
    $stateTemp = Join-Path $backupFull ('active-install.' + $id + '.tmp')
    $null = New-Item -ItemType Directory -Path $stageRoot
    $committed = $false
    try {
        Copy-Item -LiteralPath $package.BundlePath -Destination $stageRoot -Recurse
        $stageBundle = Join-Path $stageRoot $script:BundleName
        if ((Get-TreeSha256 -Path $stageBundle) -ne $package.TreeSha256) { throw 'Staged VST3 verification failed' }
        $state = [ordered]@{
            Schema = 'skellytune-mini-install/1'
            Product = $script:ProductName
            Destination = $destinationFull
            InstalledBundle = $target
            InstalledVersion = $package.Version
            InstalledSha256 = $package.TreeSha256
            PreviousBundle = $previousBundle
            PreviousVersion = $previousVersion
            PreviousSha256 = $previousHash
            UpdatedUtc = (Get-Date).ToUniversalTime().ToString('o')
        }
        Write-StateFile -Path $stateTemp -State $state
        if (Test-Path -LiteralPath $target) { Move-Item -LiteralPath $target -Destination $swapPath }
        Move-Item -LiteralPath $stageBundle -Destination $target
        if ((Get-TreeSha256 -Path $target) -ne $package.TreeSha256) { throw 'Installed VST3 hash does not match the validated package' }
        Move-Item -LiteralPath $stateTemp -Destination $statePath -Force
        $committed = $true
    } catch {
        if (Test-Path -LiteralPath $target) {
            $failedDir = Join-Path $backupFull ('failed-install-' + $id)
            $null = New-Item -ItemType Directory -Path $failedDir -Force
            Move-Item -LiteralPath $target -Destination $failedDir
        }
        if ((Test-Path -LiteralPath $swapPath) -and -not (Test-Path -LiteralPath $target)) {
            Move-Item -LiteralPath $swapPath -Destination $target
        }
        throw
    } finally {
        if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
        if (Test-Path -LiteralPath $stateTemp) { Remove-Item -LiteralPath $stateTemp -Force }
    }
    if ($committed -and (Test-Path -LiteralPath $swapPath)) {
        try { Remove-Item -LiteralPath $swapPath -Recurse -Force }
        catch { Write-Warning "Temporary swap directory remains at $swapPath; verified backup is preserved." }
    }
    Write-Host "Installed $($script:ProductName) $($package.Version) to $target"
    if ($previousBundle) { Write-Host "Verified previous version backup: $previousBundle" }
    else { Write-Host 'No prior installation existed; rollback to an older version is not available.' }
}

function Invoke-RollbackInternal {
    param([string]$BackupRoot, [string]$Destination)
    $backupFull = [IO.Path]::GetFullPath($BackupRoot)
    $plan = Get-RollbackPlan -BackupRoot $backupFull -Destination $Destination
    $id = [guid]::NewGuid().ToString('N')
    $stageRoot = Join-Path $plan.Destination ('.skellytunemini-rollback-stage-' + $id)
    $forwardDir = Join-Path $backupFull ('rolled-forward-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $id)
    $forwardBundle = Join-Path $forwardDir $script:BundleName
    $stateTemp = Join-Path $backupFull ('active-install.' + $id + '.tmp')
    $null = New-Item -ItemType Directory -Path $stageRoot
    $null = New-Item -ItemType Directory -Path $forwardDir
    try {
        Copy-Item -LiteralPath $plan.PreviousBundle -Destination $stageRoot -Recurse
        $stageBundle = Join-Path $stageRoot $script:BundleName
        if ((Get-TreeSha256 -Path $stageBundle) -ne $plan.PreviousSha256) { throw 'Rollback staging hash mismatch' }
        $newState = [ordered]@{
            Schema = 'skellytune-mini-install/1'
            Product = $script:ProductName
            Destination = $plan.Destination
            InstalledBundle = $plan.Target
            InstalledVersion = $plan.PreviousVersion
            InstalledSha256 = $plan.PreviousSha256
            PreviousBundle = $forwardBundle
            PreviousVersion = $plan.CurrentVersion
            PreviousSha256 = $plan.CurrentSha256
            UpdatedUtc = (Get-Date).ToUniversalTime().ToString('o')
        }
        Write-StateFile -Path $stateTemp -State $newState
        Move-Item -LiteralPath $plan.Target -Destination $forwardDir
        Move-Item -LiteralPath $stageBundle -Destination $plan.Target
        if ((Get-TreeSha256 -Path $plan.Target) -ne $plan.PreviousSha256) { throw 'Restored VST3 hash mismatch' }
        Move-Item -LiteralPath $stateTemp -Destination $plan.StatePath -Force
    } catch {
        if (Test-Path -LiteralPath $plan.Target) {
            $failedDir = Join-Path $backupFull ('failed-rollback-' + $id)
            $null = New-Item -ItemType Directory -Path $failedDir -Force
            Move-Item -LiteralPath $plan.Target -Destination $failedDir
        }
        if ((Test-Path -LiteralPath $forwardBundle) -and -not (Test-Path -LiteralPath $plan.Target)) {
            Move-Item -LiteralPath $forwardBundle -Destination $plan.Target
        }
        throw
    } finally {
        if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
        if (Test-Path -LiteralPath $stateTemp) { Remove-Item -LiteralPath $stateTemp -Force }
    }
    Write-Host "Restored $($script:ProductName) $($plan.PreviousVersion). The newer version is preserved at $forwardBundle"
}

function Write-TestModuleInfo {
    param([string]$BundlePath, [string]$Version, [switch]$DuplicateIds)
    $secondId = if ($DuplicateIds) { 'ABCDEF019182FAEB536B6C79536B5254' } else { 'ABCDEF011234ABCD536B6C79536B5254' }
    $classes = @(
        [ordered]@{ CID = 'ABCDEF019182FAEB536B6C79536B5254'; Category = 'Audio Module Class'; Name = $script:ProductName; Vendor = 'Skelly Audio'; Version = $Version },
        [ordered]@{ CID = $secondId; Category = 'Component Controller Class'; Name = $script:ProductName; Vendor = 'Skelly Audio'; Version = $Version }
    )
    $document = [ordered]@{
        Name = $script:ProductName
        Version = $Version
        'Factory Info' = [ordered]@{ Vendor = 'Skelly Audio' }
        Classes = $classes
    }
    $path = Join-Path $BundlePath 'Contents/Resources/moduleinfo.json'
    [IO.File]::WriteAllText($path, (ConvertTo-Json -InputObject $document -Depth 8), [Text.UTF8Encoding]::new($false))
}

function Write-TestPe {
    param([string]$Path, [UInt16]$Machine = 0x8664)
    $bytes = New-Object byte[] 1024
    $bytes[0] = 0x4d
    $bytes[1] = 0x5a
    [BitConverter]::GetBytes([int]0x80).CopyTo($bytes, 0x3c)
    $bytes[0x80] = 0x50
    $bytes[0x81] = 0x45
    $bytes[0x82] = 0
    $bytes[0x83] = 0
    [BitConverter]::GetBytes($Machine).CopyTo($bytes, 0x84)
    [BitConverter]::GetBytes([UInt16]0x20b).CopyTo($bytes, 0x98)
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Write-TestBundle {
    param([string]$Root, [string]$Version, [switch]$DuplicateIds, [UInt16]$Machine = 0x8664)
    $bundle = Join-Path $Root $script:BundleName
    $moduleDir = Join-Path $bundle 'Contents/x86_64-win'
    $resourceDir = Join-Path $bundle 'Contents/Resources'
    $null = New-Item -ItemType Directory -Path $moduleDir -Force
    $null = New-Item -ItemType Directory -Path $resourceDir -Force
    Write-TestModuleInfo -BundlePath $bundle -Version $Version -DuplicateIds:$DuplicateIds
    Write-TestPe -Path (Join-Path $moduleDir $script:BundleName) -Machine $Machine
    return $bundle
}

function Write-TestChecksumManifest {
    param([string]$Root)
    $manifest = Join-Path $Root 'SHA256SUMS.txt'
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($file in (Get-ChildItem -LiteralPath $Root -File -Recurse -Force |
        Where-Object { $_.FullName -ne $manifest } | Sort-Object -Property FullName)) {
        $relative = [IO.Path]::GetRelativePath($Root, $file.FullName).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $lines.Add("$hash  $relative")
    }
    [IO.File]::WriteAllLines($manifest, $lines.ToArray(), [Text.UTF8Encoding]::new($false))
}

function Assert-Test {
    param([bool]$Condition, [string]$Label)
    if (-not $Condition) { throw "Installer self-test failed: $Label" }
    $script:SelfTestAssertions++
}

function Assert-TestFails {
    param([scriptblock]$Action, [string]$Label)
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    Assert-Test -Condition $failed -Label $Label
}

function Invoke-SelfTest {
    $script:SelfTestAssertions = 0
    $root = Join-Path ([IO.Path]::GetTempPath()) ('skellytunemini-installer-test-' + [guid]::NewGuid().ToString('N'))
    $packageRoot = Join-Path $root 'package'
    $destination = Join-Path $root 'VST3'
    $backupRoot = Join-Path $root 'Backups'
    try {
        $null = New-Item -ItemType Directory -Path $packageRoot -Force
        $bundle = Write-TestBundle -Root $packageRoot -Version '3.3.1'
        $buildLines = @('Product: SkellyTune Realtime', 'Version: 3.3.1', 'Target: Windows x64 VST3')
        [IO.File]::WriteAllLines((Join-Path $packageRoot 'BUILD-INFO.txt'), $buildLines, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $packageRoot 'LICENSE.txt'), 'test license')
        [IO.File]::WriteAllText((Join-Path $packageRoot 'THIRD_PARTY_NOTICES.md'), 'test notices')
        $null = New-Item -ItemType Directory -Path (Join-Path $packageRoot 'THIRD_PARTY_NOTICES') -Force
        [IO.File]::WriteAllText((Join-Path $packageRoot 'THIRD_PARTY_NOTICES/JUCE-LICENSE.md'), 'test JUCE notice')
        Write-TestChecksumManifest -Root $packageRoot
        $package = Test-PreviewPackage -PackageRoot $packageRoot
        Assert-Test -Condition ($package.Version -eq '3.3.1') -Label 'valid package accepted'

        Write-TestModuleInfo -BundlePath $bundle -Version '3.3.1' -DuplicateIds
        Write-TestChecksumManifest -Root $packageRoot
        Assert-TestFails -Action { Test-PreviewPackage -PackageRoot $packageRoot } -Label 'duplicate class IDs rejected'

        Write-TestModuleInfo -BundlePath $bundle -Version '3.3.1'
        $module = Join-Path $bundle 'Contents/x86_64-win/SkellyTune Realtime.vst3'
        Write-TestPe -Path $module -Machine 0xaa64
        Write-TestChecksumManifest -Root $packageRoot
        Assert-TestFails -Action { Test-PreviewPackage -PackageRoot $packageRoot } -Label 'non-AMD64 module rejected'

        Write-TestPe -Path $module
        Write-TestChecksumManifest -Root $packageRoot
        [IO.File]::WriteAllText((Join-Path $packageRoot 'LICENSE.txt'), 'tampered license')
        Assert-TestFails -Action { Test-PreviewPackage -PackageRoot $packageRoot } -Label 'checksum mismatch rejected'
        [IO.File]::WriteAllText((Join-Path $packageRoot 'LICENSE.txt'), 'test license')
        Write-TestChecksumManifest -Root $packageRoot
        $package = Test-PreviewPackage -PackageRoot $packageRoot

        $null = New-Item -ItemType Directory -Path $destination -Force
        $null = Write-TestBundle -Root $destination -Version '3.3.0'
        Invoke-InstallInternal -PackageRoot $packageRoot -Destination $destination -BackupRoot $backupRoot
        $target = Join-Path $destination $script:BundleName
        $installed = Test-InstalledBundle -BundlePath $target
        Assert-Test -Condition ($installed.Version -eq '3.3.1') -Label 'new preview installed'
        $state = [IO.File]::ReadAllText((Join-Path $backupRoot 'active-install.json')) | ConvertFrom-Json
        Assert-Test -Condition (Test-Path -LiteralPath $state.PreviousBundle -PathType Container) -Label 'previous version backup retained'

        Invoke-RollbackInternal -BackupRoot $backupRoot -Destination $destination
        $restored = Test-InstalledBundle -BundlePath $target
        Assert-Test -Condition ($restored.Version -eq '3.3.0') -Label 'rollback restores previous version'
        Invoke-RollbackInternal -BackupRoot $backupRoot -Destination $destination
        $forward = Test-InstalledBundle -BundlePath $target
        Assert-Test -Condition ($forward.Version -eq '3.3.1') -Label 'rollback is reversible and preserves newer version'

        $module = Join-Path $target 'Contents/x86_64-win/SkellyTune Realtime.vst3'
        $moduleBytes = [IO.File]::ReadAllBytes($module)
        $moduleBytes[300] = $moduleBytes[300] -bxor 1
        [IO.File]::WriteAllBytes($module, $moduleBytes)
        $tamperedHash = Get-TreeSha256 -Path $target
        Assert-TestFails -Action { Invoke-RollbackInternal -BackupRoot $backupRoot -Destination $destination } -Label 'modified installation blocks rollback'
        Assert-Test -Condition ((Get-TreeSha256 -Path $target) -eq $tamperedHash) -Label 'refused rollback leaves modified installation untouched'

        Write-Host "PASS: $script:SelfTestAssertions SkellyTune Mini installer/rollback assertions"
    } finally {
        if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
    }
}

if ($SelfTest) {
    if ($Rollback -or -not [string]::IsNullOrWhiteSpace($PackageRoot)) { throw '-SelfTest cannot be combined with install or rollback arguments' }
    Invoke-SelfTest
    return
}

if ([string]::IsNullOrWhiteSpace($BackupRoot)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) { throw 'LOCALAPPDATA is unavailable; pass -BackupRoot explicitly' }
    $BackupRoot = Join-Path $env:LOCALAPPDATA 'Skelly/Backups/SkellyTuneMini'
}
$BackupRoot = [IO.Path]::GetFullPath($BackupRoot)

if ($Rollback) {
    $plan = Get-RollbackPlan -BackupRoot $BackupRoot -Destination $Destination
    if ($PSCmdlet.ShouldProcess($plan.Target, "Restore verified SkellyTune Realtime $($plan.PreviousVersion)")) {
        Invoke-RollbackInternal -BackupRoot $BackupRoot -Destination $plan.Destination
    } else {
        Write-Host 'Rollback validation passed. No files changed.'
    }
} else {
    if ([string]::IsNullOrWhiteSpace($PackageRoot)) { throw 'Pass -PackageRoot to install, or -Rollback to restore the previous version' }
    if ([string]::IsNullOrWhiteSpace($Destination)) {
        $commonFiles = $env:CommonProgramFiles
        if ([string]::IsNullOrWhiteSpace($commonFiles)) {
            if ([string]::IsNullOrWhiteSpace($env:ProgramFiles)) { throw 'Common Program Files is unavailable; pass -Destination explicitly' }
            $commonFiles = Join-Path $env:ProgramFiles 'Common Files'
        }
        $Destination = Join-Path $commonFiles 'VST3'
    }
    $package = Test-PreviewPackage -PackageRoot $PackageRoot
    $target = Join-Path ([IO.Path]::GetFullPath($Destination)) $script:BundleName
    if ($PSCmdlet.ShouldProcess($target, "Install validated SkellyTune Realtime $($package.Version) with backup/rollback")) {
        Invoke-InstallInternal -PackageRoot $PackageRoot -Destination $Destination -BackupRoot $BackupRoot
    } else {
        Write-Host 'Package validation passed. No files changed.'
    }
}
