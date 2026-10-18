# Factory firmware backup

Full 4 MB flash image of the board as it came from the factory, taken 2026-10-14
before flashing the Yellow Pad firmware.

## The board

- ESP32-2432S028R "Cheap Yellow Display" (2.8" 320x240 touchscreen), sold by
  Shenzhen Jingcai Intelligent (manual: http://pan.jczn1688.com/1)
- Chip: ESP32-D0WD-V3 rev 3.1, dual core 240 MHz, Wi-Fi + Bluetooth
- Flash: 4 MB, no PSRAM
- USB: micro-USB through a CH340 serial chip (macOS driver is built in).
  Shows up as `/dev/cu.usbserial-*`; the number depends on the USB port.
- MAC: 68:09:47:85:c8:b0

## Files

- `factory-firmware-4MB.bin`: the whole flash, address 0x000000 to 0x3FFFFF
- `factory-firmware-4MB.bin.sha256`: checksum
  (`d701e3396309d2b6edd0554c8008bbafb84837d2588dc998cb52aea43f2c6a5e`)
- `backup_flash.sh`: the script that made the backup

## Restore the factory firmware

1. Close anything using the serial port (Arduino IDE, serial monitors).
2. Find the port: `ls /dev/cu.usbserial-*`
3. Check the backup is intact:

   ```bash
   cd /Users/pitosalas/mydev/yellow_pad/backup
   shasum -a 256 -c factory-firmware-4MB.bin.sha256
   ```

4. Write it back (replace `XXXX` with the port from step 2):

   ```bash
   uvx esptool --port /dev/cu.usbserial-XXXX --baud 115200 write-flash 0 factory-firmware-4MB.bin
   ```

   This overwrites everything, including saved settings, and takes a few minutes.
   esptool verifies what it writes, so a bad transfer fails instead of leaving a
   corrupt board. If it fails, just run it again.

The board resets when done and should boot into the factory demo.

## Making a new backup

`./backup_flash.sh [port] [baud]` reads the flash in 256 KB pieces and retries
pieces that fail. Use it rather than one big `read-flash`: reads through the
USB-C adapter hit corrupt data at every speed tried (921600, 460800, 115200)
when done in one go. It saves pieces to `chunks/`, resumes if re-run, and joins
them at the end. Before starting a new backup, delete `chunks/` (otherwise the
script reuses the old pieces) and move or rename the existing `.bin` (otherwise
it is overwritten).

## Notes

- `uvx` comes with uv (`brew install uv`) and runs esptool without installing it.
- The esptool bundled with the Arduino IDE in `~/Library/Arduino15` is
  Intel-only and fails with "bad CPU type" on this Mac (no Rosetta).
- `esptool image-info` reports "invalid magic number" on this file. That is
  expected: it is a whole-flash image, not a single app image.
