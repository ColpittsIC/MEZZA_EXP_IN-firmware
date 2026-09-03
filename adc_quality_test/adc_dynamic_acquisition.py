#!/usr/bin/env python3
"""
ADC dynamic acquisition - PC-side controller / logger for a single-channel,
duration-bounded capture (as opposed to adc_quality_test.py's fixed-sample-
count calibration procedure over every channel/nominal-value combination).

Talks to the MEZZA_EXP_IN board over UART5 while it is built and flashed with
TEST_ADC_QUALITY = 1 (see main.c) - same link, same CONFIG_BEGIN/CONFIG_END
and SELECT_PROMPT handshake as adc_quality_test.py, but this script always
replies to SELECT_PROMPT with a "DYNAMIC,<device>,CH<c>,<duration_ms>"
selection: the board then acquires ONE channel (a local ADC1/ADC2 channel, or
one of the other board's 8 remote 4-20mA current-loop channels) for exactly
duration_ms milliseconds (measured on the board with HAL_GetTick()), instead
of a fixed number of samples - the resulting sample count is whatever fits in
that time, and is NOT known ahead of time (it depends on the channel: a local
ADC channel is much faster per sample than a remote current-loop channel,
which needs a full SPI request/poll round-trip with the other board).

Sequence:
  1. CONFIG_BEGIN/.../CONFIG_END, SELECT_PROMPT (as in adc_quality_test.py) -
     this script replies with the DYNAMIC selection built from --ADC1/--ADC2/
     --ADC_CURRENT, --CH0..--CH7 and --duration.
  2. "READY,DYNAMIC,<device>,<pin>,<duration_ms>ms" - the board blocks here.
  3. Once this script sends "GO", "START,DYNAMIC,<device>,<pin>,<duration_ms>ms"
     followed by an a-priori-unknown number of CSV lines
     "<index>,<raw>,<adc_mV>,<vin_mV or current_uA>", until
     "END,DYNAMIC,<device>,<pin>,<duration_ms>ms,<count>", then "ALL_DONE".

Usage:
    python adc_dynamic_acquisition.py COM5 --ADC1 --CH3 --duration 10        # ADC1 channel 3 (PA3), 10 s
    python adc_dynamic_acquisition.py COM5 --ADC2 --CH1 --duration 2.5       # ADC2 channel 1 (PB1), 2.5 s
    python adc_dynamic_acquisition.py COM5 --ADC_CURRENT --CH5 --duration 30 # remote current channel 5, 30 s

Saves the CSV to "dynamic_data/ADC<n>_<pin>_<duration>s.csv" (local channel)
or "dynamic_data/CURRENT_CH<c>_<duration>s.csv" (remote current-loop channel) -
same naming as adc_quality_test.py's calibration files, but with the
acquisition duration where that script has a nominal voltage/current.
"""

import argparse
import csv
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import serial

DEFAULT_BAUD = 921600  # must match MX_UART5_BAUD_RATE in generated/hal/mx_uart5.h
DEFAULT_OUTDIR = Path(__file__).resolve().parent / "dynamic_data"


def parse_args():
    parser = argparse.ArgumentParser(
        description="MEZZA_EXP_IN ADC dynamic (duration-bounded, single-channel) acquisition")
    parser.add_argument("port", help="Serial port connected to UART5 (e.g. COM5 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Baud rate (default: {DEFAULT_BAUD})")
    parser.add_argument("--outdir", type=Path, default=DEFAULT_OUTDIR,
                         help=f"Folder where the CSV file is saved (default: {DEFAULT_OUTDIR})")
    parser.add_argument("--duration", type=float, required=True,
                         help="Durata dell'acquisizione in secondi (accetta decimali, es. 2.5)")

    device_group = parser.add_mutually_exclusive_group(required=True)
    device_group.add_argument("--ADC1", action="store_true", help="Canale ADC1 locale (richiede --CH0..--CH7)")
    device_group.add_argument("--ADC2", action="store_true", help="Canale ADC2 locale (richiede --CH0 o --CH1)")
    device_group.add_argument("--ADC_CURRENT", action="store_true",
                               help="Canale di corrente remoto 4-20mA sull'altra scheda (richiede --CH0..--CH7)")

    ch_group = parser.add_mutually_exclusive_group(required=True)
    for n in range(8):
        ch_group.add_argument(f"--CH{n}", action="store_true",
                               help=f"Seleziona il canale {n} (con --ADC1/--ADC2/--ADC_CURRENT)")

    args = parser.parse_args()

    if args.duration <= 0:
        parser.error("--duration deve essere maggiore di zero.")

    device = "ADC1" if args.ADC1 else ("ADC2" if args.ADC2 else "CURRENT")
    ch = next(n for n in range(8) if getattr(args, f"CH{n}"))

    if device == "ADC2" and ch > 1:
        parser.error("ADC2 ha solo i canali CH0 e CH1.")

    args.device = device
    args.channel = ch
    args.duration_ms = round(args.duration * 1000)
    # Filename-friendly duration label: "5s", "2.5s", ... (no trailing ".0").
    args.duration_label = f"{args.duration:g}s"
    args.selection = f"DYNAMIC,{device},CH{ch},{args.duration_ms}"
    args.selection_label = f"{device} canale {ch}, {args.duration:g} s"

    return args


