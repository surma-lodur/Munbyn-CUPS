#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRIVER="${ROOT_DIR}/rastertorw403b"
TMP_DIR="$(mktemp -d)"
OUT_FILE="${TMP_DIR}/out.tsc"
LOG_FILE="${TMP_DIR}/rw403b.log"
FIXTURE_FILE="${TMP_DIR}/gradient.raster"
MULTI_FILE="${TMP_DIR}/multi.raster"

cleanup() {
    rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

if [[ ! -x "${DRIVER}" ]]; then
    make -C "${ROOT_DIR}" rastertorw403b >/dev/null
fi

if [[ ! -x "${DRIVER}" ]]; then
    echo "FAIL: missing driver binary at ${DRIVER}" >&2
    exit 1
fi

make -C "${ROOT_DIR}" test/test_tsc_output test/test_page_index >/dev/null
"${ROOT_DIR}/test/test_tsc_output" >/dev/null
"${ROOT_DIR}/test/test_page_index" >/dev/null

python3 "${ROOT_DIR}/fixtures/gen_raster.py" "${FIXTURE_FILE}" gradient 576 864 1 >/dev/null
python3 "${ROOT_DIR}/fixtures/gen_raster.py" "${MULTI_FILE}" gradient 576 864 3 >/dev/null

OPTIONS="PageSize=w576h864 PrintSpeed=40 Darkness=210 Horizontal=1 Vertical=1"
"${DRIVER}" 1 user smoke 1 "${OPTIONS}" "${FIXTURE_FILE}" --log "${LOG_FILE}" > "${OUT_FILE}"

if [[ ! -s "${OUT_FILE}" ]]; then
    echo "FAIL: fixture-driven TSC output is empty" >&2
    exit 1
fi

need_tokens=(
    "SIZE "
    "GAP "
    "REFERENCE 0,0"
    "OFFSET "
    "SETC AUTODOTTED OFF"
    "SETC PAUSEKEY OFF"
    "DENSITY "
    "SPEED "
    "DIRECTION 0,0"
    "CLS"
    "BITMAP "
    "PRINT 1,1"
)

for tok in "${need_tokens[@]}"; do
    if ! grep -aFq "${tok}" "${OUT_FILE}"; then
        echo "FAIL: missing token '${tok}' in fixture-driven TSC output" >&2
        exit 1
    fi
done

"${DRIVER}" 2 user smoke 1 "${OPTIONS}" "${MULTI_FILE}" --log "${LOG_FILE}" > /dev/null
for line in "DrvStartDoc++: page_index=0" "DrvSendPage: page_index=1" "DrvSendPage: page_index=2" "DrvSendPage: page_index=3" "DrvEndDoc--: total_pages=3"; do
    if ! grep -Fq "${line}" "${LOG_FILE}"; then
        echo "FAIL: missing log line '${line}'" >&2
        exit 1
    fi
done

echo "PASS: smoke"
