# Blocks or unblocks the Rival 600's settings/lighting channel (HID interface 0).
# While blocked, no program can change the mouse's lights or settings; the
# mouse itself (interfaces 1 and 2) keeps working. Needs admin.
#
#   .\mouse-channel.ps1 block
#   .\mouse-channel.ps1 unblock     (e.g. before running hidset.exe again)
param([Parameter(Mandatory)][ValidateSet('block', 'unblock')][string]$Action)

$ErrorActionPreference = 'Stop'
$id = 'HID\VID_1038&PID_1724&MI_00'
$key = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions'
$list = "$key\DenyDeviceIDs"

function Get-Entries {
    if (-not (Test-Path $list)) { return @() }
    $p = Get-ItemProperty $list
    $p.PSObject.Properties | Where-Object { $_.Name -match '^\d+$' }
}

if ($Action -eq 'block') {
    # New-Item -Force would recreate the key and drop existing entries.
    if (-not (Test-Path $list)) { New-Item $list -Force | Out-Null }
    Set-ItemProperty $key DenyDeviceIDs 1 -Type DWord
    if (-not (Get-Entries | Where-Object Value -eq $id)) {
        $n = ((Get-Entries | ForEach-Object { [int]$_.Name }) + 0 | Measure-Object -Maximum).Maximum + 1
        Set-ItemProperty $list "$n" $id -Type String
    }
    # The policy stops new installs; remove the instances already present.
    Get-PnpDevice | Where-Object InstanceId -like "$id\*" | ForEach-Object {
        pnputil /remove-device "$($_.InstanceId)" | Out-Null
    }
    Write-Host "Mouse control channel blocked."
} else {
    Get-Entries | Where-Object Value -eq $id | ForEach-Object { Remove-ItemProperty $list $_.Name }
    pnputil /scan-devices | Out-Null
    Start-Sleep -Seconds 2
    Write-Host "Mouse control channel unblocked. Run hidset.exe, then block again."
}
