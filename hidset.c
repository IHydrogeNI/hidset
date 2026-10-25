/*
 * hidset - applies fixed settings to the user's peripherals, saves them to
 * each device's onboard memory and exits. Uses only the inbox Windows HID
 * stack (hid.dll, setupapi.dll); no drivers, no service, nothing resident.
 *
 * Protocols taken from open-source references:
 *   SteelSeries Rival 600      rivalcfg (flozz/rivalcfg)
 *   SteelSeries Arctis Nova 3  HeadsetControl (Sapd/HeadsetControl)
 *   Razer DeathStalker V2 Pro  openrazer / OpenRGB
 *
 * Usage: hidset            apply settings
 *        hidset --dry      print what would be sent, touch nothing
 *        hidset --list     list matching HID collections
 *        hidset --read     read the keyboard's current brightness
 */
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* ---- Settings (taken from the user's SteelSeries GG profile) ---------- */

#define RIVAL_CPI1        800
#define RIVAL_CPI2        1200
#define RIVAL_POLL_CODE   0x01   /* 1000 Hz (0x02 500, 0x03 250, 0x04 125) */

#define NOVA_EQ_DB        { 0, 0, 0, 0, 0, 0 }  /* 6 bands, -6..+6, 0.5 steps */
#define NOVA_MIC_VOLUME   8      /* GG level, 0..10 */
#define NOVA_SIDETONE     1      /* 0..3 */
#define NOVA_MIC_LED      2      /* muted-mic LED brightness, 0..3 */

#define KBD_R 0xFF
#define KBD_G 0xFF
#define KBD_B 0xFF
#define KBD_BRIGHTNESS    56     /* 0..255, user's choice via Fn keys (22%) */

/* ---- HID plumbing ------------------------------------------------------ */

typedef struct {
    HANDLE h;
    HIDP_CAPS caps;
    BOOL can_write;
    wchar_t path[512];
} hiddev;

static int g_dry;

/* Finds a HID collection by VID/PID, interface number and usage. */
static BOOL hid_find(uint16_t vid, uint16_t pid, int mi, USHORT page, USHORT usage, hiddev *out)
{
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO set = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return FALSE;

    wchar_t want[64];
    swprintf(want, 64, L"vid_%04x&pid_%04x&mi_%02x", vid, pid, mi);

    BOOL found = FALSE;
    SP_DEVICE_INTERFACE_DATA ifd = { .cbSize = sizeof(ifd) };
    for (DWORD i = 0; !found && SetupDiEnumDeviceInterfaces(set, NULL, &guid, i, &ifd); i++) {
        BYTE buf[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + 1024];
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *det = (void *)buf;
        det->cbSize = sizeof(*det);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, sizeof(buf), NULL, NULL))
            continue;

        wchar_t lower[512];
        wcsncpy(lower, det->DevicePath, 511);
        lower[511] = 0;
        _wcslwr(lower);
        if (!wcsstr(lower, want))
            continue;

        /* System-owned collections (mouse, keyboard) refuse read/write access
           but still accept feature reports on a zero-access handle. */
        BOOL rw = TRUE;
        HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            rw = FALSE;
            h = CreateFileW(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, 0, NULL);
        }
        if (h == INVALID_HANDLE_VALUE)
            continue;

        PHIDP_PREPARSED_DATA pp;
        HIDP_CAPS caps;
        BOOL ok = HidD_GetPreparsedData(h, &pp);
        if (ok) {
            ok = HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS;
            HidD_FreePreparsedData(pp);
        }
        /* page 0 = any vendor-defined page, usage 0 = any usage */
        BOOL page_ok = page ? caps.UsagePage == page : caps.UsagePage >= 0xFF00;
        BOOL usage_ok = !usage || caps.Usage == usage;
        if (ok && page_ok && usage_ok) {
            out->h = h;
            out->caps = caps;
            out->can_write = rw;
            wcsncpy(out->path, det->DevicePath, 511);
            out->path[511] = 0;
            found = TRUE;
        } else {
            CloseHandle(h);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

static void dump(const char *what, const uint8_t *b, size_t n)
{
    /* Trailing zero padding is noise; show up to the last non-zero byte. */
    size_t last = n;
    while (last > 1 && b[last - 1] == 0)
        last--;
    printf("    %-7s", what);
    for (size_t i = 0; i < last; i++)
        printf(" %02x", b[i]);
    if (last < n)
        printf(" ... (%zu bytes)", n);
    printf("\n");
}

/* data[0] is the report ID. Pads to the collection's report length. */
static BOOL send_feature(hiddev *d, const uint8_t *data, size_t n)
{
    uint8_t buf[1024] = { 0 };
    size_t len = d->caps.FeatureReportByteLength;
    if (n > len || len > sizeof(buf)) {
        printf("    feature report too long (%zu > %zu)\n", n, len);
        return FALSE;
    }
    memcpy(buf, data, n);
    if (g_dry) {
        dump("feature", buf, len);
        return TRUE;
    }
    if (!HidD_SetFeature(d->h, buf, (ULONG)len)) {
        printf("    HidD_SetFeature failed (error %lu)\n", GetLastError());
        return FALSE;
    }
    return TRUE;
}

static BOOL send_output(hiddev *d, const uint8_t *data, size_t n)
{
    uint8_t buf[1024] = { 0 };
    size_t len = d->caps.OutputReportByteLength;
    if (n > len || len > sizeof(buf)) {
        printf("    output report too long (%zu > %zu)\n", n, len);
        return FALSE;
    }
    memcpy(buf, data, n);
    if (g_dry) {
        dump("output", buf, len);
        return TRUE;
    }
    DWORD written;
    BOOL ok = d->can_write ? WriteFile(d->h, buf, (DWORD)len, &written, NULL)
                           : HidD_SetOutputReport(d->h, buf, (ULONG)len);
    if (!ok)
        printf("    output report failed (error %lu)\n", GetLastError());
    return ok;
}

/* ---- SteelSeries Rival 600 (rivalcfg profile rival600.py) -------------- */

static BOOL rival600_apply(void)
{
    hiddev d;
    /* rivalcfg only pins the interface; its only collection is vendor-defined. */
    if (!hid_find(0x1038, 0x1724, 0, 0, 0, &d)) {
        printf("Rival 600: control channel not available (blocked? run mouse-channel.ps1 unblock)\n");
        return FALSE;
    }
    printf("Rival 600: found (feature %u, output %u bytes)\n",
           d.caps.FeatureReportByteLength, d.caps.OutputReportByteLength);

    BOOL ok = TRUE;
    uint8_t cpi1[] = { 0x00, 0x03, 0x00, 0x01, RIVAL_CPI1 / 100 - 1, 0x00, 0x42 };
    uint8_t cpi2[] = { 0x00, 0x03, 0x00, 0x02, RIVAL_CPI2 / 100 - 1, 0x00, 0x42 };
    uint8_t poll[] = { 0x00, 0x04, 0x00, RIVAL_POLL_CODE };
    ok &= send_output(&d, cpi1, sizeof(cpi1));
    Sleep(20);
    ok &= send_output(&d, cpi2, sizeof(cpi2));
    Sleep(20);
    ok &= send_output(&d, poll, sizeof(poll));
    Sleep(20);

    /* All 8 LED zones: single static colour, black. Byte layout as produced
       by rivalcfg's rgbgradient handler for a plain colour. */
    for (uint8_t led = 0; led < 8; led++) {
        uint8_t c[39] = { 0 };
        c[0] = 0x00;           /* report ID */
        c[1] = 0x05;           /* command */
        c[2] = 0x00;
        c[3] = led;            /* header[0]: led id */
        c[8] = led;            /* header[5]: led id */
        c[9] = 0xE8;           /* header[6..7]: duration 1000 ms */
        c[10] = 0x03;
        c[25] = 0x01;          /* header[22]: repeat (static) */
        c[30] = 0x01;          /* header[27]: colour count */
        /* body: r g b of the colour stops, all zero = off */
        ok &= send_feature(&d, c, sizeof(c));
        Sleep(20);
    }

    uint8_t save[] = { 0x00, 0x09, 0x00 };
    ok &= send_output(&d, save, sizeof(save));
    CloseHandle(d.h);
    printf("Rival 600: %s\n", ok ? "CPI 800/1200, 1000 Hz, LEDs off, saved" : "FAILED");
    return ok;
}

/* ---- SteelSeries Arctis Nova 3 (HeadsetControl) ------------------------ */

static BOOL nova3_apply(void)
{
    hiddev d;
    if (!hid_find(0x1038, 0x12EC, 4, 0xFFC0, 0x0001, &d)) {
        printf("Arctis Nova 3: not found\n");
        return FALSE;
    }
    printf("Arctis Nova 3: found (output %u bytes)\n", d.caps.OutputReportByteLength);

    /* Report 6 is declared as a 64-byte output and a 520-byte feature report;
       the headset times out on the feature path, the output path works. */
    BOOL ok = TRUE;
    const float eq[6] = NOVA_EQ_DB;
    uint8_t eqcmd[8] = { 0x06, 0x33 };
    for (int i = 0; i < 6; i++)
        eqcmd[2 + i] = (uint8_t)(0x14 + (int)(eq[i] * 2));
    uint8_t mic[] = { 0x06, 0x37, NOVA_MIC_VOLUME };
    uint8_t side[] = { 0x06, 0x39, NOVA_SIDETONE };
    uint8_t led[] = { 0x06, 0xAE, NOVA_MIC_LED };
    uint8_t save[] = { 0x06, 0x09 };

    ok &= send_output(&d, eqcmd, sizeof(eqcmd));
    Sleep(20);
    ok &= send_output(&d, mic, sizeof(mic));
    Sleep(20);
    ok &= send_output(&d, side, sizeof(side));
    Sleep(20);
    ok &= send_output(&d, led, sizeof(led));
    Sleep(20);
    ok &= send_output(&d, save, sizeof(save));
    CloseHandle(d.h);
    printf("Arctis Nova 3: %s\n", ok ? "EQ flat, mic 8, sidetone 1, mic LED 2, saved" : "FAILED");
    return ok;
}

/* ---- Razer DeathStalker V2 Pro (openrazer razerkbd_driver.c) ----------- */

#define RZ_LEN 91   /* report ID + 90-byte razer_report */

/* Sends one command; if reply is given, the 80 reply argument bytes land there. */
static BOOL razer_cmd(hiddev *d, uint8_t tid, uint8_t cls, uint8_t id,
                      const uint8_t *args, uint8_t nargs, uint8_t *reply)
{
    uint8_t r[RZ_LEN] = { 0 };
    uint8_t *rep = r + 1;     /* r[0] = report ID 0 */
    rep[1] = tid;             /* transaction id */
    rep[5] = nargs;           /* data size */
    rep[6] = cls;
    rep[7] = id;
    memcpy(rep + 8, args, nargs);
    uint8_t crc = 0;
    for (int i = 2; i < 88; i++)
        crc ^= rep[i];
    rep[88] = crc;

    if (!send_feature(d, r, sizeof(r)))
        return FALSE;
    if (g_dry)
        return TRUE;

    /* Status byte: 0x01 busy, 0x02 ok, 0x03 failure, 0x04 timeout, 0x05 unsupported */
    for (int tries = 0; tries < 10; tries++) {
        Sleep(10);
        uint8_t resp[RZ_LEN] = { 0 };
        if (!HidD_GetFeature(d->h, resp, sizeof(resp))) {
            printf("    HidD_GetFeature failed (error %lu)\n", GetLastError());
            return FALSE;
        }
        uint8_t st = resp[1];
        if (st == 0x02 && resp[7] == cls && resp[8] == id) {
            if (reply)
                memcpy(reply, resp + 9, 80);
            return TRUE;
        }
        if (st != 0x01 && st != 0x00) {
            printf("    device answered status 0x%02x to %02x:%02x\n", st, cls, id);
            return FALSE;
        }
    }
    printf("    no answer to %02x:%02x\n", cls, id);
    return FALSE;
}

static BOOL deathstalker_open(hiddev *d, uint8_t *tid)
{
    /* Interface and usage per OpenRGB's detector; transaction id per openrazer. */
    const char *mode;
    if (hid_find(0x1532, 0x0290, 2, 0x0001, 0x0002, d)) {
        *tid = 0x9F;
        mode = "wireless";
    } else if (hid_find(0x1532, 0x0292, 3, 0x000C, 0x0001, d)) {
        *tid = 0x1F;
        mode = "wired";
    } else {
        printf("DeathStalker V2 Pro: not found (Bluetooth mode cannot be configured)\n");
        return FALSE;
    }
    printf("DeathStalker V2 Pro: found, %s (feature %u bytes)\n", mode, d->caps.FeatureReportByteLength);
    if (d->caps.FeatureReportByteLength != RZ_LEN) {
        printf("    unexpected feature report length, skipping\n");
        CloseHandle(d->h);
        return FALSE;
    }
    return TRUE;
}

static BOOL deathstalker_read(void)
{
    hiddev d;
    uint8_t tid;
    if (!deathstalker_open(&d, &tid))
        return FALSE;
    const uint8_t q[] = { 0x01, 0x05 };
    uint8_t reply[80];
    BOOL ok = razer_cmd(&d, tid, 0x0F, 0x84, q, sizeof(q), reply);
    CloseHandle(d.h);
    if (ok)
        printf("DeathStalker V2 Pro: brightness %u/255 (%u%%)\n", reply[2], (reply[2] * 100 + 127) / 255);
    return ok;
}

static BOOL deathstalker_apply(void)
{
    hiddev d;
    uint8_t tid;
    if (!deathstalker_open(&d, &tid))
        return FALSE;

    /* VARSTORE (0x01) keeps the effect in onboard memory; LED 0x05 = backlight. */
    const uint8_t stat[] = { 0x01, 0x05, 0x01, 0x00, 0x00, 0x01, KBD_R, KBD_G, KBD_B };
    const uint8_t bright[] = { 0x01, 0x05, KBD_BRIGHTNESS };
    BOOL ok = razer_cmd(&d, tid, 0x0F, 0x02, stat, sizeof(stat), NULL);
    ok &= razer_cmd(&d, tid, 0x0F, 0x04, bright, sizeof(bright), NULL);
    CloseHandle(d.h);
    if (ok)
        printf("DeathStalker V2 Pro: static white, brightness %d%%, saved\n", (KBD_BRIGHTNESS * 100 + 127) / 255);
    else
        printf("DeathStalker V2 Pro: FAILED\n");
    return ok;
}

/* ---- main --------------------------------------------------------------- */

static void list(void)
{
    static const struct { uint16_t vid, pid; int mi; USHORT page, usage; const char *name; } t[] = {
        { 0x1038, 0x1724, 0, 0, 0, "Rival 600 control" },
        { 0x1038, 0x12EC, 4, 0xFFC0, 0x0001, "Arctis Nova 3 control" },
        { 0x1532, 0x0290, 2, 0x0001, 0x0002, "DeathStalker V2 Pro wireless control" },
        { 0x1532, 0x0292, 3, 0x000C, 0x0001, "DeathStalker V2 Pro wired control" },
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        hiddev d;
        if (hid_find(t[i].vid, t[i].pid, t[i].mi, t[i].page, t[i].usage, &d)) {
            printf("%-38s page %04x usage %04x in %u out %u feat %u %s\n  %ls\n", t[i].name,
                   d.caps.UsagePage, d.caps.Usage, d.caps.InputReportByteLength, d.caps.OutputReportByteLength,
                   d.caps.FeatureReportByteLength, d.can_write ? "rw" : "no-access", d.path);
            CloseHandle(d.h);
        } else {
            printf("%-38s -\n", t[i].name);
        }
    }
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list")) {
            list();
            return 0;
        } else if (!strcmp(argv[i], "--read")) {
            return deathstalker_read() ? 0 : 1;
        } else if (!strcmp(argv[i], "--dry")) {
            g_dry = 1;
        } else {
            printf("usage: hidset [--dry | --list | --read]\n");
            return 2;
        }
    }
    int failed = 0;
    failed += !rival600_apply();
    failed += !nova3_apply();
    failed += !deathstalker_apply();
    return failed ? 1 : 0;
}
