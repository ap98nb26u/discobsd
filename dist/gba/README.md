# DiscoBSD for Game Boy Advance — prebuilt images

Prebuilt binaries so you can try the DiscoBSD GBA port without setting up a
cross toolchain. Built from this repository's `master` at commit `ebca5ff9`
(2026-10-21, DiscoBSD 2.7-current); all three kernels report
`DiscoBSD 2.7-current`.

DiscoBSD is a 2.11BSD-derived UNIX for microcontrollers. This is the Arm
GBA (ARM7TDMI) port: a multi-user kernel with an on-screen soft keyboard,
an LCD text console, `sh`, `vi`, and the usual small userland.

What is in this build (besides the shell and userland):

- **Graphics**: `/dev/tft0`-`tft2` framebuffer devices (Mode 3 240x160,
  Mode 5 160x128 double-buffered); demos and a Tetris in
  `/usr/share/examples/gba` (`tetris`, `gball5`, `imgdemo`, ...).
- **Sound**: the console bell, PSG tones, and PCM playback through
  `/dev/audio` (8/16 kHz, mono/stereo).
- **A C compiler on the device**: `cc` (Smaller C with an ARM backend, plus
  `as` and `ld`) builds programs on the GBA itself; try
  `/usr/share/examples/gba/hello.c`.  On the EverDrive kernel it can even
  rebuild itself (`/usr/share/examples/gba/selfhost/selfhost.sh`).
- **Networking (EverDrive + a USB-serial link cable)**: `slipd` is a small
  TCP/IP host over SLIP on the link port at 115200 baud - ping, echo /
  discard / chargen, and **telnet logins** (two sessions).  The PC side
  (WSL2 + Windows) is in [`tools/gbanet`](https://github.com/ap98nb26u/discobsd/tree/master/tools/gbanet)
  on `master`; see its README for the setup.
- **Debugging**: if the system stops responding, hold **L+R+SELECT** - the
  kernel prints where it is and every process's state on the LCD.

A short demo, recorded in mGBA:

https://github.com/user-attachments/assets/de7e6b4d-3066-4683-97d2-ca4a127a9263

These images are also attached to the
[gba-2.7-prebuilt-2026-10-21](https://github.com/ap98nb26u/discobsd/releases/tag/gba-2.7-prebuilt-2026-10-21)
release; the earlier
[2026-09-22](https://github.com/ap98nb26u/discobsd/releases/tag/gba-2.7-prebuilt-2026-09-22)
build is kept as its own release.

## Files

| File | What it is |
|------|------------|
| `discobsd-gba.gba` | **Start here.** Plain GBA ROM for emulators / flash carts. The root filesystem is embedded in the ROM (a read-only RAM disk), so it is completely self-contained — just load it. |
| `discobsd-gbalog.gba` | Same as above, but also mirrors the kernel/console output to **AGBPrint** so you can capture a log in an emulator (see below). |
| `discobsd-gbaed.gba` | Kernel for the **EverDrive GBA X5** (real hardware). It reads its root filesystem from the SD card at runtime, so it also needs `rootfs.img` (below). Tiny (~140 KB) because it does not embed the filesystem. |
| `rootfs.img` | The root filesystem image (bare, no partition table). Needed only for the EverDrive setup. |

## Run it in an emulator (easiest)

Load `discobsd-gba.gba` in **mGBA** or **VisualBoyAdvance / VBA-M**. It boots
to a login prompt.

- Login: `root` (no password).
- The bottom rows of the screen are an on-screen **soft keyboard**: move the
  cursor with the D-pad and press A to type; the shoulder/other buttons switch
  modes. Try `ls /`, `cat /etc/motd`, `vi`, etc.
- `date` shows JST by default; `TZ=GMT date`, `TZ=UTC date`, etc. also work
  (the full zoneinfo database is included).

### Capturing a log with AGBPrint (`discobsd-gbalog.gba`)

The console is mirrored to AGBPrint, which mGBA and VisualBoyAdvance can show:

- **mGBA**: enable the logging window; AGBPrint output appears there.
- **VBA-M (Windows)**: launch from a command prompt with `--verbose`, then
  Tools → Log window → tick **AGBPrint**. The output appears on the command
  prompt's **stdout** (not inside the log window).

## Run it on real hardware (EverDrive GBA X5)

1. Copy `discobsd-gbaed.gba` onto the EverDrive's FAT32 card like any ROM and
   launch it from the EverDrive menu.
2. Write `rootfs.img` onto the **DiscoBSD root partition** of the SD card —
   MBR partition **#2** (`sd0b`), NOT sector 0. Writing to sector 0 would
   destroy the EverDrive's own FAT32 boot partition. For example, if the card
   is `/dev/sdX` and partition 2 is `/dev/sdX2`:
   ```sh
   sudo dd bs=1M if=rootfs.img of=/dev/sdX2
   ```
   (Adjust the device name to your card. Double-check it first with `lsblk`.)
3. The EverDrive kernel roots on `sd0b`, runs a boot-time `fsck`, and gives you
   a console on the LCD (and on the EverDrive's serial port).

## Build from source

These images come from this repo. To rebuild them from a clean tree:
```sh
make MACHINE=gba MACHINE_ARCH=arm clean
rm -f lib/libc.a                                # clean leaves it; members would pile up
make MACHINE=gba MACHINE_ARCH=arm distribution  # userland, /etc, kernels, rootfs.img
make -C sys/arch/gba/compile/GBALOG all         # relink GBALOG around the new rootfs.img
```
`distribution` relinks the GBA kernel around the fresh `rootfs.img` itself;
GBALOG embeds the same image, so it is relinked by hand afterwards.  The
images here are `sys/arch/gba/compile/{GBA,GBALOG,GBAED}/unix.bin` and
`distrib/gba/rootfs.img`.

The Arm cross toolchain is `arm-none-eabi-gcc` (see `tools/linux/README.md`).
Switching `MACHINE` between builds needs a `make clean` first, because
`lib/libc.a` is shared across architectures.
