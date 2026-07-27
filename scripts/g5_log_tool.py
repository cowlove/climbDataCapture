#!/usr/bin/env python3
"""List, download, or delete climbDataCapture raw G5 logs over USB serial."""

import argparse
import pathlib
import sys
import time

import serial


def open_serial(args):
    connection = serial.Serial(args.port, args.baud, timeout=args.timeout)
    connection.dtr = False
    # Opening common ESP32 serial adapters may reset the board. Allow setup(),
    # including the display and filesystem mount, to complete before sending.
    time.sleep(1.0)
    connection.reset_input_buffer()
    return connection


def send(connection, command):
    connection.write((command + "\n").encode("ascii"))
    connection.flush()


def read_until_prefix(connection, prefix, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = connection.readline()
        if not line:
            continue
        decoded = line.decode("utf-8", errors="replace").rstrip("\r\n")
        if decoded.startswith("LOG_ERROR\t"):
            raise RuntimeError(decoded.split("\t", 1)[1])
        if decoded.startswith(prefix):
            return decoded
    raise TimeoutError(f"timed out waiting for {prefix}")


def list_logs(args):
    with open_serial(args) as connection:
        send(connection, "LOG LIST")
        read_until_prefix(connection, "LOG_LIST_BEGIN", args.timeout)
        while True:
            line = connection.readline().decode("utf-8", errors="replace").rstrip("\r\n")
            if line == "LOG_LIST_END":
                return
            if line.startswith("LOG_FILE\t"):
                _, name, size = line.split("\t", 2)
                print(f"{name}\t{int(size):,} bytes")


def get_log(args):
    with open_serial(args) as connection:
        send(connection, f"LOG DUMP {args.filename}")
        marker = read_until_prefix(connection, "LOG_DUMP_BEGIN\t", args.timeout)
        _, returned_name, size_text = marker.split("\t", 2)
        size = int(size_text)
        data = connection.read(size)
        if len(data) != size:
            raise RuntimeError(f"received {len(data)} of {size} bytes")
        output = pathlib.Path(args.output)
        output.write_bytes(data)
        print(f"Downloaded {returned_name} to {output} ({size:,} bytes)")


def delete_log(args):
    with open_serial(args) as connection:
        send(connection, f"LOG DELETE {args.filename}")
        marker = read_until_prefix(connection, "LOG_DELETED\t", args.timeout)
        print(f"Deleted {marker.split(chr(9), 1)[1]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    commands = parser.add_subparsers(dest="command", required=True)

    list_parser = commands.add_parser("list", help="list stored log files")
    list_parser.set_defaults(function=list_logs)

    get_parser = commands.add_parser("get", help="download one log file")
    get_parser.add_argument("filename", help="device filename, e.g. /G5_001.TSV")
    get_parser.add_argument("output", help="local destination filename")
    get_parser.set_defaults(function=get_log)

    delete_parser = commands.add_parser("delete", help="delete one stored log")
    delete_parser.add_argument("filename", help="device filename, e.g. /G5_001.TSV")
    delete_parser.set_defaults(function=delete_log)

    args = parser.parse_args()
    try:
        args.function(args)
    except (OSError, serial.SerialException, RuntimeError, TimeoutError) as error:
        print(f"g5_log_tool: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
