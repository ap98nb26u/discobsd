/*
 * SD card connected through the EverDrive GBA X5 expansion unit (ed0).
 *
 * The EverDrive has no generic SPI/SDIO peripheral: its FPGA exposes a
 * dedicated SD command/data port (ed_sd_* in ed.c) that speaks the SD
 * protocol directly. The command framing and DMA transfer sequence
 * below are ported from EverDrive's own disk.c sample source; the CSD
 * (card size) decoding is ported from the pic32 port's sd.c, since
 * the CSD register format is standard SD spec, independent of
 * transport.
 *
 * Only one physical SD slot exists on the EverDrive, so all card_*()
 * functions below operate on that one slot regardless of "unit" --
 * NSD is expected to stay 1.
 *
 * PC-compatible partition table is supported.
 * The following device numbers are used:
 *
 * Major Minor Device  Partition
 * ----------------------------------------------
 *   0     0     sd0   Main SD card, whole volume
 *   0     1     sd0a  1-st partition
 *   0     2     sd0b  2-nd partition
 *   0     3     sd0c  3-rd partition
 *   0     4     sd0d  4-th partition
 *
 * Copyright (C) 2010-2015 Serge Vakulenko, <serge@vak.ru>
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/dk.h>
#include <sys/disk.h>
#include <sys/config.h>
#include <sys/stdint.h>

#include <gba/dev/sd.h>
#include <gba/dev/ed.h>

#define sdunit(dev)     ((minor(dev) & 8) >> 3)
#define sdpart(dev)     ((minor(dev) & 7))
#define RAWPART         0               /* 'x' partition */

/*
 * SD command framing, ported from EverDrive's disk.c.
 */
#define SD_CMD0         0x40    /* software reset */
#define SD_CMD1         0x41
#define SD_CMD2         0x42    /* read CID */
#define SD_CMD3         0x43    /* read RCA */
#define SD_CMD6         0x46
#define SD_CMD7         0x47
#define SD_CMD8         0x48
#define SD_CMD9         0x49    /* read CSD */
#define SD_CMD12        0x4C    /* stop transmission */
#define SD_CMD17        0x51    /* read single block */
#define SD_CMD18        0x52    /* read multiple block */
#define SD_CMD24        0x58    /* write single block */
#define SD_CMD25        0x59    /* write multiple block */
#define SD_CMD41        0x69    /* ACMD41 */
#define SD_CMD55        0x77
#define SD_CMD58        0x7A    /* read OCR */

#define SD_R1           1
#define SD_R2           2
#define SD_R3           3
#define SD_R6           6
#define SD_R7           7

#define SD_TYPE_HC      1       /* bit: high-capacity (block addressed) */
#define SD_TYPE_V2      2       /* bit: SD version 2 */

#define SD_ERR_CMD_TIMEOUT  1
#define SD_ERR_CRC_ERROR    2

/*
 * This bounds retry loops that call ed_sd_cmd_rd()/ed_sd_cmd_wr()
 * (ed.c) each iteration - those already retry internally with their
 * own bound, so any large count here multiplies the two bounds
 * together. This was 2048 and, combined with sd_cmd()'s own
 * SD_CMD_WAIT-bounded response-wait loop below (also multiplying
 * against ed_sd_cmd_rd()'s internal bound), was part of what turned
 * a genuinely-unresponsive-card case into a many-hour real-hardware
 * hang before any of these bounds were sized with this multiplication
 * in mind. Cut sharply.
 */
#define SD_CMD_WAIT     50

static uint8_t sd_resp_buff[18];
static uint8_t sd_card_flags;          /* SD_TYPE_HC | SD_TYPE_V2 */
static uint32_t sd_disk_addr;          /* currently open 512-byte sector, or ~0 */

/*
 * Set the moment a CMD18/CMD25 is *issued*, cleared only once a CMD12
 * has actually been sent to close it (2026-09-06).
 *
 * sd_disk_addr alone could not distinguish "no transfer is open, so
 * there is nothing to close" from "a transfer was started but we don't
 * know where it stands", and both looked like ~0U. When sd_open_read()
 * failed *after* putting CMD18 on the wire - a bad or timed-out R1
 * response doesn't mean the card ignored the command - the card was
 * left streaming while sd_disk_addr said ~0U. sd_close_rw() then saw
 * the sentinel, skipped CMD12 entirely, and the next sd_open_read()
 * issued a fresh CMD18 into a card that was still feeding the previous
 * stream, so reads came back from a completely different disk position:
 * observed on real GBAED hardware as an exec loading some *other*
 * program's image (a shell command that unexpectedly started tclsh).
 */
static uint8_t sd_stream_dirty;