def read_line(ser: serial.Serial) -> str:
    """Block until one full line arrives on the serial port, return it stripped."""
    raw = ser.readline()
    return raw.decode("ascii", errors="replace").strip()


def handle_config(ser: serial.Serial, outdir: Path, port: str, baud: int) -> None:
    """Read "key=value" lines until "CONFIG_END" and save them (plus a few
    host-side fields) to "<outdir>/adc_config.json", same as
    adc_quality_test.py - so the ADC settings in effect travel alongside the
    acquired data here too. Called once, before READY."""
    config: dict[str, str] = {}
    while True:
        line = read_line(ser)
        if not line:
            continue
        if line == "CONFIG_END":
            break
        key, sep, value = line.partition("=")
        if sep:
            config[key] = value
        else:
            print(f"  (riga CONFIG ignorata: {line!r})")

    config["_host_acquisition_datetime"] = datetime.now(timezone.utc).isoformat()
    config["_host_serial_port"] = port
    config["_host_baud"] = baud

    outdir.mkdir(parents=True, exist_ok=True)
    config_path = outdir / "adc_config.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    print(f"\n=== Configurazione ADC per questa sessione (salvata in {config_path.name}) ===")
    for key, value in config.items():
        print(f"  {key} = {value}")


def handle_ready(ser: serial.Serial, fields: list[str]) -> None:
    """fields = ["READY", "DYNAMIC", "<device>", "<pin>", "<duration_ms>ms"]"""
    device, pin, duration_label = fields[2], fields[3], fields[4]
    print(f"\n=== Acquisizione dinamica: {device} / {pin} / {duration_label} ===")
    input("Prepara quello che vuoi catturare, poi premi Enter per far partire l'acquisizione... ")
    ser.write(b"GO\r\n")


def handle_start(ser: serial.Serial, fields: list[str], outdir: Path, duration_label: str) -> int:
    """fields = ["START", "DYNAMIC", "<device>", "<pin>", "<duration_ms>ms"].
    Unlike adc_quality_test.py's handle_start(), the sample count is NOT known
    ahead of time - this reads lines until "END,DYNAMIC,..." arrives, however
    many that turns out to be. Returns the number of samples received."""
    device, pin = fields[2], fields[3]
    is_current = device == "CURRENT"

    outdir.mkdir(parents=True, exist_ok=True)
    filename = outdir / f"{device}_{pin}_{duration_label}.csv"

    print(f"Acquisizione in corso ({duration_label}) -> {filename.name} ...", end="", flush=True)
    t_start = time.monotonic()

    received = 0
    with open(filename, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["sample_index", "raw", "adc_mV", "current_uA" if is_current else "vin_mV"])

        while True:
            line = read_line(ser)
            if not line:
                continue
            if line.startswith("END,DYNAMIC,"):
                break
            parts = line.split(",")
            if len(parts) != 4:
                print(f"\n  Riga inattesa ignorata: {line!r}")
                continue
            writer.writerow(parts)
            received += 1
            if received % 1000 == 0:
                print(".", end="", flush=True)

    elapsed = time.monotonic() - t_start
    print(f" fatto ({received} campioni, {elapsed:.1f} s).")
    return received


def main() -> int:
    args = parse_args()

    print(f"Apertura {args.port} @ {args.baud} baud...")
    print(f"Modalita': acquisizione dinamica - {args.selection_label}")
    with serial.Serial(args.port, args.baud, timeout=None) as ser:
        print("Connesso. In attesa dei messaggi dalla scheda (Ctrl+C per interrompere).\n")

        while True:
            line = read_line(ser)
            if not line:
                continue

            if line == "CONFIG_BEGIN":
                handle_config(ser, args.outdir, args.port, args.baud)
                continue

            if line == "SELECT_PROMPT":
                ser.write((args.selection + "\r\n").encode())
                continue

            fields = line.split(",")

            if fields[0] == "READY" and len(fields) >= 2 and fields[1] == "DYNAMIC":
                handle_ready(ser, fields)
            elif fields[0] == "START" and len(fields) >= 2 and fields[1] == "DYNAMIC":
                handle_start(ser, fields, args.outdir, args.duration_label)
            elif fields[0] == "ALL_DONE":
                print("\n*** Acquisizione completata. ***")
                return 0
            elif fields[0] == "SELECT_INVALID":
                print(f"ERRORE: la scheda ha rifiutato la selezione {args.selection!r} - "
                      f"il firmware sara' probabilmente TEST_ADC_QUALITY=1 ma con una versione "
                      f"di main.c che non conosce ancora la selezione DYNAMIC.")
            else:
                # Firmware boot/status messages, or anything else not part of the
                # READY/START/ALL_DONE protocol: just show them.
                print(line)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrotto dall'utente.")
        sys.exit(1)
