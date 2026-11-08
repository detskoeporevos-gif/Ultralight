#!/usr/bin/env bash
# Smoke test for the EscFR sample: launch it on a virtual X display, make sure
# it stays alive, and capture screenshots of the internal pages.
#
# usage: linux-smoke.sh "build/Sample 10 - EscFR Browser"
set -uo pipefail

APP_DIR="${1:-}"
if [[ -z "$APP_DIR" || ! -x "$APP_DIR/EscFR" ]]; then
  echo "::error::EscFR binary not found in '$APP_DIR'"
  ls -la "$APP_DIR" 2>/dev/null | head -8 | while IFS= read -r line; do echo "::error::dir: $line"; done
  find build "/home/runner/work/Ultralight/Ultralight/build" -maxdepth 4 -name 'EscFR*' 2>/dev/null | head -6 | while IFS= read -r line; do echo "::error::found: $line"; done
  find . -maxdepth 4 -name 'EscFR*' 2>/dev/null | head -6 | while IFS= read -r line; do echo "::error::local: $line"; done
  exit 1
fi

cd "$APP_DIR" || exit 1

export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
export ESCFR_FORCE_CPU=1
export NO_AT_BRIDGE=1

echo "--- runtime linkage ---"
ldd ./EscFR 2>&1 | sed -n '1,40p'
if ldd ./EscFR 2>/dev/null | grep -q "not found"; then
  echo "::error::EscFR has unresolved shared libraries"
  ldd ./EscFR 2>&1 | grep "not found" | while IFS= read -r line; do echo "::error::$line"; done
  exit 1
fi

DISPLAY_NUM=":99"
Xvfb "$DISPLAY_NUM" -screen 0 1500x950x24 -nolisten tcp > /tmp/xvfb.log 2>&1 &
XVFB_PID=$!
sleep 3

if ! kill -0 "$XVFB_PID" 2>/dev/null; then
  echo "::error::Xvfb failed to start"
  cat /tmp/xvfb.log 2>&1 | while IFS= read -r line; do echo "::error::xvfb: $line"; done
  exit 1
fi

export DISPLAY="$DISPLAY_NUM"

capture() {
  local shot="$1"
  local wid
  wid=$(xdotool search --name "EscFR" 2>/dev/null | head -1)
  if [[ -n "$wid" ]]; then
    import -window "$(printf '0x%x' "$wid")" "$shot" > /tmp/import.log 2>&1 && return 0
  fi
  import -window root "$shot" >> /tmp/import.log 2>&1
}

cleanup() {
  [[ -n "${APP_PID:-}" ]] && kill "$APP_PID" 2>/dev/null
  kill "$XVFB_PID" 2>/dev/null
}
trap cleanup EXIT

FAILED=0

annotate_log() {
  tail -n 8 "$1" 2>/dev/null | while IFS= read -r line; do echo "::error::app: ${line}"; done
}

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
    annotate_log "$log"
    FAILED=1
    return
  fi

  capture "$shot"

  if [[ ! -s "$shot" ]]; then
    echo "FAIL: no screenshot produced for $url"
    cat /tmp/import.log 2>/dev/null | while IFS= read -r line; do echo "::error::import: $line"; done
    annotate_log "$log"
    FAILED=1
  else
    local colors
    colors=$(identify -format "%k" "$shot" 2>/dev/null || echo 0)
    echo "screenshot $shot — ${colors} distinct colours"
    identify "$shot" 2>&1 || true
    if [[ "${colors:-0}" -lt 64 ]]; then
      echo "FAIL: window looks blank ($url)"
      echo "::error::window looks blank for $url (${colors} colours)"
      annotate_log "$log"
      FAILED=1
    fi
  fi

  echo "--- app log ($log) ---"
  tail -n 25 "$log" 2>/dev/null || true

  kill "$APP_PID" 2>/dev/null
  wait "$APP_PID" 2>/dev/null
  APP_PID=""
}

# Extra case: drive the browser with real key events (tabs + shortcuts).
interactive_case() {
  local shot="escfr-tabs.png"
  local log="escfr-tabs.log"
  local wid

  echo "=== launching EscFR for interactive check ==="
  ESCFR_START_URL="esc://newtab" ./EscFR > "$log" 2>&1 &
  APP_PID=$!
  sleep 12

  if ! kill -0 "$APP_PID" 2>/dev/null; then
    echo "FAIL: EscFR exited during the interactive check"
    annotate_log "$log"
    FAILED=1
    return
  fi

  wid=$(xdotool search --name "EscFR" 2>/dev/null | head -1)
  if [[ -n "$wid" ]]; then
    xdotool windowfocus "$wid" 2>/dev/null || true
  fi

  echo "--- Ctrl+T (new tab) ---"
  xdotool key --clearmodifiers ctrl+t 2>/dev/null || true
  sleep 3
  capture "escfr-tabs-1.png" || true
  echo "--- Ctrl+L + type esc://settings + Enter ---"
  xdotool key --clearmodifiers ctrl+l 2>/dev/null || true
  sleep 1
  xdotool type --delay 40 "esc://settings" 2>/dev/null || true
  sleep 1
  xdotool key --clearmodifiers Return 2>/dev/null || true
  sleep 7

  capture "$shot"
  grep -iE "failed|error|exception|TABS|LOADING" "$log" 2>/dev/null | tail -8 | while IFS= read -r line; do echo "::error::DIAG $line"; done
  if [[ -s "$shot" ]]; then
    echo "screenshot $shot captured"
    identify "$shot" 2>&1 || true
  else
    echo "::warning::interactive screenshot was not captured"
  fi

  tail -n 15 "$log" 2>/dev/null || true
  kill "$APP_PID" 2>/dev/null
  wait "$APP_PID" 2>/dev/null
  APP_PID=""
}

run_case "esc://newtab" "escfr-newtab.png"
run_case "esc://settings" "escfr-settings.png"
run_case "esc://about" "escfr-about.png"
interactive_case

if [[ "$FAILED" -ne 0 ]]; then
  echo "smoke test failed"
  exit 1
fi

echo "smoke test passed"