/*
 * Set by boot() (machdep.c) around its final sync() before a reboot/
 * halt, so sd_close_rw()'s card-ready wait below can afford to wait
 * much longer than it safely can during ordinary runtime. That wait's
 * retry bound (currently 800 iterations, ~2.4s worst case) is a
 * deliberately tight compromise - see the long comment in
 * sd_close_rw() - between two failure modes that were both observed
 * on real hardware: too short and a genuinely still-busy card (SD
 * program time after a write can run into the hundreds of ms,
 * observed worse after sustained heavy rewrite of the same blocks,
 * e.g. a fork/exec-heavy workload right before shutdown) gets treated
 * as "ready" too early, returning stale data on the very next read -
 * too long and a truly unresponsive/disconnected card hangs the
 * kernel for many seconds to minutes. At ordinary runtime a hang is
 * the worse outcome (the system becomes completely unusable); during
 * the final pre-reboot flush a longer wait is nearly free (nothing
 * else is happening) while returning too early risks exactly the
 * on-disk corruption this flag exists to avoid.
 */
int sd_shutdown_flush;

static const uint16_t sd_crc16_table[] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
};

static uint32_t
sd_crc7(const uint8_t *buf, uint32_t len)
{
    unsigned crc = 0, a;

    while (len--) {
        crc ^= *buf++;
        a = 8;
        do {
            crc <<= 1;
            if (crc & (1 << 8))
                crc ^= 0x12;
        } while (--a);
    }
    return crc & 0xfe;
}

static uint8_t
sd_resp_type(uint8_t cmd)
{
    switch (cmd) {
    case SD_CMD3:
        return SD_R6;
    case SD_CMD8:
        return SD_R7;
    case SD_CMD2:
    case SD_CMD9:
        return SD_R2;
    case SD_CMD58:
    case SD_CMD41:
        return SD_R3;
    default:
        return SD_R1;
    }
}

/*
 * Send an SD command and collect its response into sd_resp_buff[].
 * Returns 0 on success, SD_ERR_* on failure.
 */
static uint8_t
sd_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t resp_type = sd_resp_type(cmd);
    uint8_t buf[6];
    uint8_t crc;
    uint16_t i;
    uint8_t resp_len = (resp_type == SD_R2) ? 17 : 6;

    buf[0] = cmd;
    buf[1] = arg >> 24;
    buf[2] = arg >> 16;
    buf[3] = arg >> 8;
    buf[4] = arg >> 0;
    crc = sd_crc7(buf, 5) | 1;

    ed_sd_mode(ED_SD_MODE8);

    ed_sd_cmd_wr(0xff);
    ed_sd_cmd_wr(cmd);
    ed_sd_cmd_wr(arg >> 24);
    ed_sd_cmd_wr(arg >> 16);
    ed_sd_cmd_wr(arg >> 8);
    ed_sd_cmd_wr(arg);
    ed_sd_cmd_wr(crc);

    if (cmd == SD_CMD18)
        return 0;

    ed_sd_cmd_rd();
    ed_sd_mode(ED_SD_MODE1);
    for (i = 0; ; i++) {
        if ((ed_sd_cmd_val() & 192) == 0)
            break;
        if (i == SD_CMD_WAIT)
            return SD_ERR_CMD_TIMEOUT;
        ed_sd_cmd_rd();
    }

    ed_sd_mode(ED_SD_MODE8);

    sd_resp_buff[0] = ed_sd_cmd_rd();
    for (i = 1; i < (uint16_t)(resp_len - 1); i++)
        sd_resp_buff[i] = ed_sd_cmd_rd();
    sd_resp_buff[i] = ed_sd_cmd_val();

    if (resp_type != SD_R3) {
        if (resp_type == SD_R2)
            crc = sd_crc7(sd_resp_buff + 1, resp_len - 2) | 1;
        else
            crc = sd_crc7(sd_resp_buff, resp_len - 1) | 1;
        if (crc != sd_resp_buff[resp_len - 1])
            return SD_ERR_CRC_ERROR;
    }
    return 0;
}

/*
 * Compute the 4-lane CRC16 needed by a multi-block write, from the
 * FPGA's own raw CRC working RAM. crc_out[0..3] need not be
 * initialized: 16 shift-in iterations per lane fully replace them.
 */
static void
sd_crc16(uint16_t *crc_out)
{
    uint16_t i, u, crc_table[4], tmp;
    uint8_t buf[512];
    uint8_t *p0, *p1, *p2, *p3;

    ed_sd_read_crc_ram(buf);

    for (i = 0; i < 4; i++)
        crc_table[i] = 0;

    p0 = &buf[0];
    p1 = &buf[128];
    p2 = &buf[256];
    p3 = &buf[384];

    for (i = 0; i < 128; i++) {
        tmp = crc_table[0];
        crc_table[0] = sd_crc16_table[(tmp >> 8) ^ *p0++];
        crc_table[0] ^= (tmp << 8);

        tmp = crc_table[1];
        crc_table[1] = sd_crc16_table[(tmp >> 8) ^ *p1++];
        crc_table[1] ^= (tmp << 8);

        tmp = crc_table[2];
        crc_table[2] = sd_crc16_table[(tmp >> 8) ^ *p2++];
        crc_table[2] ^= (tmp << 8);

        tmp = crc_table[3];
        crc_table[3] = sd_crc16_table[(tmp >> 8) ^ *p3++];
        crc_table[3] ^= (tmp << 8);
    }

    for (i = 0; i < 4; i++) {
        for (u = 0; u < 16; u++) {
            crc_out[3 - i] >>= 1;
            crc_out[3 - i] |= (crc_table[u % 4] & 1) << 15;
            crc_table[u % 4] >>= 1;
        }
    }
}

