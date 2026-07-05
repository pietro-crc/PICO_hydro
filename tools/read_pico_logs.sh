#!/usr/bin/env bash
set -euo pipefail

port="${1:-}"

if [[ -z "$port" ]]; then
  for candidate in /dev/cu.usbmodem* /dev/tty.usbmodem* /dev/cu.usbserial* /dev/tty.usbserial*; do
    if [[ -e "$candidate" ]]; then
      port="$candidate"
      break
    fi
  done
fi

if [[ -z "$port" ]]; then
  echo "Pico USB seriale non trovato. Collega il Pico o premi reset e riprova." >&2
  exit 1
fi

echo "Apro log Pico su $port a 115200 baud."
echo "Per uscire da screen: Ctrl-A, poi K, poi Y."
exec screen "$port" 115200
