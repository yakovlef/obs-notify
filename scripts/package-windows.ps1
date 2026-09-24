[CmdletBinding()]
param(
    [string] $BuildDirectory = "build",
    [string] $OutputDirectory = "release",
    [string] $Version,
    [switch] $BuildInstaller
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildSpec = Get-Content -LiteralPath (Join-Path $ProjectRoot "buildspec.json") -Raw | ConvertFrom-Json
if (-not $Version) {
    $Version = $BuildSpec.version
}

$BuildRoot = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $BuildDirectory))
$OutputRoot = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $OutputDirectory))
$PluginStage = Join-Path $OutputRoot "stage\stream-notify"
$PluginBin = Join-Path $PluginStage "bin\64bit"
$PluginData = Join-Path $PluginStage "data"
$PluginLocale = Join-Path $PluginData "locale"
$PluginTls = Join-Path $PluginData "tls"
$DllPath = Join-Path $BuildRoot "Release\stream-notify.dll"
$QtTlsPath = Join-Path $ProjectRoot ".deps\obs-deps-qt6-$($BuildSpec.dependencies.qt6.version)-x64\plugins\tls\qschannelbackend.dll"

if (-not (Test-Path -LiteralPath $DllPath)) {
    throw "Release DLL not found: $DllPath"
}
if (-not (Test-Path -LiteralPath $QtTlsPath)) {
    throw "Qt Schannel backend not found: $QtTlsPath"
}

if (Test-Path -LiteralPath $OutputRoot) {
    $ResolvedProject = [System.IO.Path]::GetFullPath($ProjectRoot)
    if (-not $OutputRoot.StartsWith($ResolvedProject, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Output directory must be inside the project: $OutputRoot"
    }
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $PluginBin, $PluginLocale, $PluginTls | Out-Null
Copy-Item -LiteralPath $DllPath -Destination (Join-Path $PluginBin "stream-notify.dll")
Copy-Item -LiteralPath (Join-Path $ProjectRoot "data\locale\en-US.ini") -Destination $PluginLocale
Copy-Item -LiteralPath (Join-Path $ProjectRoot "data\locale\ru-RU.ini") -Destination $PluginLocale
Copy-Item -LiteralPath $QtTlsPath -Destination (Join-Path $PluginTls "qschannelbackend.dll")
Copy-Item -LiteralPath (Join-Path $ProjectRoot "LICENSE") -Destination (Join-Path $PluginData "LICENSE")

$ArchivePath = Join-Path $OutputRoot "stream-notify-$Version-windows-x64.zip"
Compress-Archive -LiteralPath $PluginStage -DestinationPath $ArchivePath -CompressionLevel Optimal

if ($BuildInstaller) {
    $IsccCandidates = @(
        (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source,
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    $Iscc = $IsccCandidates | Select-Object -First 1
    if (-not $Iscc) {
        throw "Inno Setup 6 was not found. Install it or run without -BuildInstaller."
    }

    $InstallerScript = Join-Path $ProjectRoot "installer\stream-notify.iss"
    & $Iscc "/DMyAppVersion=$Version" "/DSourceDir=$PluginStage" "/DOutputDir=$OutputRoot" $InstallerScript
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup failed with exit code $LASTEXITCODE"
    }
}

$Artifacts = Get-ChildItem -LiteralPath $OutputRoot -File | Sort-Object Name
$ChecksumPath = Join-Path $OutputRoot "SHA256SUMS.txt"
$ChecksumLines = foreach ($Artifact in $Artifacts) {
    $Hash = (Get-FileHash -LiteralPath $Artifact.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$Hash  $($Artifact.Name)"
}
$ChecksumLines | Set-Content -LiteralPath $ChecksumPath -Encoding utf8NoBOM

Get-ChildItem -LiteralPath $OutputRoot -File | Select-Object Name, Length
