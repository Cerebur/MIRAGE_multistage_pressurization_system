#!/usr/bin/env python3
"""
Serial logger that connects to the device, responds to the inlet-choked prompt,
and saves META and DATA lines to a CSV file under the local `logs/` directory.
This avoids using PlatformIO's serial monitor so the host can record data.

Usage:
  .venv/bin/python tools/serial_logger.py --port auto --baud 115200 --inlet yes

Options:
  --port   Serial port or 'auto' (default: auto)
  --baud   Baud rate (default: 115200)
  --inlet  inlet choked value to send when device prompts (yes/no/unknown)

The script creates `logs/data_<timestamp>.csv` and writes any incoming
'META,...' and 'DATA,...' lines verbatim to it.
"""
import argparse
import os
import sys
import time
from datetime import datetime
import serial
import serial.tools.list_ports
import threading
import sys


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--port', default='auto')
    p.add_argument('--baud', type=int, default=115200)
    p.add_argument('--inlet', default='unknown', help='Value to send when prompted (yes/no)')
    p.add_argument('--logsdir', default='logs')
    return p.parse_args()


def list_ports():
    return list(serial.tools.list_ports.comports())


def choose_port(prefer='auto'):
    ports = list_ports()
    if not ports:
        raise RuntimeError('No serial ports detected')
    if prefer != 'auto':
        return prefer
    # prefer USB-serial types
    for p in ports:
        name = p.device.lower()
        if 'usbmodem' in name or 'usbserial' in name or 'acm' in name or 'usb' in name:
            return p.device
    return ports[0].device


def main():
    args = parse_args()
    os.makedirs(args.logsdir, exist_ok=True)
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    outfile = os.path.join(args.logsdir, f'data_{timestamp}.csv')

    port = choose_port(args.port)
    print('Using port:', port)

    ser = serial.Serial(port, args.baud, timeout=1)
    print('Opened serial')

    stop_event = threading.Event()

    def stdin_forward():
        # Read user input from stdin and forward to serial so user can send commands
        try:
            while not stop_event.is_set():
                line = sys.stdin.readline()
                if not line:
                    # EOF
                    break
                ser.write(line.encode())
        except Exception:
            pass

    stdin_thread = threading.Thread(target=stdin_forward, daemon=True)
    stdin_thread.start()

    with open(outfile, 'w', newline='') as f:
        print('Logging to', outfile)
        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode(errors='ignore').rstrip('\r\n')
                if not line:
                    continue
                print(line)

                # if device prompts for inlet choked, respond automatically
                if ('Is the inlet choked' in line) or line.lower().startswith('is the inlet choked'):
                    val = args.inlet[0].lower()
                    if val == 'y':
                        tosend = 'y\n'
                    elif val == 'n':
                        tosend = 'n\n'
                    else:
                        tosend = (args.inlet + '\n')
                    print('Sending inlet response:', tosend.strip())
                    ser.write(tosend.encode())
                    continue

                # write META/DATA lines only
                if line.startswith('META,') or line.startswith('DATA,'):
                    f.write(line + '\n')
                    f.flush()
        except KeyboardInterrupt:
            print('Stopping')
        finally:
            stop_event.set()
            try:
                ser.close()
            except Exception:
                pass


if __name__ == '__main__':
    main()