static uint8_t
sd_close_rw(void)
{
    uint8_t resp;
    uint16_t i;

    if (sd_disk_addr == ~0U && ! sd_stream_dirty)
        return 0;
    sd_disk_addr = ~0U;
    sd_stream_dirty = 0;

    resp = sd_cmd(SD_CMD12, 0);
    if (resp)
        return resp;

    ed_sd_mode(ED_SD_MODE1);
    ed_sd_dat_rd();
    ed_sd_dat_rd();
    ed_sd_dat_rd();
    ed_sd_mode(ED_SD_MODE2);

    /*
     * ed_sd_dat_rd() (ed.c) already retries internally with its own
     * bound (added this session) before giving up - looping this
     * many more times around it multiplies the two bounds together,
     * the same "outer x inner" mistake ed_sd_wait_f0() had (see its
     * comment in ed.c) and just as capable of turning a bounded wait
     * into a many-hour real-hardware hang when the card genuinely
     * isn't responding. Cut sharply for the same reason.
     *
     * DBG: bumped from 50 - vm_swap.c's new swapin checksum-verify
     * caught real GBAED hardware handing back the *previous*
     * generation's data for a just-rewritten block (checksum matched
     * an earlier write to the same blkno exactly, not garbage),
     * meaning this "wait for card ready" loop was returning before
     * the card had actually finished committing the write to flash -
     * each ed_sd_dat_rd() covers only what the FPGA's own SPI-shift
     * busy flag bounds (microseconds), not the card's real internal
     * program-time busy state (DAT0 held low, can be many ms,
     * especially after heavy sustained rewrite of the same blocks as
     * this session's fork/exec-heavy respawn loop does). 50 iterations
     * of that was nowhere near enough headroom.
     */
    /*
     * DBG: dialed back from 5000 - that fixed the checksum-mismatch
     * corruption (confirmed: zero mismatches across two full test
     * rounds after the bump), but real GBAED hardware then hung for
     * several minutes partway through a later boot, before any
     * newproc/swap activity had even logged - consistent with this
     * same loop now burning its full worst case (5000 iterations *
     * ed_sd_dat_rd()'s own up-to-50000-cycle inner wait, ~15s) instead
     * of failing fast the way the original i=50 did (~150ms worst
     * case). 800 keeps most of the added headroom (16x the original)
     * while capping a genuine no-response case at ~2.4s instead of
     * ~15s. The printf below exists so a real timeout here is no
     * longer silent - if this fires, that's confirmation this exact
     * wait is the hang, not a guess.
     */
    /*
     * During the final pre-reboot/halt flush (sd_shutdown_flush set by
     * boot(), machdep.c) wait far longer than is safe during ordinary
     * runtime - see the comment on sd_shutdown_flush's declaration
     * above for why this asymmetry is the right tradeoff.
     */
    i = sd_shutdown_flush ? 8000 : 800;
    while (--i) {
        if (ed_sd_dat_rd() == 0xff)
            break;
    }
    if (i == 0) {
        printf("sd: close: card-ready wait timed out\n");
        return SD_ERR_CMD_TIMEOUT;
    }
    return 0;
}

static uint8_t
sd_open_read(uint32_t saddr)
{
    uint8_t resp;

    if ((sd_card_flags & SD_TYPE_HC) == 0)
        saddr *= 512;

    /*
     * Mark before issuing, not after: if the response is bad or times
     * out the card may still have accepted CMD18 and started
     * streaming, and that is exactly the case sd_close_rw() must not
     * skip its CMD12 for. See sd_stream_dirty's declaration.
     */
    sd_stream_dirty = 1;
    resp = sd_cmd(SD_CMD18, saddr);
    if (resp)
        return resp;
    return 0;
}

/*
 * Read 'slen' 512-byte sectors starting at 512-byte sector 'sd_addr'
 * into 'dst'.
 */
