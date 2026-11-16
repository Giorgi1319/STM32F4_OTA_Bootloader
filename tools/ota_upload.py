#!/usr/bin/env python3
"""Upload an application image to the STM32F446 bootloader over the HC-05.

Examples (run from the Bootloader folder):

  python tools/ota_upload.py STM32F446_Application/build/Debug/STM32F446_Application.bin -p COM5
  python tools/ota_upload.py app.bin -p COM5 --no-trigger       board is already in recovery mode
  python tools/ota_upload.py app.bin --header-out header.bin    header for flashing the app by hand

Protocol (STM32F446_Bootloader/Core/Src/main.c and bl_ota.c):

  1. The host sends "OTA" to the running application. It leaves a request in
     backup SRAM and resets into the bootloader.
  2. The bootloader prints "OTA mode" and sends ACK (0x06).
  3. The host sends a 16-byte header: magic, image size, CRC-32, version,
     each a little-endian uint32.
  4. The bootloader erases the header and application sectors (a few seconds),
     then sends ACK.
  5. The host sends the image in 256-byte chunks, one chunk per ACK.
  6. The bootloader checks the CRC, writes the application header, prints
     "OTA done" (or "OTA failed, err=N") and starts the new application.

In recovery mode (no valid application) the bootloader sends an ACK about every
10 s by itself, so step 1 is not needed: pass --no-trigger to save up to 10 s.

Needs pyserial for uploading (pip install pyserial); --header-out works without it.
"""

import argparse
import re
import struct
import sys
import time
import zlib

try:
    import serial
except ImportError:
    serial = None

# Must match the bootloader: flash_layout.h, app_header.h, bl_ota.h and main.c
APP_MAGIC       = 0xDEADBEEF
APP_HEADER_ADDR = 0x08008000
APP_START_ADDR  = 0x0800C000
APP_MAX_SIZE    = 0x74000          # 464 KB, sectors 3-7
RAM_START_ADDR  = 0x20000000
RAM_END_ADDR    = 0x20020000
CHUNK_SIZE      = 256              # BL_OTA_CHUNK_SIZE
ACK             = 0x06             # OTA_ACK
TRIGGER         = b"OTA"           # ota_command_poll() in the application

# Host-side timeouts, in seconds
ERASE_TIMEOUT  = 20.0   # ACK after the header: erasing sectors 2-7 takes up to about 8 s
CHUNK_TIMEOUT  = 5.0    # ACK after a chunk: programming 256 bytes takes a few milliseconds
RESULT_TIMEOUT = 15.0   # "OTA done" after the last chunk: CRC of the flash + header write
BOOT_TIMEOUT   = 5.0    # "Booting app" after "OTA done": one more CRC of the flash
RETRY_LISTEN   = 2.0    # after a rejected header: wait this long for a recovery-mode ACK

# bl_ota.h
BL_ERRORS = {
    -1: "no header arrived before the bootloader's 10 s timeout",
    -2: "header magic is wrong",
    -3: "image size is 0 or larger than the application area",
    -4: "erasing flash failed",
    -5: "a data chunk did not arrive before the bootloader's 10 s timeout",
    -6: "programming flash failed",
    -7: "CRC of the received data does not match the header",
    -8: "CRC read back from flash does not match the header",
    -9: "writing the application header failed",
}
HEADER_ERRORS = (-1, -2, -3)   # reported before anything is erased, so a retry is safe

FAILURE_RE = re.compile(r"OTA failed, err=(-?\d+)")


class UploadError(Exception):
    pass


class BoardError(UploadError):
    """The bootloader printed "OTA failed, err=N"."""

    def __init__(self, code):
        self.code = code
        super().__init__(f"bootloader reported err={code}: {BL_ERRORS.get(code, 'unknown error')}")


def failure_code(line):
    m = FAILURE_RE.search(line)
    return int(m.group(1)) if m else None


def check_image(data):
    """The same checks as bootloader_is_app_valid(), so a wrong file is refused
    before the bootloader erases the current application."""
    if data[:4] == b"\x7fELF":
        raise UploadError("this is an ELF file; pass the .bin built next to it")
    if data[:1] == b":":
        raise UploadError("this looks like an Intel HEX file; pass the .bin built next to it")
    if len(data) < 8:
        raise UploadError(f"image is only {len(data)} bytes")
    if len(data) > APP_MAX_SIZE:
        raise UploadError(f"image is {len(data)} bytes, the application area holds {APP_MAX_SIZE}")

    sp, reset = struct.unpack_from("<II", data)
    if not RAM_START_ADDR < sp <= RAM_END_ADDR:
        raise UploadError(f"initial stack pointer 0x{sp:08X} is not in RAM; is this the application .bin?")
    if not reset & 1 or not APP_START_ADDR <= reset < APP_START_ADDR + len(data):
        raise UploadError(f"reset vector 0x{reset:08X} is outside the image at 0x{APP_START_ADDR:08X}; "
                          f"the application must be linked for 0x{APP_START_ADDR:08X} "
                          "(FLASH ORIGIN in STM32F446xx_FLASH.ld)")


