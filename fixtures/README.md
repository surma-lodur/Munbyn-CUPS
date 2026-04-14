# Test Fixtures

This directory contains test raster files and utilities for testing the Munbyn CUPS printer driver.

## Overview

- `gen_raster.py` — Python script to generate CUPS raster files with various patterns
- `Makefile` — Automates fixture generation
- `*.raster` — Generated CUPS raster files (created by running `make`)

## Generating Fixtures

### Quick Start

Generate all standard fixtures:
```sh
cd fixtures
make
```

This creates:
- `blank.raster` — All white (empty page)
- `solid.raster` — All black (solid fill)
- `gradient.raster` — Horizontal gradient from black (left) to white (right)
- `checkerboard.raster` — 8×8 checkerboard pattern
- `stripes.raster` — Vertical stripes (8px wide), alternating black/white
- `lines.raster` — Horizontal lines (8px tall), alternating black/white

### Custom Generation

Generate a custom raster file:
```sh
python3 gen_raster.py output.raster [pattern] [width] [height] [num_pages]
```

**Patterns:**
- `blank` — All 0 bits (white)
- `solid` — All 1 bits (black)
- `gradient` — Horizontal gradient
- `checkerboard` — 8×8 checkerboard
- `stripes` — Vertical stripes
- `lines` — Horizontal lines

**Examples:**
```sh
# Small gradient test (150×150px)
python3 gen_raster.py small_gradient.raster gradient 150 150

# Multi-page raster (3 pages)
python3 gen_raster.py multi.raster blank 576 864 3

# Custom size
python3 gen_raster.py custom.raster checkerboard 400 600
```

## Using Fixtures in Tests

### From Command Line

Test with a fixture:
```sh
../rastertorw403b 1 user title 1 "PageSize=w576h864" blank.raster --log /tmp/test.log
```

### From Test Code

Generate fixture in test and process:
```c
// In test code, generate and test
system("python3 gen_raster.py test.raster gradient 576 864");
// Then use test.raster for input
```

## Fixture Specs

All default fixtures use:
- **Resolution:** 203 DPI (standard for thermal printers)
- **Page Size:** 576×864 pixels (2.8" × 4.25")
- **Color Space:** Grayscale (8 bits per pixel)
- **Compression:** None

This matches the Munbyn RW403B printer specifications.

## Cleanup

Remove all generated fixtures:
```sh
make clean
```

## Notes

- Generated files are portable CUPS raster format (v1 stream, `RaSt`)
- All fixtures start with magic number `"RaSt"`
- Version field is 0 (standard CUPS raster v1)
- Files are valid input for any CUPS filter or raster processor
