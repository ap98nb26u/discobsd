/*
 * Persistent boot parameters in cartridge backup SRAM - mrams build only.
 *
 * A small parameter block is kept in the GBA's cartridge backup SRAM so
 * settings survive across runs. This works on the ROM-root (mrams / plain
 * GBA / mGBA) build, where there is no writable filesystem to persist to.
 *
 * It does NOT work on the SD-root EverDrive (GBAED) build: there DiscoBSD
 * drives the EverDrive's SD card for its root FS, and that interferes with
 * the ED committing the SRAM buffer to its .sav file on the same card, so a
 * post-boot SRAM write is never flushed (verified on real X5 hardware: writes
 * persist on mrams + mGBA/VBA but not on GBAED). The GBAED build therefore
 * persists settings in a file on its writable root instead (put "sysctl -w"
 * lines in /etc/rc.local; see etc/rc). So the SRAM machinery below is compiled
 * only under MR_ENABLED; on other builds the entry points are no-ops.
 *
 * SRAM hardware rules (GBATEK): the region at 0x0E000000 is 32KB on an 8-bit
 * data bus, accessed one byte at a time (LDRB/STRB, never 16/32-bit, never
 * DMA), WAITCNT's SRAM field set to 8 cycles, and reads executed from RAM -
 * so the byte accessors live in .iwram_text (the section locore0.S actually
 * copies into IWRAM; the IWRAM_CODE/.iwram macro does NOT - it is orphaned
 * into ROM).
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <machine/gba.h>

/*
 * Save-type signature so mGBA/VBA (and the EverDrive) auto-detect a 32KB SRAM
 * save; word-aligned and kept referenced so it survives in .rodata. Harmless
 * on the GBAED build (its Save Type is set in the ED menu).
 */
__attribute__((used, aligned(4)))
const char sram_save_type_sig[] = "SRAM_V113";

#ifdef MR_ENABLED

#define SRAM	((volatile unsigned char *)GBA_SRAM_BASE)

/* Force these into real IWRAM and out-of-line: the SRAM byte access must run
 * from RAM, so it must not be inlined into a ROM-resident caller. */
#define SRAM_CODE	__attribute__((noinline, section(".iwram_text")))

SRAM_CODE static unsigned char
sram_read8(int off)
{
	return SRAM[off];
}

SRAM_CODE static void
sram_write8(int off, unsigned char v)
{
	SRAM[off] = v;
}

/*
 * Parameter block layout (version 1), a flat byte array at SRAM offset 0
 * (byte-serialized because of the 8-bit bus):
 *   0..3  magic 'D','B','P','1'
 *   4     version (1)
 *   5     checksum: sum of every other block byte, mod 256
 *   6     flags (reserved, 0)
 *   7     reserved (0)
 *   8..9  kbd auto-repeat initial delay, ms, little-endian
 *   10..11 kbd auto-repeat interval, ms, little-endian
 */
#define SP_LEN		12
#define SP_VERSION	1
#define SP_OFF_CKSUM	5
#define SP_OFF_DELAY	8
#define SP_OFF_RATE	10

/* swkbd owns the live auto-repeat values (ms) and the clamp; see swkbd.c. */
extern int swkbd_repeat_delay_ms;
extern int swkbd_repeat_rate_ms;
extern void swkbd_set_repeat(int delay_ms, int rate_ms);

static int sram_ready;		/* WAITCNT set once */

static unsigned char
sp_cksum(const unsigned char *b)
{
	unsigned int s = 0;
	int i;

	for (i = 0; i < SP_LEN; i++)
		if (i != SP_OFF_CKSUM)
			s += b[i];
	return (unsigned char)(s & 0xff);
}

static void
sram_setup(void)
{
	if (sram_ready)
		return;
	/* SRAM needs 8 waitstates: WAITCNT (0x4000204) bits 0-1 = 3. Only
	 * touch those bits, keeping the ROM waitstates already configured. */
	REG_WAITCNT = (uint16_t)(REG_WAITCNT | 0x0003);
	sram_ready = 1;
}

/* Read + validate the block at boot and apply it, else keep the compiled
 * defaults. Never writes here. */
void
sram_params_init(void)
{
	unsigned char b[SP_LEN];
	int i, d, r;

	sram_setup();
	for (i = 0; i < SP_LEN; i++)
		b[i] = sram_read8(i);

	if (b[0] == 'D' && b[1] == 'B' && b[2] == 'P' && b[3] == '1' &&
	    b[4] == SP_VERSION && b[SP_OFF_CKSUM] == sp_cksum(b)) {
		d = b[SP_OFF_DELAY] | (b[SP_OFF_DELAY + 1] << 8);
		r = b[SP_OFF_RATE]  | (b[SP_OFF_RATE + 1]  << 8);
		swkbd_set_repeat(d, r);	/* clamps to sane ranges */
		printf("sram: params v%d loaded (kbd repeat %d/%d ms)\n",
		    SP_VERSION, swkbd_repeat_delay_ms, swkbd_repeat_rate_ms);
	} else {
		printf("sram: no valid params, using defaults (kbd repeat %d/%d ms)\n",
		    swkbd_repeat_delay_ms, swkbd_repeat_rate_ms);
	}
}

/* Write the current live parameters back to SRAM as a fresh valid block.
 * Called only on an explicit user change (a sysctl write). */
void
sram_persist_params(void)
{
	unsigned char b[SP_LEN];
	int d = swkbd_repeat_delay_ms;
	int r = swkbd_repeat_rate_ms;
	int i;

	sram_setup();
	b[0] = 'D'; b[1] = 'B'; b[2] = 'P'; b[3] = '1';
	b[4] = SP_VERSION;
	b[5] = 0;			/* checksum, filled below */
	b[6] = 0; b[7] = 0;		/* flags, reserved */
	b[SP_OFF_DELAY]     = (unsigned char)(d & 0xff);
	b[SP_OFF_DELAY + 1] = (unsigned char)((d >> 8) & 0xff);
	b[SP_OFF_RATE]      = (unsigned char)(r & 0xff);
	b[SP_OFF_RATE + 1]  = (unsigned char)((r >> 8) & 0xff);
	b[SP_OFF_CKSUM] = sp_cksum(b);

	for (i = 0; i < SP_LEN; i++)
		sram_write8(i, b[i]);
}

#else /* !MR_ENABLED: SRAM persistence unavailable (see the header); the
	 GBAED build persists via /etc instead. */

void
sram_params_init(void)
{
}

void
sram_persist_params(void)
{
}

#endif /* MR_ENABLED */
