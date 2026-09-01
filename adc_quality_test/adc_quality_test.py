#!/usr/bin/env python3
"""
ADC quality test - PC-side controller / logger.

Talks to the MEZZA_EXP_IN board over UART5 (the same debug link used for the
normal demo firmware) while it is built and flashed with TEST_ADC_QUALITY = 1
(see main.c). Before the first channel, the board sends one "CONFIG_BEGIN" /
"CONFIG_END" block of "key=value" lines describing the exact ADC settings for
this run (resolution, VREF, clock, sampling time, attenuation factor, channel
and voltage lists, current-loop shunt/channel count/nominal currents, ...),
then "SELECT_PROMPT" and blocks: this script replies with "ALL" (default, no
channel selected on the command line), "ADC<n>,CH<c>" (single local ADC
channel, see --ADC1/--ADC2 and --CH0..--CH7 below), or "CURRENT,CH<c>"
(single remote 4-20mA current-loop channel on the other board, read by this
board over SPI2 - see --ADC_CURRENT below). Then, for each selected
combination:

  1. Sends "READY,ADC<n>,<pin>,<v>V" or "READY,CURRENT,CH<c>,<i>mA" and then
     blocks, waiting for anything to arrive on UART5.
  2. Once this script sends "GO", the board replies with
     "START,ADC<n>,<pin>,<v>V,<count>" followed by <count> CSV lines
     "<index>,<raw>,<adc_mV>,<vin_mV>", or "START,CURRENT,CH<c>,<i>mA,<count>"
     followed by <count> CSV lines "<index>,<raw>,<adc_mV>,<current_uA>",
     then "END,...".
  3. This repeats for every combination; the board finally sends "ALL_DONE".

Usage:
    python adc_quality_test.py COM5                       # test everything (10 local + 8 remote channels)
    python adc_quality_test.py COM5 --ADC1 --CH3           # test only ADC1 channel 3 (PA3)
    python adc_quality_test.py COM5 --ADC2 --CH1           # test only ADC2 channel 1 (PB1)
    python adc_quality_test.py COM5 --ADC_CURRENT --CH5    # test only remote current-loop channel 5

This script:
  - Prints every line it does not specifically recognize (e.g. the firmware's
    boot messages), so nothing is silently swallowed.
  - Saves the CONFIG block to "adc_config.json" in the --outdir folder, before
    any data file is written.
  - On each READY, prompts the operator to set up the reference voltage (or
    loop current) on the given channel, waits for Enter, then tells the board
    to go ahead.
  - Saves each completed series to its own CSV file, named "ADC<n>_<pin>_<v>V.csv"
    (local channels) or "CURRENT_CH<c>_<i>mA.csv" (remote current-loop channels),
    inside the --outdir folder (default: "data", next to this script).
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
DEFAULT_OUTDIR = Path(__file__).resolve().parent / "data"


def parse_args():
    parser = argparse.ArgumentParser(description="MEZZA_EXP_IN ADC quality test controller")
    parser.add_argument("port", help="Serial port connected to UART5 (e.g. COM5 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Baud rate (default: {DEFAULT_BAUD})")
    parser.add_argument("--outdir", type=Path, default=DEFAULT_OUTDIR,
                         help=f"Folder where the CSV files are saved (default: {DEFAULT_OUTDIR})")

    device_group = parser.add_mutually_exclusive_group()
    device_group.add_argument("--ADC1", action="store_true", help="Test only ADC1 (requires one of --CH0..--CH7)")
    device_group.add_argument("--ADC2", action="store_true", help="Test only ADC2 (requires --CH0 or --CH1)")
    device_group.add_argument("--ADC_CURRENT", action="store_true",
                               help="Test only one remote 4-20mA current-loop channel on the other board "
                                    "(requires one of --CH0..--CH7)")

    ch_group = parser.add_mutually_exclusive_group()
    for n in range(8):
        ch_group.add_argument(f"--CH{n}", action="store_true",
                               help=f"Select channel {n} (with --ADC1/--ADC2/--ADC_CURRENT)")

    args = parser.parse_args()

    device = "ADC1" if args.ADC1 else ("ADC2" if args.ADC2 else ("CURRENT" if args.ADC_CURRENT else None))
    ch_selected = next((n for n in range(8) if getattr(args, f"CH{n}")), None)

    if (device is None) != (ch_selected is None):
        parser.error("--ADC1/--ADC2/--ADC_CURRENT e --CH0..--CH7 vanno usati insieme "
                      "(oppure nessuno dei due, per testare tutto).")
    if device == "ADC2" and ch_selected is not None and ch_selected > 1:
        parser.error("ADC2 ha solo i canali CH0 e CH1.")

    if device is None:
        args.selection = "ALL"
        args.selection_label = "tutti i canali (10 locali + 8 remoti in corrente)"
    elif device == "CURRENT":
        args.selection = f"CURRENT,CH{ch_selected}"
        args.selection_label = f"solo canale corrente remoto {ch_selected}"
    else:
        args.selection = f"{device},CH{ch_selected}"
        args.selection_label = f"solo {device} canale {ch_selected}"

    return args


def read_line(ser: serial.Serial) -> str:
    """Block until one full line arrives on the serial port, return it stripped."""
    raw = ser.readline()
    return raw.decode("ascii", errors="replace").strip()


def handle_config(ser: serial.Serial, outdir: Path, port: str, baud: int) -> dict:
    """Read "key=value" lines until "CONFIG_END", save them (plus a few
    host-side fields for traceability) to "<outdir>/adc_config.json", and
    return the merged dict. Called once, before the first READY."""
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

    return config


def handle_ready(ser: serial.Serial, fields: list[str]) -> None:
    """fields = ["READY", "ADC<n>", "<pin>", "<v>V"] or ["READY", "CURRENT", "CH<c>", "<i>mA"]"""
    adc_label, pin_label, voltage_label = fields[1], fields[2], fields[3]
    print(f"\n=== {adc_label} / {pin_label} / {voltage_label} ===")
    if adc_label == "CURRENT":
        input(f"Imposta il loop a {voltage_label} sul canale remoto {pin_label}, poi premi Enter... ")
    else:
        input(f"Collega/imposta il riferimento a {voltage_label} su {pin_label} ({adc_label}), poi premi Enter... ")
    ser.write(b"GO\r\n")


def handle_start(ser: serial.Serial, fields: list[str], outdir: Path) -> None:
    """fields = ["START", "ADC<n>", "<pin>", "<v>V", "<count>"]
    or ["START", "CURRENT", "CH<c>", "<i>mA", "<count>"]"""
    adc_label, pin_label, voltage_label, count_str = fields[1], fields[2], fields[3], fields[4]
    count = int(count_str)
    is_current = adc_label == "CURRENT"

    outdir.mkdir(parents=True, exist_ok=True)
    filename = outdir / f"{adc_label}_{pin_label}_{voltage_label}.csv"

    print(f"Acquisizione di {count} campioni -> {filename.name} ...", end="", flush=True)
    t_start = time.monotonic()

    with open(filename, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["sample_index", "raw", "adc_mV", "current_uA" if is_current else "vin_mV"])

        received = 0
        while received < count:
            line = read_line(ser)
            if not line:
                continue
            if line.startswith("END,"):
                print(f"\n  ATTENZIONE: END ricevuto dopo solo {received}/{count} campioni.")
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


def handle_end(fields: list[str]) -> None:
    """fields = ["END", "ADC<n>", "<pin>", "<v>V"] - only reached in the normal
    case where handle_start() already consumed exactly <count> samples and the
    firmware's END line is read here, right after, by the main loop."""
    adc_label, pin_label, voltage_label = fields[1], fields[2], fields[3]
    print(f"Serie completata: {adc_label} / {pin_label} / {voltage_label}")


def main() -> int:
    args = parse_args()

    print(f"Apertura {args.port} @ {args.baud} baud...")
    print(f"Modalita': {args.selection_label}")
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

            if fields[0] == "READY":
                handle_ready(ser, fields)
            elif fields[0] == "START":
                handle_start(ser, fields, args.outdir)
            elif fields[0] == "END":
                handle_end(fields)
            elif fields[0] == "ALL_DONE":
                print("\n*** Test completato: tutte le combinazioni sono state acquisite. ***")
                return 0
            else:
                # Firmware boot/status messages, or anything else not part of the
                # READY/START/.../END/ALL_DONE protocol: just show them.
                print(line)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrotto dall'utente.")
        sys.exit(1)
