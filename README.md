# Munbyn CUPS Driver for Linux

A native Linux CUPS driver for Munbyn label printers, including the RW403B and related models. Provides a CUPS raster filter (`rastertorw403b`) and a PPD file so the printer works out of the box with standard Linux printing tools.

## Supported printers

- Munbyn RW403B

## Requirements

- CUPS (version 1.2 or later)
- GCC and GNU make
- `libcups2-dev` (CUPS development headers)

## Installation

### 1. Install build dependencies

Debian / Ubuntu:

```sh
sudo apt update
sudo apt install build-essential pkg-config libcups2-dev
```

Fedora / RHEL:

```sh
sudo dnf install gcc make cups-devel
```

### 2. Build

```sh
make
```

The compiled filter is written to `build/rastertorw403b`.

### 3. Install the filter and PPD

```sh
sudo make install
```

This copies the filter to `/usr/lib/cups/filter/rastertorw403b` and the PPD to `/usr/share/cups/model/Munbyn/RW403B.ppd`.

### 4. Add the printer in CUPS

Open the CUPS web interface at [http://localhost:631](http://localhost:631), go to **Administration → Add Printer**, select the Munbyn RW403B, and choose the **RW403B** PPD from the Munbyn category.

Alternatively, use `lpadmin`:

```sh
sudo lpadmin -p RW403B -E -v usb://Munbyn/RW403B -m Munbyn/RW403B.ppd
```

## Usage

Once the printer is added, print as normal with any application or use `lp` from the command line:

```sh
lp -d RW403B -o media=w216h144 file.pdf
```

The `media` option accepts the label sizes defined in the PPD (e.g. `w216h144` for 3×2 inch labels). Custom sizes are also supported within the printer's range.

## Building without installing

To build and test the filter locally without touching system directories:

```sh
make
./build/rastertorw403b
```

## Running tests

```sh
make test
```

## Project layout

| Path | Description |
|---|---|
| `linux/rastertorw403b.c` | CUPS raster filter source |
| `ppd/RW403B.ppd` | PPD for the RW403B (taken from the official macOS driver available at munbyn.com) |
| `test/` | Unit tests |
| `fixtures/` | Sample raster files for testing |

