# Blocks or unblocks the Rival 600's settings/lighting channel (USB interface 0).
# While blocked, no program can change the mouse's lights or settings; the
# mouse itself (interfaces 1 and 2) keeps working. Needs admin.
#
#   .\mouse-channel.ps1 block
#   .\mouse-channel.ps1 unblock     (e.g. before running hidset.exe again)
#
# The USB interface is denied rather than its HID collection: HID collections
# are raw devices that programs can open even when their install is blocked,
# but without a driver on the USB interface no HID collection is created.
param([Parameter(Mandatory)][ValidateSet('block', 'unblock')][string]$Action)

$ErrorActionPreference = 'Stop'
$id = 'USB\VID_1038&PID_1724&MI_00'
$legacy = 'HID\VID_1038&PID_1724&MI_00'   # used by earlier versions of this script
$key = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions'
$list = "$key\DenyDeviceIDs"

function Get-Entries {
    if (-not (Test-Path $list)) { return @() }
    $p = Get-ItemProperty $list
    $p.PSObject.Properties | Where-Object { $_.Name -match '^\d+$' }
}

function Remove-Instances($prefix) {
    Get-PnpDevice | Where-Object InstanceId -like "$prefix\*" | ForEach-Object {
        pnputil /remove-device "$($_.InstanceId)" | Out-Null
    }
}

Get-Entries | Where-Object Value -eq $legacy | ForEach-Object { Remove-ItemProperty $list $_.Name }

if ($Action -eq 'block') {
    # New-Item -Force would recreate the key and drop existing entries.
    if (-not (Test-Path $list)) { New-Item $list -Force | Out-Null }
    Set-ItemProperty $key DenyDeviceIDs 1 -Type DWord
    if (-not (Get-Entries | Where-Object Value -eq $id)) {
        $n = (@(Get-Entries | ForEach-Object { [int]$_.Name }) + 0 | Measure-Object -Maximum).Maximum + 1
        Set-ItemProperty $list "$n" $id -Type String
    }
    # The policy stops new installs; remove the instances already present and
    # let Windows re-enumerate them under the policy.
    Remove-Instances $legacy
    Remove-Instances $id
    pnputil /scan-devices | Out-Null
    Start-Sleep -Seconds 2
    Write-Host "Mouse control channel blocked."
} else {
    Get-Entries | Where-Object Value -eq $id | ForEach-Object { Remove-ItemProperty $list $_.Name }
    Remove-Instances $id
    pnputil /scan-devices | Out-Null
    Start-Sleep -Seconds 2
    Write-Host "Mouse control channel unblocked. Run hidset.exe, then block again."
}