static uint8_t
sd_read_sectors(uint32_t sd_addr, void *dst, uint16_t slen)
{
    uint8_t resp;
    uint8_t reopened = 0;

    if (sd_addr != sd_disk_addr) {
        resp = sd_close_rw();
        if (resp) {
            printf("sd: rd sec=%u n=%d: close failed %d\n",
                sd_addr, slen, resp);
            return resp;
        }
        resp = sd_open_read(sd_addr);
        if (resp) {
            printf("sd: rd sec=%u n=%d: CMD18 failed %d\n",
                sd_addr, slen, resp);
            return resp;
        }
        sd_disk_addr = sd_addr;
        reopened = 1;
    }

    resp = ed_sd_dma_rd(dst, slen);
    if (resp) {
        /*
         * Tear the streaming read down before reporting failure
         * (2026-09-06). The fast path above deliberately keeps a
         * multi-block read (SD_CMD18) open across calls and skips the
         * close/reopen whenever the caller asks for exactly the next
         * sequential sector - but a failed DMA leaves that stream
         * desynchronized while sd_disk_addr still names the sector we
         * were trying to read. A caller that retries the same read
         * therefore matched the fast path, skipped the close/reopen,
         * and re-entered the *same broken stream* - so the retry could
         * never recover, failing identically every time. Observed
         * exactly that on real GBAED hardware once exec_aout.c started
         * retrying failed image reads: four attempts, four identical
         * EIOs, then "exec: image read failed (5)". sd_close_rw()
         * issues CMD12 and restores the ~0U "nothing open" sentinel,
         * so the next attempt does a full, clean reopen.
         */
        printf("sd: rd sec=%u n=%d: dma failed %d (%s stream)\n",
            sd_addr, slen, resp, reopened ? "fresh" : "continued");
        (void)sd_close_rw();
        return SD_ERR_CMD_TIMEOUT;
    }

    sd_disk_addr += slen;
    return 0;
}

/*
 * Write 'slen' 512-byte sectors starting at 512-byte sector 'sd_addr'
 * from 'src'.
 */
static uint8_t
sd_write_sectors(uint32_t sd_addr, const void *src, uint16_t slen)
{
    uint8_t resp;
    uint16_t crc16[4];
    uint16_t i, u;
    uint32_t saddr = sd_addr;
    const char *p = src;

    resp = sd_close_rw();
    if (resp)
        return resp;

    sd_disk_addr = sd_addr;
    if ((sd_card_flags & SD_TYPE_HC) == 0)
        saddr *= 512;

    /* Mark before issuing - same reasoning as sd_open_read()'s CMD18. */
    sd_stream_dirty = 1;
    resp = sd_cmd(SD_CMD25, saddr);
    if (resp)
        return resp;

    while (slen--) {
        ed_sd_mode(ED_SD_MODE2);
        ed_sd_dat_wr(0xff);
        ed_sd_dat_wr(0xf0);

        ed_sd_dma_wr(p);
        sd_crc16(crc16);
        p += 512;

        ed_sd_mode(ED_SD_MODE2);
        for (i = 0; i < 4; i++) {
            ed_sd_dat_wr(crc16[i] >> 8);
            ed_sd_dat_wr(crc16[i] & 0xff);
        }

        ed_sd_mode(ED_SD_MODE1);
        ed_sd_dat_wr(0xff);
        ed_sd_dat_rd();

        /* Same outer-x-inner multiplication concern as below - cut. */
        i = 50;
        while ((ed_sd_dat_rd() & 1) != 0 && --i != 0)
            ;
        if (i == 0) {
            /* Same reasoning as sd_read_sectors()'s failure path -
             * don't leave a half-finished multi-block stream open
             * with sd_disk_addr still naming a sector inside it. */
            (void)sd_close_rw();
            return SD_ERR_CMD_TIMEOUT;
        }

        resp = 0;
        for (i = 0; i < 3; i++) {
            resp <<= 1;
            u = ed_sd_dat_rd();
            resp |= u & 1;
        }
        resp &= 7;
        if (resp != 0x02) {
            (void)sd_close_rw();
            return (resp == 5) ? SD_ERR_CRC_ERROR : SD_ERR_CMD_TIMEOUT;
        }

        ed_sd_mode(ED_SD_MODE1);
        ed_sd_dat_rd();

        /*
         * ed_sd_dat_rd() already retries internally with its own
         * bound - looping this many more times around it multiplies
         * the two bounds together (the same "outer x inner" mistake
         * ed_sd_wait_f0() had, see its comment in ed.c), capable of
         * turning a bounded wait into a many-hour real-hardware hang
         * when the card genuinely isn't responding. Cut sharply.
         *
         * DBG: same fix and same reason as sd_close_rw()'s identical
         * wait loop below in this file - undersized (50) against
         * genuine SD program-time busy, not just the FPGA's own
         * SPI-shift busy flag. First tried 5000, which fixed the
         * checksum-mismatch corruption but then caused a several-
         * minute real-hardware hang on a later boot (this loop runs
         * per block in a multi-block write, so its worst case
         * multiplies by however many blocks are in flight); dialed
         * back to 800 for the same reasoning as sd_close_rw().
         *
         * DBG: this is a SEPARATE wait from sd_close_rw()'s own -
         * it gates every individual block of a multi-block write
         * (SD_CMD25), while sd_close_rw()'s only gates the transfer's
         * final CMD12 close. Missed applying sd_shutdown_flush here
         * in the first pass at this fix (see sd_shutdown_flush's
         * declaration comment above) - a multi-block write closing
         * out fine at sd_close_rw() doesn't mean every block *within*
         * it actually had enough real time to finish programming
         * before the next block's data started clocking in over the
         * same DAT line, and this per-block wait is exactly what
         * would need to be longer for that to matter during the
         * final pre-reboot flush too.
         */
        i = sd_shutdown_flush ? 8000 : 800;
        while (--i) {
            if (ed_sd_dat_rd() == 0xff)
                break;
        }
        if (i == 0) {
            printf("sd: write: per-block ready wait timed out\n");
            /* Close the half-written multi-block stream, as the two
             * error returns above now do. */
            (void)sd_close_rw();
            return SD_ERR_CMD_TIMEOUT;
        }
    }

    return sd_close_rw();
}