def write_header_file(path, bin_path, image, version):
    """app_header_t as the bootloader writes it after a successful OTA."""
    data = struct.pack("<5I", 0xFFFFFFFF, APP_MAGIC, len(image), zlib.crc32(image), version)
    with open(path, "wb") as f:
        f.write(data)
    print(f"Wrote {path} ({len(data)} bytes). Program it at 0x{APP_HEADER_ADDR:08X} together with "
          f"this exact .bin at 0x{APP_START_ADDR:08X}, for example:")
    print(f'  STM32_Programmer_CLI -c port=SWD -w "{bin_path}" 0x{APP_START_ADDR:08X} -v')
    print(f'  STM32_Programmer_CLI -c port=SWD -w "{path}" 0x{APP_HEADER_ADDR:08X} -v -rst')


class Link:
    """The serial connection to the board: picks out ACK bytes and prints the board's text lines."""

    def __init__(self, ser):
        self.ser = ser
        self.pending = bytearray()   # received but not yet looked at
        self.line = bytearray()      # text line being collected
        self.progress_open = False   # a progress bar without a newline is on screen
        self.app_erased = False      # the bootloader accepted the header and erased the old app

    def send(self, data):
        self.ser.write(data)
        self.ser.flush()

    def discard_input(self):
        self.ser.reset_input_buffer()
        self.pending.clear()
        self.line.clear()

    def show(self, text):
        if self.progress_open:
            sys.stdout.write("\n")
            self.progress_open = False
        print(text, flush=True)

    def progress(self, done, total, started):
        width = 30
        filled = width * done // total
        rate = done / max(time.monotonic() - started, 1e-3)
        sys.stdout.write(f"\r  [{'#' * filled}{'.' * (width - filled)}] {done}/{total} bytes, {rate:.0f} B/s")
        if done >= total:
            sys.stdout.write("\n")
        sys.stdout.flush()
        self.progress_open = done < total

    def _next_byte(self, deadline):
        while not self.pending:
            if time.monotonic() >= deadline:
                return None
            self.pending += self.ser.read(self.ser.in_waiting or 1)   # read timeout is 0.1 s
        b = self.pending[0]
        del self.pending[0]
        return b

    def _collect(self, b):
        """Add a text byte; return the line once it is complete."""
        if b == 0x0A:
            text = self.line.decode("ascii", "replace").strip()
            self.line.clear()
            if text:
                self.show(f"  board: {text}")
                return text
        elif b != 0x0D:
            self.line.append(b)
        return None

    def wait_ack(self, timeout, abort_on_fail=True):
        """True once an ACK arrives, False on timeout. With abort_on_fail, an
        "OTA failed" line from the bootloader raises BoardError."""
        deadline = time.monotonic() + timeout
        while True:
            b = self._next_byte(deadline)
            if b is None:
                return False
            if b == ACK:
                return True
            line = self._collect(b)
            if line and failure_code(line) is not None:
                if abort_on_fail:
                    raise BoardError(failure_code(line))
                self.show("  (an earlier attempt timed out; the bootloader asks again)")

    def wait_line(self, prefixes, timeout):
        """Return the first board line that starts with one of prefixes, or None on timeout."""
        deadline = time.monotonic() + timeout
        while True:
            b = self._next_byte(deadline)
            if b is None:
                return None
            if b == ACK:
                continue
            line = self._collect(b)
            if line and line.startswith(prefixes):
                return line


def reach_bootloader(link, trigger, timeout):
    """Steps 1-2: return once the bootloader has sent the ACK that asks for the header."""
    if trigger:
        link.discard_input()   # text the application printed earlier
        print('Sending "OTA" to the application...')
        link.send(TRIGGER)
    else:
        # Keep what already arrived: an ACK received since the port was opened is
        # recent, and the bootloader is still waiting for the header.
        print("Waiting for the bootloader's ACK (recovery mode)...")

    if not link.wait_ack(timeout, abort_on_fail=False):
        if trigger:
            hint = "Is the HC-05 connected and the application running?"
        else:
            hint = ("Is the board in recovery mode (LED toggling about every 10 s)? "
                    "If the application is running, leave out --no-trigger.")
        raise UploadError(f"no ACK from the bootloader within {timeout:.0f} s. {hint}")


