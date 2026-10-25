# hidset

A tiny replacement for SteelSeries GG and Razer Synapse. It writes fixed
settings to the devices, saves them in each device's onboard memory and exits.
Nothing stays running: no service, no tray app, no autostart, no vendor drivers.
It talks to the devices through the inbox Windows HID stack only (`hid.dll`,
`setupapi.dll`).

| Device | Settings |
|---|---|
| SteelSeries Rival 600 (`1038:1724`) | CPI 800 / 1200, 1000 Hz polling, all 8 LED zones off |
| SteelSeries Arctis Nova 3 (`1038:12EC`) | EQ flat, mic volume 8, sidetone 1, muted-mic LED 2 |
| Razer DeathStalker V2 Pro (`1532:0290` dongle, `1532:0292` wired) | Static white, brightness 56/255 |

The values are constants at the top of [`hidset.c`](hidset.c).

## Usage

```
hidset            apply and save all settings
hidset --dry      print the reports that would be sent, send nothing
hidset --list     show which HID collections were found
hidset --read     read the keyboard's current brightness
```

## Build

Any MinGW-w64 toolchain (tested with llvm-mingw):

```
x86_64-w64-mingw32-gcc -O2 -s -Wall -Wextra -o hidset.exe hidset.c -lhid -lsetupapi
```

or run `build.ps1`, which looks for llvm-mingw in `%LOCALAPPDATA%`.

## Scripts (run as administrator)

| Script | Purpose |
|---|---|
| `cleanup.ps1` | One-off: uninstalls GG and Synapse, deletes their driver packages, blocks GG's reinstall hooks, runs `hidset.exe`, blocks the mouse control channel. Creates a restore point first. |
| `mouse-channel.ps1 block\|unblock` | Blocks the Rival 600's settings/lighting interface (`HID\VID_1038&PID_1724&MI_00`) with the device-installation policy, so no program can change its lights. The mouse keeps working. Unblock before running `hidset.exe` for mouse changes. |
| `hide-updates.ps1 [-Show]` | Hides SteelSeries and Razer driver offers in Windows Update. Re-run when a new version is offered. |

### Why the blocks matter

GG installs a "GG Install Component" software component (`SWC\VEN_SSGG&IID_0100`)
and an extension driver for the devices. Windows Update re-downloads them within
a minute of their removal and they reinstall GG. `cleanup.ps1` denies those IDs
under `HKLM\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions\DenyDeviceIDs`.
Driver updates whose hardware ID is the device itself cannot be denied without
disabling the device, which is what `hide-updates.ps1` is for.

## Protocol notes

- **Rival 600** — interface 0, vendor page `FFC0`. Output reports (33 bytes) for
  CPI `03 00 <n> <cpi/100-1> 00 42`, polling `04 00 <code>`, save `09 00`;
  feature reports (579 bytes) `05 00 <led> ...` for LED colours.
  Byte layout matches [rivalcfg](https://github.com/flozz/rivalcfg).
- **Arctis Nova 3** — interface 4, collection 1, page `FFC0`, report ID 6.
  Commands from [HeadsetControl](https://github.com/Sapd/HeadsetControl):
  EQ `06 33 <6 bands, 0x14 + 2*dB>`, mic `06 37`, sidetone `06 39`,
  mic LED `06 AE`, save `06 09`. On Windows the headset times out on the
  520-byte feature report, so they are sent as 64-byte output reports.
- **DeathStalker V2 Pro** — 90-byte Razer feature reports per
  [openrazer](https://github.com/openrazer/openrazer); dongle on interface 2
  (usage `01:02`, transaction ID `0x9F`), wired on interface 3 (usage `0C:01`,
  `0x1F`), as detected by [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB).
  Static effect `0F:02`, brightness `0F:04` / `0F:84`, both in VARSTORE so they
  persist. Bluetooth mode is not supported.

Unofficial; not affiliated with SteelSeries or Razer.

## License

GPL-3.0, see [LICENSE](LICENSE).
