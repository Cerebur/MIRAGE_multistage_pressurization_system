# Pressurisation Prototype

This repository contains two related Arduino/PlatformIO test firmware trees for the pressurisation prototype, plus a small set of host-side logging and plotting tools.

The code is now split into:

- `old_tests/` for the earlier test firmware
- `new_tests/` for the newer test firmware that uses flight hardware sensors and adds programmable automatic modes

Both directories follow the same general layout and are used in the same way. The main differences are the hardware stack and the control model:

- `old_tests/` focuses on manual and cyclic pump control with the earlier sensor setup
- `new_tests/` integrates the flight hardware sensor stack and adds `programme` / `auto` modes for scripted automatic operation

## Repository Layout

- `old_tests/src/main.cpp` - legacy test firmware entry point
- `old_tests/src/MPX5500/` - legacy pressure sensor helper code
- `old_tests/logs/` - older serial log output
- `old_tests/tools/` - legacy host-side logging and plotting scripts
- `new_tests/src/main.cpp` - current test firmware entry point
- `new_tests/src/sensor_readout/` - sensor abstraction used by the new tests
- `new_tests/src/programmes/` - programmable automatic mode definitions and framework
- `new_tests/logs/` - example serial log output
- `new_tests/tools/` - host-side logging and plotting scripts

## New Versus Old Tests

The older test firmware is the simpler baseline. It keeps the dual-pump control workflow and serial interaction, and it uses the earlier sensor mix centered on the MPX5500 pressure sensor and BMP280, with optional DHT11 support.

The newer test firmware keeps the same overall pump-control structure but extends it in two important ways:

- It reads the flight hardware sensor stack through `sensor_readout/` using sensors such as TMP1075, ABP2, MS5803, SHT45, and BMP280
- It supports programmable automatic sequences through `programmes/`, including a selectable automatic mode

## Common Hardware

Both test trees target an Arduino Nano using the `nanoatmega328new` PlatformIO board definition and drive the pumps with 20 kHz PWM:

- G4-4 diaphragm pump on D9
- W6 piston pump MOSFET gate on D10
- Solenoid valve on a dedicated digital output that differs between the old and new test builds

The exact sensor set depends on which test tree you open. The newer tests use the flight hardware sensors, while the older tests use the earlier pressure-sensor-oriented setup.

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

## Working With The Repository

Each test directory is a separate PlatformIO project. Open `old_tests/` or `new_tests/` directly in Visual Studio Code, depending on which firmware variant you want to build.

The shared PlatformIO configuration in each directory targets:

- board: `nanoatmega328new`
- framework: `arduino`
- serial monitor baud: `115200`

## Building And Uploading

From Visual Studio Code:

1. Open the desired test folder.
2. Wait for PlatformIO to initialize the project.
3. Use `Build` to compile the firmware.
4. Connect the Arduino Nano by USB.
5. Use `Upload` to flash the firmware to the board.
6. Use `Monitor` to open the serial console at `115200` baud.

From the PlatformIO terminal inside either test folder:

```bash
pio run
pio run --target upload
pio device monitor --baud 115200
```

## Serial Usage

Both test trees print a help message on boot and then emit machine-readable lines for logging. The newer firmware adds more serial commands for automatic mode control, while the older firmware keeps the simpler manual/cyclic command set.

The common patterns are:

- `<n>` sets both pumps to `<n>%`
- `d<n>` sets only the diaphragm pump
- `p<n>` sets only the piston pump
- `s` or `status` prints the current status
- `h` or `help` prints the command list

The newer firmware also accepts automatic/programme commands such as `auto`, `programme`, `program`, and `manual`.

## Host-Side Logging And Plotting

The `tools/` directory in each test tree contains scripts for logging and plotting serial data.

To capture data and plot it live, do not use the PlatformIO serial monitor. It occupies the serial port and is not the right workflow for live plotting. Instead, start the logger first, wait a few seconds so it can create the first log file entry, and then start the plotter.

Example workflow:

```bash
python tools/serial_logger.py --port auto --baud 115200 --inlet yes
```

After a short delay, in a second terminal:

```bash
python tools/plot_from_log.py --logsdir logs --interval 1000
```

`serial_logger.py` writes CSV files into `logs/`, and `plot_from_log.py` reads the newest log file and refreshes the plot every second. If you want to plot a specific file, pass `--file logs/data_<timestamp>.csv` instead of `--logsdir`.
