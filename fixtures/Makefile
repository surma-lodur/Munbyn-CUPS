# Fixture generation makefile
# Generates various test raster files for testing

RASTER_GENERATOR := gen_raster.py
PYTHON3 := python3

# Default fixtures to generate
FIXTURES := blank.raster solid.raster gradient.raster checkerboard.raster stripes.raster lines.raster

# Standard page size: 576x864 pixels (2.8" x 4.25" @ 203 DPI)
WIDTH := 576
HEIGHT := 864

all: $(FIXTURES)

blank.raster:
	$(PYTHON3) $(RASTER_GENERATOR) $@ blank $(WIDTH) $(HEIGHT)

solid.raster:
	$(PYTHON3) $(RASTER_GENERATOR) $@ solid $(WIDTH) $(HEIGHT)

gradient.raster:
	$(PYTHON3) $(RASTER_GENERATOR) $@ gradient $(WIDTH) $(HEIGHT)

checkerboard.raster:
	$(PYTHON3) $(RASTER_GENERATOR) $@ checkerboard $(WIDTH) $(HEIGHT)

stripes.raster:
	$(PYTHON3) $(RASTER_GENERATOR) $@ stripes $(WIDTH) $(HEIGHT)

lines.raster:
	$(PYTHON3) $(RASTER_GENERATOR) $@ lines $(WIDTH) $(HEIGHT)

# Multi-page test file
multi.raster:
	$(PYTHON3) $(RASTER_GENERATOR) $@ blank $(WIDTH) $(HEIGHT) 3

clean:
	rm -f *.raster

.PHONY: all clean
