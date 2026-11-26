"""
stand_protocol.py - link to the D2.GreenHouse stand over the USB virtual COM port.

The stand firmware speaks a line-based text protocol (see docs/en/protocol.md):
    PC -> stand : "SET HEAT_TSET 26.5\n"
    stand -> PC : "#TEL key=value;...", "#OK ...", "#ERR n ...", "#PAR ...",
                  "#EVT ...", "#BUS ...", "#INFO ...", "#NODE ..."
This module has no GUI dependencies and can be used from scripts.
"""
import queue
import threading
import time

try:
    import serial                       # pyserial
    import serial.tools.list_ports
except ImportError:                     # GUI shows a message if missing
    serial = None

# error word bits (firmware app.h)
ERROR_BITS = {
    0x00000001: "Wi-Fi",
    0x00000004: "DTS: temperature sensor not ready",
    0x00000010: "RS-485: a module does not answer",
    0x00004000: "Some soil moisture sensors absent",
    0x00008000: "All soil moisture sensors absent",
    0x00010000: "Tank: no water",
    0x01000000: "No link with STM32F030 (M8SPI)",
    0x02000000: "EEPROM: defaults loaded",
    0x04000000: "RTC not answering",
    0x08000000: "OVERHEAT",
    0x10000000: "AM2301A air humidity sensor absent",
}

FAULTS = ["T1", "T2", "T3", "RH", "W1", "W2", "TANK", "F030", "RS485"]


def list_ports():
    """Names of available serial ports (+ the simulator link on Linux)."""
    names = []
    if serial is not None:
        names = [p.device for p in serial.tools.list_ports.comports()]
    import os
    if os.path.exists("/tmp/d2gh_stand"):
        names.insert(0, "/tmp/d2gh_stand")
    return names


STR_KEYS = {"K", "ERR", "RGB", "mode", "time", "unit", "min", "max", "def"}


def parse_kv(text):
    """'a=1;b=2.5;c=-' -> {'a': 1.0, 'b': 2.5, 'c': None} (numbers as float,
    keys in STR_KEYS stay strings, e.g. K=00101, ERR=0800000A)."""
    out = {}
    for item in text.split(";"):
        if "=" not in item:
            continue
        k, v = item.split("=", 1)
        k = k.strip()
        v = v.strip()
        if v == "-":
            out[k] = None
            continue
        if k in STR_KEYS:
            out[k] = v
            continue
        try:
            out[k] = float(v)
        except ValueError:
            out[k] = v
    return out


def decode_errors(word):
    return [txt for bit, txt in ERROR_BITS.items() if word & bit]


class StandLink:
    """Serial link with a background reader thread.

    Received lines are put into self.lines (queue.Queue) as tuples
    (tag, payload, raw) where tag is 'TEL', 'OK', 'ERR', 'PAR', 'EVT', ...
    """

    def __init__(self):
        self.ser = None
        self.lines = queue.Queue()
        self._stop = threading.Event()
        self._thread = None

    @property
    def connected(self):
        return self.ser is not None and self.ser.is_open

    def open(self, port, baud=115200):
        if serial is None:
            raise RuntimeError("pyserial is not installed: pip install pyserial")
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self._stop.clear()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def close(self):
        self._stop.set()
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def send(self, cmd):
        """Send one command line (without '\\n')."""
        if self.connected:
            self.ser.write((cmd.strip() + "\n").encode("ascii", "replace"))

    def _reader(self):
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(512)
            except Exception as e:                    # cable removed etc.
                self.lines.put(("LINK", str(e), ""))
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("ascii", "replace").strip()
                if not line:
                    continue
                if line.startswith("#"):
                    tag, _, payload = line[1:].partition(" ")
                else:
                    tag, payload = "TXT", line
                self.lines.put((tag, payload, line))


if __name__ == "__main__":                            # quick console test
    import sys
    link = StandLink()
    link.open(sys.argv[1] if len(sys.argv) > 1 else "/tmp/d2gh_stand")
    for c in ("INFO", "STREAM 1000"):
        link.send(c)
    t0 = time.time()
    while time.time() - t0 < 3:
        try:
            print(link.lines.get(timeout=0.5))
        except queue.Empty:
            pass
    link.close()
