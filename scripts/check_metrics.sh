#!/bin/sh
# author: oaho
# date:   2026/04/29
# desc:   Scrape ZLMediaKit /metrics and validate exposition format with
#         Prometheus' promtool. Use this locally after metric/label changes
#         and (optionally) in CI when promtool is available.
#
# usage:  scripts/check_metrics.sh [URL]
#         default URL: http://localhost/metrics

set -eu

URL=${1:-http://localhost/metrics}

if ! command -v curl >/dev/null 2>&1; then
    echo "FAIL: curl not found in PATH"
    exit 10
fi
if ! command -v promtool >/dev/null 2>&1; then
    echo "FAIL: promtool not found in PATH (install: brew install prometheus, or grab a prometheus release)"
    exit 11
fi

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

if ! curl -sf "$URL" > "$TMP"; then
    echo "FAIL: scrape ${URL} failed"
    exit 1
fi

if ! promtool check metrics < "$TMP"; then
    echo "FAIL: invalid Prometheus exposition format"
    exit 2
fi

LINES=$(wc -l < "$TMP" | tr -d ' ')
SERIES=$(grep -c '^[a-z]' "$TMP" || true)
echo "OK: ${URL} - ${LINES} lines, ${SERIES} series"
