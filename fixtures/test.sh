#!/bin/bash
# Test helper script - runs driver with fixtures and captures output

FIXTURES_DIR="$(dirname "${BASH_SOURCE[0]}")"
DRIVER="${FIXTURES_DIR}/../rastertorw403b"
PATTERN="${1:-blank}"
OPTIONS="${2:-}"
LOG_FILE="${3:-/tmp/test_output.log}"

# Verify fixture exists
RASTER_FILE="${FIXTURES_DIR}/${PATTERN}.raster"
if [ ! -f "$RASTER_FILE" ]; then
    echo "Error: Fixture '$RASTER_FILE' not found"
    echo "Available fixtures:"
    ls -1 "${FIXTURES_DIR}"/*.raster 2>/dev/null | xargs -n1 basename | sed 's/\.raster$//'
    exit 1
fi

# Build option string if not provided
if [ -z "$OPTIONS" ]; then
    OPTIONS="PageSize=w576h864 PrintSpeed=40"
fi

# Run driver
echo "Running: $DRIVER 1 user test 1 '$OPTIONS' $RASTER_FILE --log $LOG_FILE"
"$DRIVER" 1 user test 1 "$OPTIONS" "$RASTER_FILE" --log "$LOG_FILE"

# Show log
echo ""
echo "=== Log output ==="
cat "$LOG_FILE"
