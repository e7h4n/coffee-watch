#!/usr/bin/env python3
"""Interactive serial capture; sends no startup commands. USB open/close may reset StopWatch."""
import argparse
import datetime as dt
import json
import sys
import threading
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("output", type=Path)
    parser.add_argument("--quiet-rx", action="store_true",
                        help="keep raw RX in the file, omit RX lines from terminal output")
    parser.add_argument("--quiet-pixels", action="store_true",
                        help="save PX screenshot rows without printing them")
    args = parser.parse_args()
    import serial  # pyserial is included in an activated ESP-IDF environment

    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive create prevents accidentally overwriting a measurement run.
    with args.output.open("x") as log:
        port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
        port.dtr = False
        port.rts = False
        port.port = args.port
        port.open()
        lock = threading.Lock()
        stop = threading.Event()

        def save(kind, text):
            entry = {"host_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                     "source": kind, "line": text}
            with lock:
                log.write(json.dumps(entry, ensure_ascii=False) + "\n")
                log.flush()

        def receive():
            pending = bytearray()
            try:
                while not stop.is_set():
                    pending.extend(port.read(4096))
                    while b"\n" in pending:
                        line, _, pending = pending.partition(b"\n")
                        text = line.decode("utf-8", errors="replace").rstrip("\r")
                        save("device", text)
                        try:
                            raw_rx = json.loads(text).get("event") == "rx"
                        except (ValueError, AttributeError):
                            raw_rx = False
                        if not ((args.quiet_rx and raw_rx) or (args.quiet_pixels and text.startswith("PX:"))):
                            print(text, flush=True)
            except serial.SerialException as exc:
                save("capture_error", str(exc))
                print(str(exc), file=sys.stderr)
                stop.set()
            finally:
                if pending:
                    save("partial", pending.decode("utf-8", errors="replace"))

        save("capture", f"port={args.port}; no automatic commands")
        reader = threading.Thread(target=receive, daemon=True)
        reader.start()
        print("Commands: scan | connect ADDRESS TYPE | services CONN | gatt CONN | "
              "sub CONN CCCD 1 | read CONN HANDLE | scale CONN HANDLE CODE | "
              "disconnect CONN | mark TEXT | quit", file=sys.stderr)
        print("Adapter: begin CONN | status CONN; rs-events-v4 decodes discovered 00FA/FF01 notifications", file=sys.stderr)
        try:
            for line in sys.stdin:
                command = line.strip()
                if stop.is_set() or command == "quit":
                    break
                if not command:
                    continue
                if command.startswith("mark "):
                    save("marker", command[5:])
                    continue
                try:
                    encoded = command.encode("ascii")
                except UnicodeEncodeError:
                    print("Device commands must be ASCII; use mark for notes.", file=sys.stderr)
                    continue
                if len(encoded) > 190:
                    print("Command too long", file=sys.stderr)
                    continue
                save("host_command", command)
                port.write((command + "\n").encode("ascii"))
        except KeyboardInterrupt:
            pass
        finally:
            stop.set()
            reader.join(timeout=2)
            port.close()


if __name__ == "__main__":
    main()
