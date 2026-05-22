#!/usr/bin/env python3
"""
Serial plotter and logger for pressurisation_prototype.
Reads CSV lines from the Arduino over serial (lines starting with "DATA,")
and plots each metric in a separate live-updating subplot while also
logging all received data to a CSV file.

Usage:
  python tools/serial_plotter.py --port /dev/tty.usbmodemXXXX --baud 115200

Requires: pyserial, matplotlib
  pip install pyserial matplotlib
"""
import argparse
import csv
import os
import sys
import time
from collections import deque

import serial
import serial.tools.list_ports
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation


def parse_args():
    p = argparse.ArgumentParser(description="Serial plotter/logger for Arduino DATA output")
    p.add_argument("--port", default="auto", help="Serial port (e.g. /dev/tty.usbmodemXXXX or COM3). Use 'auto' to pick the first available port")
    p.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    p.add_argument("--logdir", default="logs", help="Directory to save CSV logs (default: logs)")
    p.add_argument("--maxpoints", type=int, default=1000, help="Maximum points to keep in-memory for plotting")
    p.add_argument("--debug", action='store_true', help="Print raw serial lines for debugging")
    return p.parse_args()


class SerialLoggerPlotter:
    def __init__(self, port, baud, logdir, maxpoints=1000):
        self.port = port
        self.baud = baud
        self.logdir = logdir
        self.maxpoints = maxpoints

        os.makedirs(self.logdir, exist_ok=True)
        timestr = time.strftime("%Y%m%d_%H%M%S")
        self.logpath = os.path.join(self.logdir, f"data_{timestr}.csv")
        self.csvfile = open(self.logpath, "w", newline='')
        self.csvwriter = csv.writer(self.csvfile)
        self.header_written = False
        self.debug = False

        # store data in deques
        self.ts = deque(maxlen=maxpoints)
        self.diaphragm = deque(maxlen=maxpoints)
        self.piston = deque(maxlen=maxpoints)
        self.pressure = deque(maxlen=maxpoints)
        self.bmp_temp = deque(maxlen=maxpoints)
        self.bmp_pressure = deque(maxlen=maxpoints)

        # try to open serial. support auto-detection
        def list_available_ports():
            ports = list(serial.tools.list_ports.comports())
            return ports

        if self.port == 'auto':
            ports = list_available_ports()
            if not ports:
                raise RuntimeError('No serial ports detected. Connect the device and try again.')
            # prefer likely Arduino-like ports
            preferred = None
            for p in ports:
                name = p.device.lower()
                if 'usbmodem' in name or 'usbserial' in name or 'acm' in name or 'usb' in name:
                    preferred = p
                    break
            chosen = (preferred or ports[0]).device
            print('Available ports:')
            for p in ports:
                print('  ', p.device, '-', getattr(p, 'description', ''))
            print('Chosen port:', chosen)
            try:
                self.ser = serial.Serial(chosen, self.baud, timeout=1)
                print(f"Auto-opened serial port {chosen} @ {self.baud}")
                self.port = chosen
            except Exception as e:
                print('Failed to open detected serial port:', chosen, e)
                raise
        else:
            try:
                self.ser = serial.Serial(self.port, self.baud, timeout=1)
                print(f"Opened serial port {self.port} @ {self.baud}")
            except Exception as e:
                print("Failed to open serial port:", e)
                ports = list_available_ports()
                if ports:
                    print('Available ports:')
                    for p in ports:
                        print('  ', p.device)
                else:
                    print('No serial ports detected on the system.')
                raise

    def close(self):
        try:
            self.csvfile.close()
        except Exception:
            pass
        try:
            self.ser.close()
        except Exception:
            pass

    def read_line(self):
        try:
            raw = self.ser.readline()
            if not raw:
                return None
            line = raw.decode(errors='ignore').strip()
            if getattr(self, 'debug', False):
                print(f"RAW: {line}")
            return line
        except Exception as e:
            # Attempt to reconnect once every 2 seconds
            print("Serial read error:", e)
            now = time.time()
            last = getattr(self, '_last_reconnect', 0)
            if now - last > 2:
                self._last_reconnect = now
                try:
                    print('Attempting to reopen serial port', self.port)
                    try:
                        self.ser.close()
                    except Exception:
                        pass
                    self.ser = serial.Serial(self.port, self.baud, timeout=1)
                    print('Reopened serial port', self.port)
                except Exception as e2:
                    print('Reopen failed:', e2)
            return None

    def process_line(self, line):
        # optional quick debug count
        if getattr(self, 'debug', False) and line.startswith('DATA,'):
            try:
                self._data_count += 1
            except AttributeError:
                self._data_count = 1
            if self._data_count % 10 == 0:
                print(f"Processed DATA lines: {self._data_count}")
        # Expect lines like: DATA,ts_ms,diaphragm_pct,piston_pct,pressure_hPa,bmp_temp_C,bmp_pressure_hPa
        if not line.startswith("DATA,"):
            return
        parts = line.split(',')
        # accept either old 5-column format or new 7-column format
        if len(parts) < 5:
            return
        try:
            ts_ms = int(parts[1])
            dia = float(parts[2])
            pis = float(parts[3])
            mpx_pres = float(parts[4])
            if len(parts) >= 7:
                bmp_temp = float(parts[5])
                bmp_pres = float(parts[6])
            else:
                bmp_temp = 0.0
                bmp_pres = 0.0
        except ValueError:
            return

        # write header if needed
        if not self.header_written:
            self.csvwriter.writerow(["ts_ms", "diaphragm_pct", "piston_pct", "mpx_pressure_hPa", "bmp_temp_C", "bmp_pressure_hPa"])
            self.header_written = True
        # write row
        self.csvwriter.writerow([ts_ms, dia, pis, mpx_pres, bmp_temp, bmp_pres])
        self.csvfile.flush()

        # append to deques
        self.ts.append((ts_ms - self.ts[0]) / 1000.0 if self.ts else 0.0)
        self.diaphragm.append(dia)
        self.piston.append(pis)
        self.pressure.append(mpx_pres)
        self.bmp_temp.append(bmp_temp)
        self.bmp_pressure.append(bmp_pres)

    def run(self):
        # Setup matplotlib figure with 3 subplots
        try:
            plt.style.use('seaborn-darkgrid')
        except Exception:
            try:
                plt.style.use('seaborn')
            except Exception:
                plt.style.use('default')
        # create five subplots: diaphragm, piston, MPX pressure, BMP temp, BMP pressure
        fig, axs = plt.subplots(5, 1, sharex=True, figsize=(10, 12))
        (ax_dia, ax_pis, ax_mpx_pres, ax_bmp_temp, ax_bmp_pres) = axs

        ax_dia.set_ylabel('Diaphragm (%)')
        ax_pis.set_ylabel('Piston (%)')
        ax_mpx_pres.set_ylabel('MPX Pressure (hPa)')
        ax_bmp_temp.set_ylabel('BMP Temp (°C)')
        ax_bmp_pres.set_ylabel('BMP Pressure (hPa)')
        ax_bmp_pres.set_xlabel('Time (s)')

        line_dia, = ax_dia.plot([], [], label='Diaphragm')
        line_pis, = ax_pis.plot([], [], label='Piston')
        line_mpx_pres, = ax_mpx_pres.plot([], [], label='MPX Pressure')
        line_bmp_temp, = ax_bmp_temp.plot([], [], label='BMP Temp')
        line_bmp_pres, = ax_bmp_pres.plot([], [], label='BMP Pressure')

        ax_dia.set_ylim(-5, 105)
        ax_pis.set_ylim(-5, 105)

        def init():
            line_dia.set_data([], [])
            line_pis.set_data([], [])
            line_mpx_pres.set_data([], [])
            line_bmp_temp.set_data([], [])
            line_bmp_pres.set_data([], [])
            return line_dia, line_pis, line_mpx_pres, line_bmp_temp, line_bmp_pres

        def update(frame):
            # read available lines
            for _ in range(50):
                line = self.read_line()
                if not line:
                    break
                self.process_line(line)

            if len(self.ts) == 0:
                return line_dia, line_pis, line_mpx_pres, line_bmp_temp, line_bmp_pres

            xs = list(self.ts)
            line_dia.set_data(xs, list(self.diaphragm))
            line_pis.set_data(xs, list(self.piston))
            line_mpx_pres.set_data(xs, list(self.pressure))
            line_bmp_temp.set_data(xs, list(self.bmp_temp))
            line_bmp_pres.set_data(xs, list(self.bmp_pressure))

            # adjust xlimits
            xmin, xmax = xs[0], xs[-1]
            for ax in (ax_dia, ax_pis, ax_mpx_pres, ax_bmp_temp, ax_bmp_pres):
                ax.set_xlim(xmin, xmax)

            # autoscale MPX pressure y
            if len(self.pressure) > 0:
                ymin = min(self.pressure)
                ymax = max(self.pressure)
                if ymin == ymax:
                    ymin -= 0.5
                    ymax += 0.5
                ax_mpx_pres.set_ylim(ymin - 0.1 * abs(ymin), ymax + 0.1 * abs(ymax))

            # autoscale BMP pressure y
            if len(self.bmp_pressure) > 0:
                ymin = min(self.bmp_pressure)
                ymax = max(self.bmp_pressure)
                if ymin == ymax:
                    ymin -= 0.5
                    ymax += 0.5
                ax_bmp_pres.set_ylim(ymin - 0.1 * abs(ymin), ymax + 0.1 * abs(ymax))

            # autoscale BMP temp y
            if len(self.bmp_temp) > 0:
                ymin = min(self.bmp_temp)
                ymax = max(self.bmp_temp)
                if ymin == ymax:
                    ymin -= 0.5
                    ymax += 0.5
                ax_bmp_temp.set_ylim(ymin - 0.1 * abs(ymin), ymax + 0.1 * abs(ymax))

            return line_dia, line_pis, line_mpx_pres, line_bmp_temp, line_bmp_pres

        ani = FuncAnimation(fig, update, init_func=init, interval=200, blit=False)

        try:
            plt.tight_layout()
            plt.show()
        except KeyboardInterrupt:
            print('Interrupted')
        finally:
            self.close()


if __name__ == '__main__':
    args = parse_args()
    sp = SerialLoggerPlotter(args.port, args.baud, args.logdir, args.maxpoints)
    sp.debug = args.debug
    try:
        sp.run()
    except Exception as e:
        print('Error:', e)
        sp.close()
        sys.exit(1)
