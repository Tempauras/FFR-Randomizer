param(
    [string]$EngineDir,
    [string]$GameDir,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms

function Test-EngineDir([string]$Dir) {
    $VersionFile = Join-Path $Dir 'Engine\Build\Build.version'
    if (-not (Test-Path $VersionFile)) { return $false }
    $Version = Get-Content $VersionFile -Raw | ConvertFrom-Json
    return ($Version.MajorVersion -eq 5 -and $Version.MinorVersion -eq 6)
}

function Test-GameDir([string]$Dir) {
    return (Test-Path (Join-Path $Dir 'FFRS\Content\Paks'))
}

function Get-FirstExisting([string[]]$Folders) {
    return $Folders | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
}

function Select-Folder([string]$Description, [scriptblock]$IsValid, [string]$Invalid, [string]$StartAt) {
	$Owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true }
	$Dialog = New-Object System.Windows.Forms.FolderBrowserDialog -Property @{
		Description = $Description
        	ShowNewFolderButton = $false
    	}
    if ($StartAt) { $Dialog.SelectedPath = $StartAt }
    try {
        while ($true) {
            if ($Dialog.ShowDialog($Owner) -ne [System.Windows.Forms.DialogResult]::OK) {
                throw 'Cancelled: no folder was selected.'
            }
            if (& $IsValid $Dialog.SelectedPath) { return $Dialog.SelectedPath }
            [void][System.Windows.Forms.MessageBox]::Show($Owner, $Invalid, 'FFR Randomizer')
        }
    }
    finally {
        $Dialog.Dispose()
        $Owner.Dispose()
    }
}

$Project = Split-Path $PSScriptRoot -Parent
$PathsFile = "$PSScriptRoot\BuildPaths.local.json"
$Saved = if (Test-Path $PathsFile) { Get-Content $PathsFile -Raw | ConvertFrom-Json }

if ($EngineDir -and -not (Test-EngineDir $EngineDir)) { throw "$EngineDir is not Unreal Engine 5.6" }
if ($GameDir -and -not (Test-GameDir $GameDir)) { throw "$GameDir is not the game folder" }
if (-not $EngineDir -and $Saved -and $Saved.EngineDir -and (Test-EngineDir $Saved.EngineDir)) { $EngineDir = $Saved.EngineDir }
if (-not $GameDir -and $Saved -and $Saved.GameDir -and (Test-GameDir $Saved.GameDir)) { $GameDir = $Saved.GameDir }

if (-not $EngineDir) {
    $EpicGames = "$env:ProgramFiles\Epic Games"
    $Start = Get-FirstExisting @("$EpicGames\UE_5.6", $EpicGames, $env:ProgramFiles)
    $EngineDir = Select-Folder 'Select your Unreal Engine 5.6 folder (usually Epic Games\UE_5.6)' ${function:Test-EngineDir} 'That folder is not Unreal Engine 5.6. Select the folder that contains "Engine".' $Start
}
if ($Install -and -not $GameDir) {
    # Steam records where it installed the game, which may be another drive's library.
    $SteamEntry = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 3259780' -ErrorAction SilentlyContinue
    $SteamCommon = "${env:ProgramFiles(x86)}\Steam\steamapps\common"
    $Start = Get-FirstExisting @($(if ($SteamEntry) { $SteamEntry.InstallLocation }), "$SteamCommon\FINAL FANTASY RESONANCE", $SteamCommon)
    $GameDir = Select-Folder 'Select your FINAL FANTASY RESONANCE game folder' ${function:Test-GameDir} 'That is not the game folder. Select the folder that contains "FFRS".' $Start
}

@{ EngineDir = $EngineDir; GameDir = $GameDir } | ConvertTo-Json | Set-Content $PathsFile -Encoding UTF8

$Uproject = "$Project\FFRS.uproject"
$Engine = "$EngineDir\Engine"
$Editor = "$Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$UnrealPak = "$Engine\Binaries\Win64\UnrealPak.exe"
$Cooked = "$Project\Saved\Cooked\Windows"
$ModName = 'FFRRandomizer'
$Assets = @('WBP_SeedPanel')