/*
 * Bring up the SD card and detect its type. Returns nonzero on success.
 */
static int
card_init(int unit)
{
    uint16_t i;
    uint8_t resp;
    uint32_t rca;
    const uint16_t wait_len = SD_CMD_WAIT;

    if (unit != 0)
        return 0;

    /*
     * Power/signal settle delay before talking to the card. Standard
     * SD initialization practice calls for >=1ms of settle time after
     * power-up before issuing the first command, on top of the >=74
     * clock cycles already sent below - this port never had either
     * (mdelay(), machdep.c, is an empty GBA stub, never implemented).
     * Booting through the EverDrive's stock ROM-selection menu first
     * happened to provide enough incidental delay to mask this; a
     * cold power-on straight into this ROM (as \GBASYS\GBAOS.gba)
     * does not, and card_init() below can then fail or hang mid
     * command depending on how unsettled the card/FPGA still is.
     * mdelay() itself is unusable (no real timebase behind it on this
     * port), so busy-wait on raw CPU cycles instead - not precise,
     * just needs to be "clearly more than a few ms" at ~16.78MHz.
     */
    {
        /*
         * This runs from config() (init_main.c's main(), before
         * proc[0]/u are set up), but cpu_initclocks() (also called
         * from config(), just before device probing) has already
         * armed TIMER0 - so a busy-wait this long left interrupts
         * enabled through several timer ticks, and this session's
         * first attempt at this delay (interrupts left alone)
         * regressed a previously-working A-button/menu boot into
         * crash-rebooting at the same spot every time. Almost
         * certainly hardclock()/schedcpu() firing against process
         * state that doesn't exist yet. Block interrupts for the
         * duration and explicitly re-enable after - nothing between
         * here and the real interrupt-enable point later in boot
         * needs them.
         */
        extern void gba_irq_allow(void);
        extern void gba_irq_block(void);
        volatile uint32_t settle;

        /*
         * DBG: bumped from 100000 - still seeing sd0 fail to be
         * recognized on some cold boots (2 failed probe-and-reboot
         * cycles observed before a 3rd attempt succeeded), suggesting
         * the original margin isn't always enough. Cheap to make
         * larger; only paid once at boot.
         */
        gba_irq_block();
        for (settle = 0; settle < 400000; settle++)
            ;
        gba_irq_allow();
    }

    sd_card_flags = 0;

    ed_sd_speed(ED_SD_SPD_LO);
    ed_sd_mode(ED_SD_MODE8);

    for (i = 0; i < 40; i++)
        ed_sd_dat_wr(0xff);
    sd_cmd(SD_CMD0, 0x1aa);

    for (i = 0; i < 40; i++)
        ed_sd_dat_wr(0xff);

    resp = sd_cmd(SD_CMD8, 0x1aa);
    if (resp != 0 && resp != SD_ERR_CMD_TIMEOUT)
        return 0;
    if (resp == 0)
        sd_card_flags |= SD_TYPE_V2;

    if (sd_card_flags == SD_TYPE_V2) {
        for (i = 0; i < wait_len; i++) {
            resp = sd_cmd(SD_CMD55, 0);
            if (resp)
                return 0;
            if ((sd_resp_buff[3] & 1) != 1)
                continue;
            sd_cmd(SD_CMD41, 0x40300000);
            if ((sd_resp_buff[1] & 128) == 0)
                continue;
            break;
        }
    } else {
        i = 0;
        do {
            resp = sd_cmd(SD_CMD55, 0);
            if (resp)
                return 0;
            resp = sd_cmd(SD_CMD41, 0x40300000);
            if (resp)
                return 0;
        } while (sd_resp_buff[1] < 1 && i++ < wait_len);
    }
    if (i == wait_len)
        return 0;

    if ((sd_resp_buff[1] & 64) && sd_card_flags != 0)
        sd_card_flags |= SD_TYPE_HC;

    resp = sd_cmd(SD_CMD2, 0);
    if (resp)
        return 0;

    resp = sd_cmd(SD_CMD3, 0);
    if (resp)
        return 0;
    rca = ((uint32_t)sd_resp_buff[1] << 24) | ((uint32_t)sd_resp_buff[2] << 16) |
          ((uint32_t)sd_resp_buff[3] << 8) | sd_resp_buff[4];

    resp = sd_cmd(SD_CMD9, rca);        /* get CSD */
    if (resp)
        return 0;
    bcopy(sd_resp_buff + 1, sddrives[unit].csd, 16);

    resp = sd_cmd(SD_CMD7, rca);
    if (resp)
        return 0;

    resp = sd_cmd(SD_CMD55, rca);
    if (resp)
        return 0;

    resp = sd_cmd(SD_CMD6, 2);
    if (resp)
        return 0;

    /*
     * DBG: tried forcing SPD_LO here to test a clock-speed/signal-
     * integrity theory for intermittent read/write corruption (see
     * the swap-checksum-mismatch + fsck "BLK(S) MISSING" evidence) -
     * made things strictly worse on real GBAED hardware: the partition
     * table read in sd_setup() (right after card_init() returns)
     * started failing every time ("no fs on dev", reproducing even
     * after rewriting the SD image fresh), where it previously worked
     * at high speed. Reverted back to HIGH; whatever the low-speed
     * mode actually does on this FPGA, it isn't simply "the same
     * protocol, slower" - something else in the driver's timing
     * assumptions is calibrated for HIGH specifically. The settle-
     * delay increase above (card_init()'s pre-command wait) is
     * unrelated and stays.
     */
    ed_sd_speed(ED_SD_SPD_HI);
    /* Card was just fully re-initialized: no transfer can still be
     * open, so drop the "might be streaming" mark too. */
    sd_disk_addr = ~0U;
    sd_stream_dirty = 0;

    if (sd_card_flags & SD_TYPE_HC)
        sddrives[unit].card_type = TYPE_SDHC;
    else if (sd_card_flags & SD_TYPE_V2)
        sddrives[unit].card_type = TYPE_SD_II;
    else
        sddrives[unit].card_type = TYPE_SD_LEGACY;

    return 1;
}

