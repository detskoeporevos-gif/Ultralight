#!/usr/bin/env bash
# Smoke test for the EscFR sample: launch it on a virtual X display, make sure
# it stays alive, and capture screenshots of the two internal pages.
#
# usage: linux-smoke.sh "build/samples/Sample 10 - EscFR Browser"
set -uo pipefail

APP_DIR="${1:-}"
if [[ -z "$APP_DIR" || ! -x "$APP_DIR/EscFR" ]]; then
  echo "EscFR binary not found in '$APP_DIR'" >&2
  exit 1
fi

cd "$APP_DIR" || exit 1

export LIBGL_ALWAYS_SOFTWARE=1
export ESCFR_FORCE_CPU=1
export NO_AT_BRIDGE=1

DISPLAY_NUM=":99"
Xvfb "$DISPLAY_NUM" -screen 0 1280x820x24 -nolisten tcp > /tmp/xvfb.log 2>&1 &
XVFB_PID=$!
sleep 3

if ! kill -0 "$XVFB_PID" 2>/dev/null; then
  echo "Xvfb failed to start" >&2
  cat /tmp/xvfb.log >&2
  exit 1
fi

export DISPLAY="$DISPLAY_NUM"

cleanup() {
  [[ -n "${APP_PID:-}" ]] && kill "$APP_PID" 2>/dev/null
  kill "$XVFB_PID" 2>/dev/null
}
trap cleanup EXIT

FAILED=0

run_case() {
  local url="$1"
  local shot="$2"
  local log="escfr-${shot%.png}.log"

  echo "=== launching EscFR with start url: $url ==="
  ESCFR_START_URL="$url" ./EscFR > "$log" 2>&1 &
  APP_PID=$!

  # Give the renderer time to load fonts, paint the chrome and fetch the page.
  sleep 14

  if ! kill -0 "$APP_PID" 2>/dev/null; then
    echo "FAIL: EscFR exited before the smoke window elapsed ($url)"
    tail -n 60 "$log" || true
    FAILED=1
    return
  fi

  import -window root "$shot" > /tmp/import.log 2>&1 || true

  if [[ ! -s "$shot" ]]; then
    echo "FAIL: no screenshot produced for $url"
    cat /tmp/import.log || true
    FAILED=1
  else
    local colors
    colors=$(identify -format "%k" "$shot" 2>/dev/null || echo 0)
    echo "screenshot $shot — ${colors} distinct colours"
    identify "$shot" || true
    if [[ "${colors:-0}" -lt 64 ]]; then
      echo "FAIL: window looks blank ($url)"
      FAILED=1
    fi
  fi

  echo "--- app log ($log) ---"
  tail -n 25 "$log" || true

  kill "$APP_PID" 2>/dev/null
  wait "$APP_PID" 2>/dev/null
  APP_PID=""
}

run_case "esc://newtab" "escfr-newtab.png"
run_case "esc://settings" "escfr-settings.png"
run_case "esc://about" "escfr-about.png"

if [[ "$FAILED" -ne 0 ]]; then
  echo "smoke test failed" >&2
  exit 1
fi

echo "smoke test passed"
