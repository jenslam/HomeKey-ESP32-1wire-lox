#!/bin/bash
# flash.sh — Build, flash and monitor HomeKey-Loxone firmware on macOS
# Usage: ./flash.sh [port]
# If port is omitted, auto-detects first available USB-serial device.

set -e

if [ -n "$1" ]; then
    PORT="$1"
else
    PORT=$(ls /dev/tty.usbserial-* /dev/tty.SLAB_USBtoUART /dev/tty.usbmodem* 2>/dev/null | head -1)
    if [ -z "$PORT" ]; then
        echo "ERROR: No USB-serial device found."
        echo "       Connect the ESP32 via USB and retry, or run: ./flash.sh /dev/tty.YOURPORT"
        echo ""
        echo "To list all serial devices: ls /dev/tty.*"
        exit 1
    fi
    echo "Auto-detected port: $PORT"
fi

echo "Building firmware..."
idf.py build

echo "Flashing to $PORT..."
idf.py -p "$PORT" flash

echo "Starting serial monitor (Ctrl+] to exit)..."
idf.py -p "$PORT" monitor
