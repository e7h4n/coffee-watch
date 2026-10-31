"""Strict DiFluid cleartext envelope/identity decoder; no measurement completion logic."""


def decode_frame(packet: bytes) -> dict:
    if len(packet) < 6 or packet[:2] != b"\xdf\xdf":
        raise ValueError("invalid DiFluid envelope")
    if len(packet) != packet[4] + 6:
        raise ValueError("DiFluid payload length mismatch")
    if sum(packet[:-1]) % 256 != packet[-1]:
        raise ValueError("DiFluid checksum mismatch")
    function, command = packet[2:4]
    data = packet[5:-1]
    result = {"function": function, "command": command, "data_hex": data.hex().upper()}
    if function == 0 and command in (1, 2):
        # Only the SDK's exact model/firmware responses are interpreted here.
        result["kind"] = "model" if command == 1 else "firmware"
        result["value"] = data.decode("ascii")
    else:
        result["kind"] = "uninterpreted"
    return result
