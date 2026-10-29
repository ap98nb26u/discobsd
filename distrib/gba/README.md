# DiscoBSD/gba - 2.11BSD-based OS for the Nintendo Game Boy Advance

DiscoBSD/gba is a port of DiscoBSD to the Game Boy Advance (ARM7TDMI).
The port has two kernel builds:

 * **GBA** - the plain build. The root file system is embedded in the ROM
   image (a ROM memory disk), so the cartridge is self-contained. It runs
   in an emulator such as [mGBA][1] or VBA-M, or from an EverDrive (where
   its root is writable until power-off). A plain flash cartridge should
   work too, but is untested.
 * **GBAED** - the EverDrive build. The root file system lives on the
   flash cartridge's real SD card, and swap in its PSRAM. It targets the
   EverDrive GBA X5.

[1]: https://mgba.io/

## Currently supported hardware

 * Game Boy Advance (ARM7TDMI), in the mGBA / VBA-M emulator or on real
   hardware via an EverDrive (GBA build).
 * EverDrive GBA X5 flash cartridge with an SD card (GBAED build).

## Kernel images

Each kernel builds into its own directory under `sys/arch/gba/compile`:

 * `GBA/`    - the plain (memory-disk) kernel.
 * `GBALOG/` - the GBA kernel plus AGBPrint output to the emulator's log.
 * `GBAED/`  - the EverDrive (SD-card) kernel.

The build produces several formats of the kernel `unix` in each directory:
ELF `unix.elf`, Intel HEX `unix.hex`, and a Game Boy Advance ROM binary
`unix.bin`. Load `unix.bin` onto a flash cartridge, or run it directly in an
emulator:

  ```sh
    $ mgba sys/arch/gba/compile/GBA/unix.bin
  ```

## SD card layout (GBAED)

The GBAED build reads its root file system from the SD card (swap is in the
EverDrive's PSRAM). The card uses an MBR partition table:

 * Partition 1 - the EverDrive's own FAT32 boot partition (menu / firmware).
 * Partition 2 - the DiscoBSD root file system.
 * Partition 3 - unused (reserved for crash dumps, which the port does not
   implement).

Write the DiscoBSD root file system image `rootfs.img` (built by the top-level
`make`, see below) onto partition 2 of the SD card, for example with `dd`:

  ```sh
    $ dd bs=1M if=distrib/gba/rootfs.img of=/dev/<sd-card-partition-2>
  ```

The plain GBA build needs no SD card: it embeds a smaller image with the
same files, `distrib/gba/mdroot.img`, directly in the ROM.

## Console and logging in

The primary console is the on-screen LCD text console, with an on-screen
software keyboard for input. No external hardware is required.

The GBAED build also supports an optional serial console over the link cable
(115200 baud), which can be used with a link-cable-to-serial adapter and a
terminal program such as `cu`, `screen`, `minicom`, or `putty`.

Log in to DiscoBSD with user `root` and a blank password.

Shut the system down with:
  ```sh
    $ shutdown -h now
  ```
`halt` and `reboot` also bring down the system.

## Building DiscoBSD/gba on a Unix-like host

Build the kernels (GBA, GBALOG and GBAED) and the root file system image
with:
  ```sh
    $ make MACHINE=gba MACHINE_ARCH=arm distribution
  ```
(`make ... all` builds the same, but does not install a fresh `/etc` into
the image.)

This produces `distrib/gba/rootfs.img` and the `unix.bin` / `unix.hex` kernels
in `sys/arch/gba/compile/GBA`, `GBALOG` and `GBAED`.

A single kernel can be built independently, for example:
  ```sh
    $ cd sys/arch/gba/compile/GBA
    $ make
  ```
Note: Building the kernel requires the `tools/config` config utility.

See `sys/arch/gba/NOTES.hardware` and `sys/arch/gba/NOTES.toolchain` for
details of the port's hardware usage and native toolchain work.