/*
 * Get disk size in 512-byte sectors, decoded from the CSD register
 * already read by card_init(). Return nonzero if successful.
 */
static int
card_size(int unit)
{
    struct disk *du = &sddrives[unit];
    unsigned csize, n;
    int nsectors;

    if (unit != 0)
        return 0;

    switch (du->csd[0] >> 6) {
    case 1:                     /* CSD ver 2.00 (SDHC/SDXC) */
        csize = du->csd[9] + (du->csd[8] << 8) + 1;
        nsectors = csize << 10;
        break;
    case 0:                     /* CSD ver 1.XX */
        n = (du->csd[5] & 15) + ((du->csd[10] & 128) >> 7) +
            ((du->csd[9] & 3) << 1) + 2;
        csize = (du->csd[8] >> 6) + (du->csd[7] << 2) +
            ((du->csd[6] & 3) << 10) + 1;
        nsectors = csize << (n - 9);
        break;
    default:
        return 0;
    }
    return nsectors;
}

static int
card_read(int unit, unsigned int offset, char *data, unsigned int bcount)
{
    uint32_t sd_addr;
    uint16_t slen;

    if (unit != 0)
        return 0;

    sd_addr = (uint32_t)offset << 1;
    slen = (bcount + 511) / 512;
    if (sd_read_sectors(sd_addr, data, slen) == 0)
        return 1;

    /*
     * The read failed and sd_read_sectors() already tore its stream
     * down (CMD12) - but on real GBAED hardware a close+reopen
     * (CMD12+CMD18) has been observed NOT to clear the failure: four
     * successive exec-image-read retries all failed identically at the
     * same "fresh stream" CMD18 with the same code, i.e. the FPGA SD
     * controller / card is in a state a fresh CMD18 alone can't
     * recover from (see project_gba_sh_fault_sigreturn_hang.md and
     * project_gba_sd_driver_plan.md). Fully re-initialize the card
     * here as a heavier recovery, then try the read once more before
     * reporting failure up to whatever retry loop the caller has
     * (exec_aout.c / vm_swp.c). This is expensive (card_init() busy-
     * waits a settle delay with interrupts blocked) but only runs on
     * an actual failure, which should be rare.
     */
    printf("sd: read failed, reinitializing card and retrying\n");
    (void)card_init(unit);
    if (sd_read_sectors(sd_addr, data, slen) == 0)
        return 1;

    return 0;
}

