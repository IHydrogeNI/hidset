# One-off removal of SteelSeries GG and Razer Synapse, their drivers and
# auto-reinstall hooks. Run elevated. Log: cleanup.log next to this script.
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Start-Transcript -Path "$here\cleanup.log" -Force | Out-Null

function Step($t) { Write-Host "`n=== $t" }

function Wait-Uninstallers {
    # NSIS-style uninstallers hand off to a temp copy and return early.
    for ($i = 0; $i -lt 180; $i++) {
        $p = Get-Process | Where-Object { $_.Name -match '^(Au_|Un_A|uninst|Uninstall)' }
        if (-not $p) { return }
        Start-Sleep -Seconds 1
    }
    Write-Host "uninstaller still running after 3 minutes, continuing"
}

Step "Restore point"
try {
    Enable-ComputerRestore -Drive "$env:SystemDrive\" -ErrorAction Stop
    Checkpoint-Computer -Description "Before GG/Synapse removal" -RestorePointType MODIFY_SETTINGS -ErrorAction Stop
    Write-Host "created"
} catch { Write-Host "skipped: $($_.Exception.Message)" }

Step "Backup GG databases"
$bk = "$here\backup\gg-db"
New-Item $bk -ItemType Directory -Force | Out-Null
Get-ChildItem 'C:\ProgramData\SteelSeries\GG' -Recurse -File -Include '*.db', '*.db-wal', '*.db-shm', '*.json' -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item $_.FullName (Join-Path $bk ($_.FullName.Substring(29) -replace '[\\:]', '_')) -Force }
Write-Host "$((Get-ChildItem $bk).Count) files"

Step "Stop SteelSeries / Razer processes"
Get-Process | Where-Object { $_.Name -match '^(SteelSeries|Razer|Rz)' } | Stop-Process -Force -ErrorAction SilentlyContinue

Step "Uninstall SteelSeries GG"
$gg = 'C:\Program Files\SteelSeries\GG\uninst.exe'
if (Test-Path $gg) { Start-Process $gg '/S' -Wait; Wait-Uninstallers } else { Write-Host "not installed" }

Step "Uninstall Razer Synapse"
$rz = 'C:\WINDOWS\Installer\Razer\Razer Synapse\Uninstall.exe'
if (Test-Path $rz) {
    Start-Process $rz '"/U:C:\WINDOWS\Installer\Razer\Razer Synapse\uninstall.xml" /S' -Wait; Wait-Uninstallers
} else { Write-Host "not installed" }
Get-Process | Where-Object { $_.Name -match '^(SteelSeries|Razer|Rz)' } | Stop-Process -Force -ErrorAction SilentlyContinue

Step "Delete driver packages"
$names = 'rz0290dev.inf', 'rz0291dev.inf', 'rz0292dev.inf', 'ssbthid.inf', 'ssdevfactory.inf', 'sshid.inf',
         'steelseries-sonar-vad-extension.inf', 'steelseriesggext.inf', 'sonar.apo.inf',
         'steelseries-sonar-vad.inf', 'steelseriesggswc.inf'
$blocks = ((pnputil /enum-drivers) -join "`n") -split "`n\s*`n"
foreach ($b in $blocks) {
    if ($b -match 'Published Name:\s+(\S+)' ) { $pub = $Matches[1] } else { continue }
    if ($b -match 'Original Name:\s+(\S+)' -and $names -contains $Matches[1].ToLower()) {
        Write-Host "$pub ($($Matches[1]))"
        pnputil /delete-driver $pub /uninstall /force | Out-Host
    }
}

Step "Remove leftover SteelSeries devices"
Get-PnpDevice | Where-Object {
    $_.InstanceId -match 'STEELSERIES_GG_INSTALL_COMPONENT|^ROOT\\SYSTEM\\' -and $_.FriendlyName -match 'SteelSeries' -or
    $_.FriendlyName -match 'Sonar'
} | ForEach-Object { Write-Host $_.InstanceId; pnputil /remove-device "$($_.InstanceId)" | Out-Null }
pnputil /scan-devices | Out-Null
Start-Sleep -Seconds 5

Step "Block GG reinstall hooks"
$key = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions'
$list = "$key\DenyDeviceIDs"
if (-not (Test-Path $list)) { New-Item $list -Force | Out-Null }
Set-ItemProperty $key DenyDeviceIDs 1 -Type DWord
$deny = 'SWC\VEN_SSGG&IID_0100', 'SWC\VEN_SSGG&AID_0001', 'ROOT\VEN_SSGG&DEV_0001', 'root\ssdevfactory'
$have = (Get-ItemProperty $list).PSObject.Properties | Where-Object { $_.Name -match '^\d+$' }
$n = (($have | ForEach-Object { [int]$_.Name }) + 0 | Measure-Object -Maximum).Maximum
foreach ($id in $deny) {
    if (-not ($have | Where-Object Value -eq $id)) { $n++; Set-ItemProperty $list "$n" $id -Type String; Write-Host "denied $id" }
}

Step "Apply device settings (hidset)"
& "$here\hidset.exe" | Out-Host

Step "Block mouse control channel"
& "$here\mouse-channel.ps1" block

Step "Remove leftovers"
foreach ($svc in 'Razer Elevation Service', 'SteelSeriesGGUpdateServiceProxy') {
    if (Get-Service $svc -ErrorAction SilentlyContinue) { Stop-Service $svc -Force -ErrorAction SilentlyContinue; sc.exe delete "$svc" | Out-Host }
}
Remove-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Run' 'SteelSeriesGG' -ErrorAction SilentlyContinue
$dirs = 'C:\Program Files\SteelSeries', 'C:\ProgramData\SteelSeries', 'C:\Program Files\Razer',
        'C:\Program Files (x86)\Razer', 'C:\ProgramData\Razer', 'C:\WINDOWS\Installer\Razer',
        "$env:LOCALAPPDATA\SteelSeries", "$env:APPDATA\steelseries-gg-client", "$env:APPDATA\steelseries-engine-3-client",
        'C:\ProgramData\Microsoft\Windows\Start Menu\Programs\SteelSeries', 'C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Razer Synapse'
foreach ($d in $dirs) { if (Test-Path $d) { Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue; Write-Host "removed $d$(if (Test-Path $d) { ' (partly, in use)' })" } }

Step "Check"
Get-Process | Where-Object { $_.Name -match '^(SteelSeries|Razer|Rz)' } | Format-Table Name, Id
Get-ItemProperty HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*, HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\* -ErrorAction SilentlyContinue |
    Where-Object { $_.DisplayName -match 'SteelSeries|Razer' } | Format-Table DisplayName
Get-CimInstance Win32_PnPSignedDriver | Where-Object { $_.DriverProviderName -match 'SteelSeries|Razer' } | Format-Table DeviceName, InfName
Write-Host "done"
Stop-Transcript | Out-Null
