"""Offline RS single-measurement proof, based on official CoffeeOS 1.1.25.

Uses the app's fixed level-1 RS parameters and the local OpenSSL executable.
This is a capture decoder, not yet a BLE stream adapter or a command API.
"""
import json
import math
import struct
import subprocess
import sys

KEY = '7a3fe91c8b5d2a6e4c91f738b264d50a'
IV = '9e2b7c451f8a3d6be457920c8f3a6db1'


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


def unpack_frame(frame):
    if not frame.startswith(b'\xda\xda') or not frame.endswith(b'\x0a\x0a'):
        raise ValueError('invalid RS frame boundaries')
    wire = frame[2:-2]
    body = bytearray()
    i = 0
    while i < len(wire):
        byte = wire[i]
        if byte == 0xfa:
            i += 1
            if i == len(wire) or wire[i] not in (0xda, 0x0a, 0xfa):
                raise ValueError('invalid RS escape')
            byte = wire[i]
        elif byte in (0xda, 0x0a):
            raise ValueError('unescaped RS boundary byte')
        body.append(byte)
        i += 1
    if len(body) < 18 or sum(body[:-1]) & 255 != body[-1]:
        raise ValueError('RS outer checksum mismatch')
    if body[-2] & 3 != 1:
        raise ValueError('only observed RS level-1 frames supported')
    ciphertext = bytes(body[:-2])
    if len(ciphertext) % 16:
        raise ValueError('RS ciphertext block length mismatch')
    return ciphertext


def parse_measurement(plain):
    if len(plain) < 8 or plain[:4] != bytes.fromhex('01031200'):
        raise ValueError('not the validated RS single-result interface')
    size = int.from_bytes(plain[4:6], 'little')
    end = 6 + size
    if size < 30 or end + 2 > len(plain) or len(plain) - end - 2 >= 16:
        raise ValueError('RS payload length mismatch')
    if crc16(plain[:end]) != int.from_bytes(plain[end:end+2], 'little'):
        raise ValueError('RS inner CRC mismatch')
    data = plain[6:end]
    error, brix, avg, count, temperature, flags, refraction, glue, liquid = struct.unpack_from('<BffBfIfff', data)
    if not all(math.isfinite(v) for v in (brix, avg, temperature, refraction, glue, liquid)):
        raise ValueError('nonfinite RS measurement')
    return dict(event='R3_DET_REG_MEAS', kind='measurement_error' if error or flags else 'measurement',
                error_code=error, error_flags=flags, tds_percent=brix, brix_avg=avg,
                count=count, temperature_c=temperature, refraction=refraction,
                x_pos_glue=glue, x_pos_liquid=liquid, extra_hex=data[30:].hex(),
                inner_crc_verified=True)


def decode_frame(frame):
    cipher = unpack_frame(frame)
    plain = subprocess.run(['openssl', 'enc', '-d', '-aes-128-cbc', '-K', KEY,
                            '-iv', IV, '-nopad'], input=cipher,
                           capture_output=True, check=True).stdout
    return parse_measurement(plain)


if __name__ == '__main__':
    print(json.dumps(decode_frame(bytes.fromhex(sys.argv[1])), indent=2))