static int
card_write(int unit, unsigned offset, char *data, unsigned bcount)
{
    uint32_t sd_addr;
    uint16_t slen;

    if (unit != 0)
        return 0;

    sd_addr = (uint32_t)offset << 1;
    slen = (bcount + 511) / 512;
    if (sd_write_sectors(sd_addr, data, slen))
        return 0;
    return 1;
}

static void
card_release(int unit)
{
    if (unit != 0)
        return;
    sd_close_rw();
    sd_disk_addr = ~0U;
}

/*
 * Detect a card.
 */
static int
sd_setup(int unit)
{
    struct disk *du = &sddrives[unit];
    u_short buf[256];

    if (! card_init(unit)) {
        printf("sd%d: no SD/MMC card detected\n", unit);
        return 0;
    }
    /* Get the size of raw partition. */
    bzero(du->part, sizeof(du->part));
    du->part[RAWPART].dp_offset = 0;
    du->part[RAWPART].dp_nsectors = card_size(unit);
    if (du->part[RAWPART].dp_nsectors == 0) {
        printf("sd%d: cannot get card size\n", unit);
        return 0;
    }

    printf("sd%d: type %s, size %u kbytes\n", unit,
        (du->card_type == TYPE_SDHC) ? "SDHC" :
        (du->card_type == TYPE_SD_II) ? "II" : "I",
        du->part[RAWPART].dp_nsectors / 2);

    /* Read partition table. */
    int s = splbio();
    if (! card_read(unit, 0, (char*)buf, sizeof(buf))) {
        splx(s);
        printf("sd%d: cannot read partition table\n", unit);
        return 0;
    }
    splx(s);
    if (buf[255] == MBR_MAGIC) {
        bcopy(&buf[223], &du->part[1], 64);
        int i;
        for (i=1; i<=NPARTITIONS; i++) {
            if (du->part[i].dp_type != 0)
                printf("sd%d%c: partition type %02x, sector %u, size %u kbytes\n",
                    unit, i+'a'-1, du->part[i].dp_type,
                    du->part[i].dp_offset,
                    du->part[i].dp_nsectors / 2);
        }
    }
    return 1;
}

/*
 * Disable the SD card.
 */
static void
sd_release(int unit)
{
    struct disk *du = &sddrives[unit];

    card_release(unit);

    /* Forget the partition table. */
    du->part[RAWPART].dp_nsectors = 0;
}

/*
 * sd block device
 */

int
sdopen(dev_t dev, int flags, int mode)
{
    int unit = sdunit(dev);
    int part = sdpart(dev);
    struct disk *du = &sddrives[unit];
    unsigned mask, i;

    if (unit >= NSD || part > NPARTITIONS)
        return ENXIO;

    /*
     * Setup the SD card interface.
     */
    if (du->part[RAWPART].dp_nsectors == 0) {
        if (! sd_setup(unit)) {
            return ENODEV;
        }
    }
    mask = 1 << part;

    /*
     * Warn if a partion is opened
     * that overlaps another partition which is open
     * unless one is the "raw" partition (whole disk).
     */
    if (part != RAWPART && (du->openpart & mask) == 0) {
        unsigned start = du->part[part].dp_offset;
        unsigned end = start + du->part[part].dp_nsectors;

        /* Check for overlapped partitions. */
        for (i=0; i<=NPARTITIONS; i++) {
            struct diskpart *pp = &du->part[i];

            if (i == part || i == RAWPART)
                continue;

            if (pp->dp_offset + pp->dp_nsectors <= start ||
                pp->dp_offset >= end)
                continue;

            if (du->openpart & (1 << i))
                printf("sd%d%c: overlaps open partition (sd%d%c)\n",
                    unit, part + 'a' - 1,
                    unit, pp - du->part + 'a' - 1);
        }
    }
    du->openpart |= mask;
    return 0;
}

int
sdclose(dev_t dev, int mode, int flag)
{
    int unit = sdunit(dev);
    int part = sdpart(dev);
    struct disk *du = &sddrives[unit];

    if (unit >= NSD || part > NPARTITIONS)
        return ENODEV;

    du->openpart &= ~(1 << part);
    if (du->openpart == 0) {
        /* All partitions closed.
         * Release the SD card. */
        sd_release(unit);
    }
    return 0;
}

/*
 * Get disk size in kbytes.
 * Return nonzero if successful.
 */
daddr_t
sdsize(dev_t dev)
{
    int unit = sdunit(dev);
    int part = sdpart(dev);
    struct disk *du = &sddrives[unit];

    if (unit >= NSD || part > NPARTITIONS || du->openpart == 0)
        return 0;

    return du->part[part].dp_nsectors >> 1;
}

