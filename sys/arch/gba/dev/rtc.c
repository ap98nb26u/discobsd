/*
 * Real-time clock on the EverDrive GBA (ed0).
 *
 * The EverDrive routes a standard GBA cartridge GPIO RTC (a Seiko
 * S-3511-style chip, the same one commercial GBA games with a clock
 * use) onto the cartridge GPIO pins once RTC access is enabled via
 * the EverDrive's own config register (ED_CFG_RTC_ON, see ed.c's
 * ed_rtc_on()). It is therefore read with the ordinary GBA GPIO RTC
 * bit-bang protocol at 0x080000C4-C8, not through any EverDrive-
 * specific register.
 *
 * The bit-bang sequence below is ported from the BSD-licensed
 * GBA_RTCRead by megaboyexe (referenced from krikzz forum topic
 * 10360), kept as close to verbatim as possible so the exact command
 * timing the S-3511 needs is preserved.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>		/* struct timeval time */
#include <sys/config.h>
#include <sys/stdint.h>

#include <gba/dev/ed.h>

/* GBA cartridge GPIO registers used by the S-3511 RTC protocol. */
#define RTC_DATA	(*(volatile uint16_t *)0x080000C4)
#define RTC_RW		(*(volatile uint16_t *)0x080000C6)
#define RTC_ENABLE	(*(volatile uint16_t *)0x080000C8)

/* S-3511 command byte: register number in bits, plus read/write flag. */
#define RTC_CMD_READ(x)	(((x) << 1) | 0x61)

/* Datetime block register indices (register 2 read returns 7 bytes). */
#define _YEAR	0
#define _MONTH	1
#define _DAY	2
#define _WKD	3
#define _HOUR	4
#define _MIN	5
#define _SEC	6

#define UNBCD(x)	(((x) & 0xF) + (((x) >> 4) * 10))

/*
 * Registered with machdep.c's inittodr() when the RTC is found, so
 * the machine-independent boot path can read the clock without any
 * arch-specific reference when the RTC is absent (e.g. the mGBA build,
 * which does not compile this file at all).
 */
extern int (*md_rtc_gettime)(time_t *);

/* Shift one command byte out to the RTC (S-3511 bit-bang). */
static void
rtc_cmd(int v)
{
    int l;
    uint16_t b;

    v = v << 1;
    for (l = 7; l >= 0; l--) {
        b = (v >> l) & 0x2;
        RTC_DATA = b | 4;
        RTC_DATA = b | 4;
        RTC_DATA = b | 4;
        RTC_DATA = b | 5;
    }
}

/* Read one byte back from the RTC (S-3511 bit-bang). */
static int
rtc_read_byte(void)
{
    int j, l;
    uint16_t b;
    int v = 0;

    for (l = 0; l < 8; l++) {
        for (j = 0; j < 5; j++)
            RTC_DATA = 4;
        RTC_DATA = 5;
        b = RTC_DATA;
        v = v | ((b & 2) << l);
    }
    return v >> 1;
}

/*
 * Read the 7-byte datetime block into data[]:
 *   [0]=year [1]=month [2]=day [3]=weekday [4]=hour [5]=min [6]=sec,
 * each BCD-encoded. Wrapped in ed_rtc_on()/ed_rtc_off() so the
 * EverDrive routes the GPIO to its RTC for the duration.
 */
static void
rtc_get(uint8_t *data)
{
    int i;

    ed_rtc_on();

    RTC_ENABLE = 1;		/* enable GPIO read/write */
    RTC_DATA = 1;
    RTC_RW = 7;
    RTC_DATA = 1;
    RTC_DATA = 5;
    rtc_cmd(RTC_CMD_READ(2));
    RTC_RW = 5;
    for (i = 0; i < 4; i++)
        data[i] = (uint8_t)rtc_read_byte();
    RTC_RW = 5;
    for (i = 4; i < 7; i++)
        data[i] = (uint8_t)rtc_read_byte();

    ed_rtc_off();
}

static int
is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

/*
 * Convert a decoded calendar datetime to seconds since the Unix epoch
 * (1970-01-01 00:00:00 UTC). The RTC keeps local wall-clock time; any
 * timezone handling is left to userland.
 */
static time_t
ymdhms_to_secs(int year, int mon, int day, int hour, int min, int sec)
{
    static const int mdays[12] =
        { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    long days = 0;
    int i;

    for (i = 1970; i < year; i++)
        days += is_leap(i) ? 366 : 365;
    for (i = 1; i < mon; i++) {
        days += mdays[i - 1];
        if (i == 2 && is_leap(year))
            days++;
    }
    days += day - 1;

    return (time_t)days * 86400 + (time_t)hour * 3600 +
        (time_t)min * 60 + sec;
}

/*
 * Read the RTC and return the current time in *tp. Returns 0 on a
 * plausible reading, nonzero if the values are out of range (e.g. an
 * unset or absent clock), so the caller can fall back to another time
 * source rather than trust garbage.
 */
/* Last successfully decoded reading, for the probe's announcement. */
static int rtc_last_year, rtc_last_mon, rtc_last_day;
static int rtc_last_hour, rtc_last_min, rtc_last_sec;

int
rtc_gettime(time_t *tp)
{
    uint8_t d[7];
    int year, mon, day, hour, min, sec;

    rtc_get(d);

    year = 2000 + UNBCD(d[_YEAR]);
    mon  = UNBCD(d[_MONTH]);
    day  = UNBCD(d[_DAY]);
    hour = UNBCD(d[_HOUR] & 0x3F);	/* mask 12/24h + PM flag bits */
    min  = UNBCD(d[_MIN]);
    sec  = UNBCD(d[_SEC] & 0x7F);

    if (mon < 1 || mon > 12 || day < 1 || day > 31 ||
        hour > 23 || min > 59 || sec > 59)
        return 1;

    rtc_last_year = year;
    rtc_last_mon = mon;
    rtc_last_day = day;
    rtc_last_hour = hour;
    rtc_last_min = min;
    rtc_last_sec = sec;

    *tp = ymdhms_to_secs(year, mon, day, hour, min, sec);
    return 0;
}

/*
 * Probe: announce the RTC as a child of its controller (ed0) and hook
 * it into the boot-time clock read.
 */
static int
rtc_probe(struct conf_device *config)
{
    const char *ctlr_name = config->dev_cdriver->d_name;
    int ctlr_num = config->dev_ctlr;
    time_t t;

    if (rtc_gettime(&t) != 0) {
        printf("rtc%d: on %s%d, but clock is unset\n",
            config->dev_unit, ctlr_name, ctlr_num);
    } else {
        printf("rtc%d on %s%d: %04d-%02d-%02d %02d:%02d:%02d\n",
            config->dev_unit, ctlr_name, ctlr_num,
            rtc_last_year, rtc_last_mon, rtc_last_day,
            rtc_last_hour, rtc_last_min, rtc_last_sec);
    }

    md_rtc_gettime = rtc_gettime;
    return 1;
}

struct driver rtcdriver = {
    "rtc", rtc_probe,
};
