#!/usr/bin/env python3
"""
Plot data from CSV logs written by the device (in logs/ directory).
The script watches the latest CSV file, parses the header dynamically,
and plots all variables on independent, synchronized axes.

Usage:
  python tools/plot_from_log.py --logsdir logs --interval 1000
"""
import argparse
import csv
import glob
import os
import math
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation


def parse_args():
    p = argparse.ArgumentParser(description='Plot logs dynamically based on CSV headers')
    p.add_argument('--logsdir', default='logs', help='Directory containing log files')
    p.add_argument('--file', default=None, help='Specific log file to plot (overrides logsdir)')
    p.add_argument('--interval', type=int, default=1000, help='Refresh interval ms')
    return p.parse_args()


class LogPlotter:
    def __init__(self, logsdir, filepath=None):
        self.logsdir = logsdir
        self.filepath = filepath
        self.meta = {}
        self.headers = []  # Stores variable names dynamically
        self.ts = []       # Keeps absolute timestamps
        self.data_tracks = {}  # Dict mapping variable_name -> list of floats

    def find_latest(self):
        if self.filepath:
            return self.filepath
        pattern = os.path.join(self.logsdir, '*')
        files = [f for f in glob.glob(pattern) if os.path.isfile(f)]
        if not files:
            raise FileNotFoundError('No log files found in ' + self.logsdir)
        return max(files, key=os.path.getmtime)

    def read_file(self, path):
        self.meta = {}
        new_headers = []
        raw_rows = []

        try:
            with open(path, 'r') as f:
                reader = csv.reader(f)
                for parts in reader:
                    if not parts or len(parts) < 2:
                        continue
                    if parts[0] == 'META':
                        # Match loose metadata formats e.g. META,val or META,key,val
                        if len(parts) == 2:
                            self.meta['Test Data'] = parts[1]
                        elif len(parts) >= 3:
                            self.meta[parts[1]] = parts[2]
                    elif parts[0] == 'DATA':
                        if parts[1] == 'ts_ms':
                            # Dynamically capture variables excluding metadata tags/timestamps
                            new_headers = [h.strip() for h in parts[2:]]
                        else:
                            raw_rows.append(parts)
            
            if not new_headers:
                return

            # If headers changed or initialized, re-allocate empty track lists
            if new_headers != self.headers:
                self.headers = new_headers
                self.data_tracks = {h: [] for h in self.headers}
                self.ts = []
            else:
                # Clear lists for complete file reread reloads
                self.ts = []
                for h in self.headers:
                    self.data_tracks[h] = []

            # Process data rows out of the file
            for parts in raw_rows:
                try:
                    ts = int(parts[1]) / 1000.0
                except ValueError:
                    continue
                
                self.ts.append(ts)
                for idx, h in enumerate(self.headers):
                    val_idx = idx + 2
                    if val_idx < len(parts):
                        try:
                            self.data_tracks[h].append(float(parts[val_idx]))
                        except ValueError:
                            self.data_tracks[h].append(math.nan)
                    else:
                        self.data_tracks[h].append(math.nan)

        except Exception as e:
            print('Error reading file:', path, e)

    def run(self, interval_ms=1000):
        filepath = self.find_latest()
        print('Parsing initial layout from file:', filepath)
        
        self.read_file(filepath)
        if not self.headers:
            print("Error: Could not locate a valid 'DATA,ts_ms,...' header row.")
            return

        plt.style.use('default')
        num_plots = len(self.headers)
        
        # 1. Force the layout configuration directly on creation
        fig, axs = plt.subplots(num_plots, 1, sharex=True, figsize=(10, 1.6 * num_plots + 1.2))
        if num_plots == 1:
            axs = [axs]

        lines = {}
        for ax, h in zip(axs, self.headers):
            # 2. Assign targeted explicit font sizes directly to labels and tick lines
            ax.tick_params(axis='both', which='major', labelsize=5)
            
            line, = ax.plot([], [], label=h)
            lines[h] = line
            ax.legend(loc='upper right', fontsize=6, frameon=False)
            
            if 'pct' in h or 'percentage' in h:
                ax.set_ylim(-5, 105)

        # 3. Explicit label formatting for the final shared x-axis
        axs[-1].set_xlabel('Relative Time (s)', fontsize=6)

        def safe_min_max(seq):
            arr = np.array(seq, dtype=float)
            if arr.size == 0:
                return None, None
            finite = np.isfinite(arr)
            if not finite.any():
                return None, None
            a = arr[finite]
            return float(np.min(a)), float(np.max(a))

        def update(frame):
            nonlocal filepath, axs, fig, lines, num_plots
            if not self.filepath:
                try:
                    current_latest = self.find_latest()
                    if current_latest != filepath:
                        filepath = current_latest
                        print('Switched tracking to newer file:', filepath)
                except FileNotFoundError:
                    return list(lines.values())

            self.read_file(filepath)
            if not self.ts:
                return list(lines.values())

            # Check if an updated runtime file structure altered field numbers
            if len(self.headers) != num_plots:
                print("CSV format layout changed dynamically! Please restart script to redraw subplots.")
                return list(lines.values())

            # Convert timestamps relative to the run's start
            xs = [t - self.ts[0] for t in self.ts]
            
            # Map values out to our axis lines
            for h in self.headers:
                lines[h].set_data(xs, self.data_tracks[h])

            # Apply shared timeline bounds
            xmin, xmax = xs[0], xs[-1]
            if xmin == xmax:
                xmax += 1.0
            axs[-1].set_xlim(xmin, xmax)

            # Auto-scale the signals safely
            for ax, h in zip(axs, self.headers):
                if 'pct' in h or 'percentage' in h:
                    continue  # Keep static bound mapping for percentages
                ymin, ymax = safe_min_max(self.data_tracks[h])
                if ymin is not None:
                    if ymin == ymax:
                        ymin -= 0.5
                        ymax += 0.5
                    margin = max(0.05 * abs(ymin), 0.05 * abs(ymax), 0.1)
                    ax.set_ylim(ymin - margin, ymax + margin)

            # Format window title metadata block 
            meta_str = ", ".join([f"{k}: {v}" for k, v in self.meta.items()])
            fig.suptitle(meta_str if meta_str else os.path.basename(filepath))
            return list(lines.values())

        ani = FuncAnimation(fig, update, interval=interval_ms, blit=False)
        plt.tight_layout()
        plt.show()


if __name__ == '__main__':
    args = parse_args()
    p = LogPlotter(args.logsdir, args.file)
    p.run(args.interval)