void
sdstrategy(struct buf *bp)
{
    int unit = sdunit(bp->b_dev);
    struct disk *du = &sddrives[unit];
    struct diskpart *p = &du->part[sdpart(bp->b_dev)];
    int part_size = p->dp_nsectors >> 1;
    int offset = bp->b_blkno;
    long nblk = btod(bp->b_bcount);
    int s;

    /*
     * Determine the size of the transfer, and make sure it is
     * within the boundaries of the partition.
     */
    offset += p->dp_offset >> 1;
    if (offset == 0 &&
        ! (bp->b_flags & B_READ) && ! du->label_writable)
    {
        /* Write to partition table not allowed. */
        bp->b_error = EROFS;
bad:    bp->b_flags |= B_ERROR;
        biodone(bp);
        return;
    }
    if (bp->b_blkno + nblk > part_size) {
        /* if exactly at end of partition, return an EOF */
        if (bp->b_blkno == part_size) {
            bp->b_resid = bp->b_bcount;
            biodone(bp);
            return;
        }
        /* or truncate if part of it fits */
        nblk = part_size - bp->b_blkno;
        if (nblk <= 0) {
            bp->b_error = EINVAL;
            goto bad;
        }
        bp->b_bcount = nblk << DEV_BSHIFT;
    }

    if (bp->b_dev == swapdev) {
        led_control(LED_SWAP, 1);
    } else {
        led_control(LED_DISK, 1);
    }

    s = splbio();
#ifdef UCB_METER
    if (du->dkindex >= 0) {
        dk_busy |= 1 << du->dkindex;
        dk_xfer[du->dkindex]++;
        dk_bytes[du->dkindex] += bp->b_bcount;
    }
#endif

    //printf("DBG: sdstrategy unit=%d %s offset=%d bcount=%d\n",
    //    unit, (bp->b_flags & B_READ) ? "READ" : "WRITE",
    //    offset, bp->b_bcount);

    /*
     * DBG: card_read()/card_write()'s return value (1=ok, 0=failed)
     * used to be discarded here - a genuine SD failure (including the
     * bounded card-ready-wait timeouts in sd.c/ed.c added this
     * session) was silently treated as a completed, successful I/O:
     * biodone(bp) ran regardless, with whatever partial/stale data
     * was already in the destination buffer. vm_swp.c's swap() (the
     * only caller that matters for the corruption chased this
     * session) explicitly panics on B_ERROR - "panic: hard err: swap"
     * never once fired despite confirmed real corruption, because
     * this path never set it. Wiring it up turns a silent bad-data
     * or opaque-hang failure into an actual diagnosable panic.
     */
    if (bp->b_flags & B_READ) {
        if (!card_read(unit, offset, bp->b_addr, bp->b_bcount)) {
            bp->b_error = EIO;
            bp->b_flags |= B_ERROR;
        }
    } else {
        if (!card_write(unit, offset, bp->b_addr, bp->b_bcount)) {
            bp->b_error = EIO;
            bp->b_flags |= B_ERROR;
        }
    }

    biodone(bp);
    if (bp->b_dev == swapdev) {
        led_control(LED_SWAP, 0);
    } else {
        led_control(LED_DISK, 0);
    }
#ifdef UCB_METER
    if (du->dkindex >= 0)
        dk_busy &= ~(1 << du->dkindex);
#endif
    splx(s);
}

int
sdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    int unit = sdunit(dev);
    int part = sdpart(dev);
    struct diskpart *dp;
    int i, error = 0;

    switch (cmd) {

    case DIOCGETMEDIASIZE:
        /* Get disk size in kbytes. */
        dp = &sddrives[unit].part[part];
        *(int*) addr = dp->dp_nsectors >> 1;
        break;

    case DIOCREINIT:
        for (i=0; i<=NPARTITIONS; i++)
            bflush(makedev(major(dev), i));
        sd_setup(unit);
        break;

    case DIOCGETPART:
        /* Get partition table entry. */
        dp = &sddrives[unit].part[part];
        *(struct diskpart*) addr = *dp;
        break;

    default:
        error = EINVAL;
        break;
    }
    return error;
}

/*
 * Test to see if device is present.
 * Return true if found and initialized ok.
 */
static int
sd_probe(config)
    struct conf_device *config;
{
    int unit = config->dev_unit;
    const char *ctlr_name = config->dev_cdriver->d_name;
    int ctlr_num = config->dev_ctlr;

    if (unit < 0 || unit >= NSD)
        return 0;
    printf("sd%u: port %s%d\n", unit, ctlr_name, ctlr_num);

    if (! card_init(unit)) {
        printf("sd%u: cannot open %s%u port\n", unit, ctlr_name, ctlr_num);
        return 0;
    }

    /* Disable the SD card. */
    sd_release(unit);

#ifdef UCB_METER
    dk_alloc(&sddrives[unit].dkindex, 1, (unit == 0) ? "sd0" : "sd1");
#endif
    return 1;
}

struct driver sddriver = {
    "sd", sd_probe,
};
