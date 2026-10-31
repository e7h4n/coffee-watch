#!/usr/bin/env python3
"""Replay BOOKOO packets without inferring dose, command ACKs or brew completion."""
import argparse
import functools
import json
import operator
import re
import sys


def decode_bookoo(packet):
    if len(packet) != 20 or packet[0] != 3:
        raise ValueError("expected a 20-byte BOOKOO notification")
    if functools.reduce(operator.xor, packet) != 0:
        raise ValueError("BOOKOO XOR mismatch")
    kind = packet[1]
    result = {"type": f"{kind:02X}", "checksum_valid": True}

    def magnitude(offset, sign_offset, name):
        # The physical unit emits ASCII '+' / '-'; preserve unknown encodings.
        result[name + "_magnitude_g"] = int.from_bytes(packet[offset:offset+3], "big") / 100
        result[name + "_sign_raw"] = packet[sign_offset]
        if packet[sign_offset] in (0x2B, 0x2D):
            result[name + "_g"] = result[name + "_magnitude_g"] * (-1 if packet[sign_offset] == 0x2D else 1)

    if kind == 0x0B:
        result["kind"] = "weight"
        result["elapsed_ms"] = int.from_bytes(packet[2:5], "big")
        result["display_unit_raw"] = packet[5]
        magnitude(7, 6, "weight")
        result["flow_magnitude_g_s"] = int.from_bytes(packet[11:13], "big") / 100
        result["flow_sign_raw"] = packet[10]
        if packet[10] in (0x2B, 0x2D):
            result["flow_g_s"] = result["flow_magnitude_g_s"] * (-1 if packet[10] == 0x2D else 1)
        result["battery_percent"] = packet[13]
    elif kind == 0x0F:
        result["kind"] = "dose"
        magnitude(3, 2, "dose")
    elif kind == 0x0D:
        result["kind"] = "auto_event"
        result["state_raw"] = packet[2]
        result["elapsed_ms"] = int.from_bytes(packet[3:6], "big")
        magnitude(7, 6, "weight")
        result["settlement_magnitude"] = int.from_bytes(packet[11:13], "big") / 100
        result["settlement_sign_raw"] = packet[10]
    else:
        result["kind"] = "unknown"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    parser.add_argument("--conn", type=int, required=True, help="BOOKOO connection handle")
    parser.add_argument("--epoch", type=int, required=True, help="connection generation from peer record")
    parser.add_argument("--handle", type=int, action="append", required=True,
                        help="value handle from GATT discovery; repeat for FF11 and FF12")
    parser.add_argument("--boot", type=int, default=0, help="zero-based boot segment (default: first segment)")
    args = parser.parse_args()
    errors = matched = boot = 0
    seen_device = False
    with open(args.capture) as source:
        for number, line in enumerate(source, 1):
            try:
                entry = json.loads(line)
                if entry.get("source") == "device":
                    try:
                        entry = json.loads(entry["line"])
                    except json.JSONDecodeError:
                        continue  # ESP-IDF boot/status lines remain in the raw capture
                if entry.get("event") == "boot" and seen_device:
                    boot += 1
                if "event" in entry:
                    seen_device = True
                if boot != args.boot or entry.get("event") != "rx":
                    continue
                fields = dict(re.findall(r"(\w+)=([^ ]+)", entry["detail"]))
                if (int(fields["conn"]), int(fields["epoch"])) != (args.conn, args.epoch) or int(fields["handle"]) not in args.handle:
                    continue
                result = decode_bookoo(bytes.fromhex(fields["hex"]))
                matched += 1
                print(json.dumps({"line": number, "boot": boot, "us": entry["us"],
                                  "dropped_total": entry.get("dropped_total"), **result}))
            except (ValueError, KeyError, TypeError) as exc:
                errors += 1
                print(json.dumps({"line": number, "error": str(exc)}), file=sys.stderr)
    if not matched:
        print("No matching valid notifications; check boot/conn/epoch/handle.", file=sys.stderr)
    return 1 if errors else (0 if matched else 2)


if __name__ == "__main__":
    raise SystemExit(main())
