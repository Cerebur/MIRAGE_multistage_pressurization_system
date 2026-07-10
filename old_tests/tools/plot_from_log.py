#!/usr/bin/env python3
"""
Plot data from CSV logs written by the device (in logs/ directory).
The script watches the latest CSV file and reloads it on each animation frame,
updating the plots. The CSV format expects a META line and a DATA header line,
followed by DATA rows like:

META,inlet_choked,yes
DATA,ts_ms,diaphragm_pct,piston_pct,mpx_pressure_hPa,bmp_temp_C,bmp_pressure_hPa
DATA,12345,10,20,1013.25,22.5,1012.8

Usage:
  python tools/plot_from_log.py --logsdir logs --interval 1000

Requires: matplotlib, numpy
  pip install matplotlib numpy
"""
import argparse
import csv
import glob
import os
import time

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation


def parse_args():
    p = argparse.ArgumentParser(description='Plot logs produced by firmware')
    p.add_argument('--logsdir', default='logs', help='Directory containing log files')
    p.add_argument('--file', default=None, help='Specific log file to plot (overrides logsdir)')
    p.add_argument('--interval', type=int, default=1000, help='Refresh interval ms')
    return p.parse_args()


class LogPlotter:
    def __init__(self, logsdir, filepath=None):
        self.logsdir = logsdir
        self.filepath = filepath
        self.meta = {}
        self.ts = []
        self.diaphragm = []
        self.piston = []
        self.mpx_pressure = []
        self.bmp_temp = []
        self.bmp_pressure = []

    def find_latest(self):
        if self.filepath:
            return self.filepath
        pattern = os.path.join(self.logsdir, '*')
        files = [f for f in glob.glob(pattern) if os.path.isfile(f)]
        if not files:
            raise FileNotFoundError('No log files found in ' + self.logsdir)
        latest = max(files, key=os.path.getmtime)
        return latest

    def read_file(self, path):
        self.meta = {}
        self.ts = []
        self.diaphragm = []
        self.piston = []
        self.mpx_pressure = []
        self.bmp_temp = []
        self.bmp_pressure = []
        import math
        try:
            with open(path, 'r') as f:
                reader = csv.reader(f)
                for parts in reader:
                    if not parts:
                        continue
                    if parts[0] == 'META':
                        if len(parts) >= 3:
                            self.meta[parts[1]] = parts[2]
                    elif parts[0] == 'DATA':
                        # handle both header and data rows
                        if parts[1] == 'ts_ms':
                            continue
                        # parse fields robustly; non-numeric -> NaN
                        try:
                            ts = int(parts[1]) / 1000.0
                        except Exception:
                            continue
                        def tofloat(x):
                            try:
                                return float(x)
                            except Exception:
                                return math.nan

                        self.ts.append(ts)
                        self.diaphragm.append(tofloat(parts[2]) if len(parts) > 2 else math.nan)
                        self.piston.append(tofloat(parts[3]) if len(parts) > 3 else math.nan)
                        self.mpx_pressure.append(tofloat(parts[4]) if len(parts) > 4 else math.nan)
                        if len(parts) >= 7:
                            self.bmp_temp.append(tofloat(parts[5]))
                            self.bmp_pressure.append(tofloat(parts[6]))
                        else:
                            self.bmp_temp.append(math.nan)
                            self.bmp_pressure.append(math.nan)
        except Exception as e:
            print('Error reading file', path, e)

    def run(self, interval_ms=1000):
        filepath = self.find_latest()
        print('Plotting file:', filepath)

        plt.style.use('default')
        fig, axs = plt.subplots(5, 1, sharex=True, figsize=(10, 12))
        (ax_dia, ax_pis, ax_mpx, ax_btmp, ax_bmp) = axs
        ax_dia.set_ylabel('Diaphragm (%)')
        ax_pis.set_ylabel('Piston (%)')
        ax_mpx.set_ylabel('MPX Pressure (hPa)')
        ax_btmp.set_ylabel('BMP Temp (°C)')
        ax_bmp.set_ylabel('BMP Pressure (hPa)')
        ax_bmp.set_xlabel('Time (s)')

        l_dia, = ax_dia.plot([], [])
        l_pis, = ax_pis.plot([], [])
        l_mpx, = ax_mpx.plot([], [])
        l_btmp, = ax_btmp.plot([], [])
        l_bmp, = ax_bmp.plot([], [])

        # Fix pump axes to 0..100%
        ax_dia.set_ylim(-1, 101)
        ax_pis.set_ylim(-1, 101)

        def update(frame):
            nonlocal filepath
            # if no specific file, always pick latest
            if not self.filepath:
                try:
                    filepath = self.find_latest()
                except FileNotFoundError:
                    return l_dia, l_pis, l_mpx, l_btmp, l_bmp
            self.read_file(filepath)
            if not self.ts:
                return l_dia, l_pis, l_mpx, l_btmp, l_bmp
            xs = [t - self.ts[0] for t in self.ts]
            l_dia.set_data(xs, self.diaphragm)
            l_pis.set_data(xs, self.piston)
            l_mpx.set_data(xs, self.mpx_pressure)
            l_btmp.set_data(xs, self.bmp_temp)
            l_bmp.set_data(xs, self.bmp_pressure)

            xmin, xmax = xs[0], xs[-1]
            for ax in axs:
                ax.set_xlim(xmin, xmax)

            # autoscale y for pressure/temp
            import math
            import numpy as np

            def safe_min_max(seq):
                arr = np.array(seq, dtype=float)
                if arr.size == 0:
                    return None, None
                finite = np.isfinite(arr)
                if not finite.any():
                    return None, None
                a = arr[finite]
                return float(np.min(a)), float(np.max(a))

            # autoscale MPX pressure y if finite data exists
            ymin, ymax = safe_min_max(self.mpx_pressure)
            if ymin is not None:
                if ymin == ymax:
                    ymin -= 0.5; ymax += 0.5
                margin = max(0.1 * abs(ymin), 0.1 * abs(ymax), 0.5)
                ax_mpx.set_ylim(ymin - margin, ymax + margin)

            # autoscale BMP pressure y if finite data exists
            ymin, ymax = safe_min_max(self.bmp_pressure)
            if ymin is not None:
                if ymin == ymax:
                    ymin -= 0.5; ymax += 0.5
                margin = max(0.1 * abs(ymin), 0.1 * abs(ymax), 0.5)
                ax_bmp.set_ylim(ymin - margin, ymax + margin)

            # autoscale BMP temp y if finite data exists
            ymin, ymax = safe_min_max(self.bmp_temp)
            if ymin is not None:
                if ymin == ymax:
                    ymin -= 0.5; ymax += 0.5
                margin = max(0.1 * abs(ymin), 0.1 * abs(ymax), 0.5)
                ax_btmp.set_ylim(ymin - margin, ymax + margin)

            title_meta = ''
            if 'inlet_choked' in self.meta:
                title_meta = f"Inlet choked: {self.meta['inlet_choked']}"
            fig.suptitle(title_meta)
            return l_dia, l_pis, l_mpx, l_btmp, l_bmp

        ani = FuncAnimation(fig, update, interval=interval_ms, blit=False)
        plt.tight_layout()
        plt.show()


if __name__ == '__main__':
    args = parse_args()
    p = LogPlotter(args.logsdir, args.file)
    p.run(args.interval)
