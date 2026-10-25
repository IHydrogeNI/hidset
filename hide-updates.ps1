# Hides SteelSeries and Razer driver offers in Windows Update so they are never
# installed. Run elevated. Re-run if Windows Update offers a new version.
#   .\hide-updates.ps1          hide them
#   .\hide-updates.ps1 -Show    list hidden vendor updates and unhide them
param([switch]$Show)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Start-Transcript -Path "$here\hide-updates.log" -Force | Out-Null

$searcher = (New-Object -ComObject Microsoft.Update.Session).CreateUpdateSearcher()
$hidden = if ($Show) { 1 } else { 0 }
$result = $searcher.Search("IsInstalled=0 and IsHidden=$hidden")
foreach ($u in $result.Updates) {
    if ("$($u.DriverManufacturer) $($u.Title)" -notmatch 'SteelSeries|Razer') { continue }
    $u.IsHidden = -not $Show
    Write-Host "$(if ($Show) { 'unhidden' } else { 'hidden  ' })  $($u.Title)  [$($u.DriverHardwareID)]"
}
Stop-Transcript | Out-Null