def upload(link, image, version, trigger, connect_timeout, retries):
    total = len(image)
    header = struct.pack("<4I", APP_MAGIC, total, zlib.crc32(image), version)

    # Steps 1-4: get the header accepted. A header the bootloader rejects
    # (err -1..-3) erased nothing, so it is safe to try again.
    for attempt in range(retries + 1):
        # After a rejected header a recovery-mode bootloader asks again right away;
        # otherwise it went back to the old application, which needs the trigger.
        if attempt == 0 or not link.wait_ack(RETRY_LISTEN, abort_on_fail=False):
            reach_bootloader(link, trigger, connect_timeout)

        print("Sending header; the bootloader now erases the old application...")
        link.send(header)
        try:
            if not link.wait_ack(ERASE_TIMEOUT):
                link.app_erased = True   # unknown how far it got; assume the worst
                raise UploadError(f"no ACK within {ERASE_TIMEOUT:.0f} s after the header")
            break
        except BoardError as err:
            if err.code not in HEADER_ERRORS or attempt == retries:
                raise
            print(f"Header rejected ({err}), trying again")

    link.app_erased = True

    # Step 5: one chunk per ACK. The ACK for the first chunk is the one after the header.
    print(f"Sending {total} bytes (about {total * 10 / link.ser.baudrate:.0f} s "
          f"at {link.ser.baudrate} baud)...")
    started = time.monotonic()
    for offset in range(0, total, CHUNK_SIZE):
        if offset and not link.wait_ack(CHUNK_TIMEOUT):
            raise UploadError(f"no ACK for the chunk at offset {offset}")
        chunk = image[offset:offset + CHUNK_SIZE]
        link.send(chunk)
        link.progress(offset + len(chunk), total, started)

    # Step 6
    line = link.wait_line(("OTA done", "OTA failed"), RESULT_TIMEOUT)
    if line is None:
        raise UploadError(f"no result from the bootloader within {RESULT_TIMEOUT:.0f} s after the last chunk")
    if failure_code(line) is not None:
        raise BoardError(failure_code(line))
    print(f"Upload verified by the bootloader ({total} bytes in {time.monotonic() - started:.1f} s).")

    if link.wait_line(("Booting app",), BOOT_TIMEOUT):
        link.wait_line((), 1.0)   # show what the new application prints first
        print("The bootloader started the new application.")
    else:
        print('Warning: the bootloader did not print "Booting app".')


def u32(text):
    try:
        value = int(text, 0)
    except ValueError:
        raise argparse.ArgumentTypeError(f"not a number: {text}")
    if not 0 <= value <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("must fit in 32 bits")
    return value


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Upload an application image to the STM32F446 bootloader over the HC-05.",
        epilog=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bin", help="application image (STM32F446_Application.bin, linked for 0x0800C000)")
    ap.add_argument("-p", "--port", help="serial port of the HC-05, e.g. COM5 (any pyserial URL works)")
    ap.add_argument("-b", "--baud", type=int, default=9600, help="baud rate (default 9600, as in both projects)")
    ap.add_argument("--fw-version", type=u32, default=1,
                    help="version number stored in the application header (default 1)")
    ap.add_argument("--no-trigger", action="store_true",
                    help='do not send "OTA" first; for a board already in recovery mode')
    ap.add_argument("--connect-timeout", type=float, default=25.0,
                    help="seconds to wait for the bootloader's first ACK (default 25)")
    ap.add_argument("--retries", type=int, default=2,
                    help="how often to try again when the bootloader rejects the header (default 2)")
    ap.add_argument("--header-out", metavar="FILE",
                    help=f"write the application header for programming by hand at 0x{APP_HEADER_ADDR:08X}")
    args = ap.parse_args(argv)

    if not args.port and not args.header_out:
        ap.error("give --port to upload, --header-out to write a header file, or both")

    try:
        with open(args.bin, "rb") as f:
            image = f.read()
        check_image(image)
    except (OSError, UploadError) as err:
        print(f"Error: {args.bin}: {err}", file=sys.stderr)
        return 1

    print(f"Image: {args.bin}, {len(image)} bytes, CRC-32 0x{zlib.crc32(image):08X}, version {args.fw_version}")

    if args.header_out:
        try:
            write_header_file(args.header_out, args.bin, image, args.fw_version)
        except OSError as err:
            print(f"Error: {err}", file=sys.stderr)
            return 1
    if not args.port:
        return 0

    if serial is None:
        print("Error: pyserial is not installed (pip install pyserial)", file=sys.stderr)
        return 1
    try:
        ser = serial.serial_for_url(args.port, baudrate=args.baud, timeout=0.1, write_timeout=10)
    except (serial.SerialException, ValueError) as err:
        print(f"Error: cannot open {args.port}: {err}", file=sys.stderr)
        print("Check that the HC-05 is paired and that no serial monitor has the port open.", file=sys.stderr)
        return 1

    link = Link(ser)
    try:
        upload(link, image, args.fw_version, not args.no_trigger, args.connect_timeout, args.retries)
        return 0
    except (UploadError, serial.SerialException, KeyboardInterrupt) as err:
        link.show(f"Error: {err}" if not isinstance(err, KeyboardInterrupt) else "Interrupted")
        if link.app_erased or (isinstance(err, BoardError) and err.code not in HEADER_ERRORS):
            print("The old application has been erased. The bootloader stays in recovery mode and "
                  "accepts another upload: run again with --no-trigger.")
        return 1
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
