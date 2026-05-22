# Pressurisation Prototype

Arduino-based dual-pump pressurisation controller built for use in Visual Studio Code with the PlatformIO extension.

The firmware runs on an Arduino Nano using the `nanoatmega328new` PlatformIO target and controls two pumps with 20 kHz PWM:

- G4-4 diaphragm pump on pin D9
- W6 piston pump MOSFET gate on pin D10

It also reads an MPX5500 pressure sensor on A3 and optionally a BMP280 over I2C.

## What This Repository Contains

- `src/main.cpp` - firmware entry point
- `src/MPX5500/` - MPX5500 pressure sensor helper
- `tools/` - host-side logging and plotting scripts
- `logs/` - example CSV output from serial logging

## Required Hardware

Minimum hardware required to run the firmware:

- Arduino Nano with ATmega328P using the new bootloader
  - PlatformIO board target: `nanoatmega328new`
- G4-4 diaphragm pump
  - PWM control signal connected to D9
- W6 piston pump
  - MOSFET gate control connected to D10
- MPX5500 pressure sensor
  - Analog input connected to A3

Optional hardware:

- BMP280 temperature/pressure sensor on I2C
  - SDA on A4
  - SCL on A5
  - Address `0x76` or `0x77`

## Software Requirements

Install the following on the development computer:

- Visual Studio Code
- PlatformIO extension for VS Code
- Git
- Python 3.10 or newer if you want to use the host-side logging or plotting scripts

Python packages needed by the scripts in `tools/`:

- `pyserial`
- `matplotlib` for plotting

## Copying The Repository Locally

### Option 1: Clone from GitHub

Replace the URL below with the actual GitHub repository URL:

```bash
git clone https://github.com/<your-account>/pressurisation_prototype.git
cd pressurisation_prototype
```

### Option 2: Download the ZIP

If you do not want to use Git:

1. Open the repository on GitHub.
2. Click `Code`.
3. Choose `Download ZIP`.
4. Extract the ZIP file to a local folder.
5. Open that folder in Visual Studio Code.

## Installing The Software

### 1. Install Git

- Windows: install Git from `https://git-scm.com/downloads`
- macOS: install Xcode Command Line Tools or use Homebrew
- Linux: install from your package manager

Verify the installation:

```bash
git --version
```

### 2. Install Visual Studio Code

Download and install Visual Studio Code from:

```text
https://code.visualstudio.com/
```

### 3. Install PlatformIO

In Visual Studio Code:

1. Open the Extensions view.
2. Search for `PlatformIO IDE`.
3. Install the PlatformIO extension.
4. Restart VS Code if prompted.

PlatformIO will create its own toolchain and download the required compiler and libraries when the project is opened or built for the first time.

### 4. Install Python 3 for the helper tools

If you want to use the scripts in `tools/`, install Python 3 and then install the packages:

```bash
python -m pip install pyserial matplotlib
```

If your system uses `python3` instead of `python`, use:

```bash
python3 -m pip install pyserial matplotlib
```

## Opening The Project In VS Code

1. Start Visual Studio Code.
2. Select `File -> Open Folder...`
3. Open the root folder of this repository.
4. Wait for PlatformIO to finish initializing the project.

The project is configured in [`platformio.ini`](platformio.ini) for:

- board: `nanoatmega328new`
- framework: `arduino`
- serial monitor baud: `115200`

## Building And Uploading

### From Visual Studio Code

1. Open the PlatformIO sidebar.
2. Use `Build` to compile the firmware.
3. Connect the Arduino Nano by USB.
4. Use `Upload` to flash the firmware to the board.
5. Use `Monitor` to open the serial console at `115200` baud.

### From the PlatformIO terminal

You can also use the command line from inside the project directory:

```bash
pio run
pio run --target upload
pio device monitor --baud 115200
```

## Serial Usage

When the board boots, it prints a short help message and asks whether the inlet is choked. After that it emits machine-readable lines:

- `META,...` for metadata
- `DATA,...` for periodic sensor samples

You can also send commands over serial:

- `<n>` sets both pumps to `<n>%`
- `d<n>` sets only the diaphragm pump
- `p<n>` sets only the piston pump
- `s` or `status` prints the current status
- `h` or `help` prints the command list

## Host-Side Logging And Plotting

The `tools/` directory contains scripts for logging and plotting serial data.

Example logging command:

```bash
python tools/serial_logger.py --port auto --baud 115200 --inlet yes
```

Example live plot command:

```bash
python tools/serial_plotter.py --port auto --baud 115200
```

Both scripts write CSV files into `logs/`.

## Wiring Summary

- D9 - G4-4 diaphragm pump PWM control
- D10 - W6 piston pump MOSFET gate drive
- A3 - MPX5500 analog pressure sensor
- A4 - BMP280 SDA, if installed
- A5 - BMP280 SCL, if installed

## Notes

- The firmware uses Timer1 PWM on D9 and D10, so those pins are reserved for pump control.
- The BMP280 is optional. If it is not present, the firmware continues running and prints a warning.
- The code assumes a 5 V Arduino environment unless you adapt the hardware and calibration constants.