& "$Engine\Build\BatchFiles\Build.bat" FFRSEditor Win64 Development "-Project=$Uproject" -WaitMutex -NoHotReload | Select-String ': error|Result:'
if ($LASTEXITCODE -ne 0) { throw 'The editor tool did not build' }

Remove-Item -LiteralPath "$Project\Content\Mods\$ModName" -Recurse -Force -ErrorAction SilentlyContinue
& $Editor $Uproject -run=BuildFFRRandomizerUI -unattended -nop4 -nosplash -stdout -AllowStdOutLogVerbosity | Select-String 'LogFFRRandomizerUI'
if ($LASTEXITCODE -ne 0) { throw 'The assets were not generated' }

$Packages = @($Assets | ForEach-Object { "-PACKAGE=/Game/Mods/$ModName/$_" })
& $Editor $Uproject -run=Cook -TargetPlatform=Windows -unattended -nop4 -nosplash -unversioned -cooksinglepackage @Packages -stdout -AllowStdOutLogVerbosity |
    Select-String 'Success - |Failure - '
if ($LASTEXITCODE -ne 0) { throw 'The cook failed' }

# Need to replace forward slash by backward ones for UnrealPak
$Work = (Join-Path ([IO.Path]::GetTempPath()) "$ModName-Pack") -replace '\\', '/'
Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Work | Out-Null
$Response = foreach ($Asset in $Assets) {
    foreach ($Extension in 'uasset', 'uexp') {
        $Source = "$Cooked\FFRS\Content\Mods\$ModName\$Asset.$Extension"
        "`"$Source`" `"../../../FFRS/Content/Mods/$ModName/$Asset.$Extension`""
    }
}
$Ascii = New-Object Text.ASCIIEncoding
[IO.File]::WriteAllLines("$Work/response.txt", [string[]]$Response, $Ascii)
[IO.File]::WriteAllText("$Work/empty.txt", '', $Ascii)
[IO.File]::WriteAllText("$Work/commands.txt", "-Output=`"$Work/$ModName.utoc`" -ContainerName=$ModName -ResponseFile=`"$Work/response.txt`"`r`n", $Ascii)

$Metadata = "$Cooked\FFRS\Metadata"
& $UnrealPak "-CreateGlobalContainer=$Work/global.utoc" "-PackageStoreManifest=$Metadata\packagestore.manifest" "-CookedDirectory=$Cooked" `
    "-Commands=$Work/commands.txt" "-ScriptObjects=$Metadata\scriptobjects.bin" -patchpaddingalign=2048 -compressionformats=Oodle `
    -compresslevel=4 -compressmethod=Kraken -WriteBackMetadataToAssetRegistry=Disabled -nopak | Select-String 'LogIoStore: Error'
if ($LASTEXITCODE -ne 0) { throw 'The container was not created' }

& $UnrealPak "$Work/$ModName.pak" "-create=$Work/empty.txt" | Select-String 'LogPakFile: Error'
if ($LASTEXITCODE -ne 0) { throw 'The pak was not created' }

$Dist = "$Project\Dist\$ModName"
New-Item -ItemType Directory -Force $Dist | Out-Null
foreach ($Extension in 'pak', 'ucas', 'utoc') { Copy-Item "$Work/$ModName.$Extension" $Dist -Force }

if ($Install) {
    # The panel is loaded by the DLL, so the pak goes in ~mods, which the engine mounts on its own. An older install
    # in LogicMods is removed so the same assets are not mounted twice.
    $Mods = "$GameDir\FFRS\Content\Paks\~mods"
    New-Item -ItemType Directory -Force $Mods | Out-Null
    Copy-Item "$Dist\$ModName.*" $Mods -Force
    Remove-Item "$GameDir\FFRS\Content\Paks\LogicMods\$ModName.*" -Force -ErrorAction SilentlyContinue
}
Get-ChildItem $Dist | Select-Object Name, Length
exit 0
