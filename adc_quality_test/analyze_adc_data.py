#!/usr/bin/env python3
"""
ADC quality test - offline analysis of the CSV series produced by
adc_quality_test.py (files named "ADC<n>_<pin>_<v>V.csv", columns
sample_index,raw,adc_mV,vin_mV).

For every (ADC, pin) channel, across all its nominal voltage points, this
computes:

  Per point (one CSV file):
    - mean / std / min / max / peak-to-peak of raw, adc_mV, vin_mV
    - an approximate "effective resolution" from the raw code spread
      (see estimate_enob_bits() docstring for the exact, informal formula
      and its limitations - this is a DC noise indicator, not a rigorous
      SNR-based ENOB, which requires an AC/sine input)

  Per channel (all its points together):
    - a linear fit of measured mean vin_mV vs. the nominal voltage
      (least-squares), giving gain error and offset error
    - the residual (measured - fit) at each point, as a simple
      linearity/INL indicator
    - drift check: mean of the first half of samples vs. the second half
      of the largest series, to flag warm-up/thermal drift during a run

Results are written as two CSV summaries into "<folder>/analysis/", and
printed to the console. Pass --plot to also save PNG charts there
(per-channel gain/offset fit + residuals, and a cross-channel comparison).

Usage:
    python analyze_adc_data.py [folder] [--plot] [--show]

If "folder" is omitted, a folder-picker dialog is shown (falls back to a
text prompt if no display / tkinter is unavailable).
"""

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Optional

import numpy as np
import pandas as pd

FILENAME_RE = re.compile(r"^ADC(\d+)_([A-Za-z0-9]+)_(\d+)V\.csv$", re.IGNORECASE)

# 12-bit resolution, external VREF+ = 3.3 V (must match main.c's ADC_VREF_MV).
ADC_VREF_MV = 3300.0
ADC_FULL_SCALE_CODES = 4096.0


def select_folder(cli_arg: Optional[str]) -> Path:
    """Return the folder to analyze: the CLI argument if given, otherwise a
    folder-picker dialog, falling back to a plain text prompt if tkinter/a
    display is not available (e.g. running over SSH without X forwarding)."""
    if cli_arg:
        return Path(cli_arg)

    try:
        import tkinter as tk
        from tkinter import filedialog

        root = tk.Tk()
        root.withdraw()
        chosen = filedialog.askdirectory(title="Seleziona la cartella con i CSV del test ADC")
        root.destroy()
        if chosen:
            return Path(chosen)
    except Exception:
        pass

    default_dir = Path(__file__).resolve().parent / "data"
    typed = input(f"Cartella dati da analizzare (Enter per default: {default_dir}): ").strip()
    return Path(typed) if typed else default_dir


def print_config(folder: Path) -> None:
    """If adc_quality_test.py saved an adc_config.json in this folder (the ADC
    settings in effect when the data was acquired), print it so it travels
    together with the analysis results."""
    config_path = folder / "adc_config.json"
    if not config_path.is_file():
        print("(nessun adc_config.json trovato in questa cartella - dati acquisiti con una versione "
              "precedente dello script, o cartella sbagliata)")
        return

    with open(config_path) as f:
        config = json.load(f)

    print(f"=== Configurazione ADC al momento dell'acquisizione ({config_path.name}) ===")
    for key, value in config.items():
        print(f"  {key} = {value}")


def discover_series(folder: Path) -> list[dict]:
    """Find every ADC<n>_<pin>_<v>V.csv file in folder and parse its name."""
    entries = []
    for path in sorted(folder.glob("ADC*.csv")):
        match = FILENAME_RE.match(path.name)
        if match is None:
            print(f"  (ignorato, nome non riconosciuto: {path.name})")
            continue
        entries.append({
            "adc": int(match.group(1)),
            "pin": match.group(2),
            "nominal_v": int(match.group(3)),
            "path": path,
        })
    return entries


def estimate_enob_bits(raw_ptp: float) -> float:
    """Very informal "effective resolution" indicator from the peak-to-peak
    spread of raw codes at a fixed (DC) input: each doubling of the spread
    beyond 1 ideal LSB is treated as one lost bit of resolution.
    This is NOT the standard AC/sine-wave SNR-based ENOB (which cannot be
    computed from a constant-voltage input) - it is only meant as a quick,
    comparable-across-channels noise indicator."""
    spread = max(raw_ptp, 1.0)
    return 12.0 - np.log2(spread)


def analyze_point(entry: dict) -> dict:
    """Load one CSV series and compute its point-level statistics."""
    df = pd.read_csv(entry["path"])

    half = len(df) // 2
    first_half_mean = df["vin_mV"].iloc[:half].mean() if half > 0 else float("nan")
    second_half_mean = df["vin_mV"].iloc[half:].mean() if half > 0 else float("nan")

    return {
        "adc": entry["adc"],
        "pin": entry["pin"],
        "nominal_v": entry["nominal_v"],
        "n_samples": len(df),
        "raw_mean": df["raw"].mean(),
        "raw_std": df["raw"].std(),
        "raw_ptp": df["raw"].max() - df["raw"].min(),
        "adc_mV_mean": df["adc_mV"].mean(),
        "adc_mV_std": df["adc_mV"].std(),
        "vin_mV_mean": df["vin_mV"].mean(),
        "vin_mV_std": df["vin_mV"].std(),
        "vin_mV_ptp": df["vin_mV"].max() - df["vin_mV"].min(),
        "enob_bits_est": estimate_enob_bits(df["raw"].max() - df["raw"].min()),
        "drift_mV_1st_vs_2nd_half": second_half_mean - first_half_mean,
    }


def analyze_channel(points: pd.DataFrame) -> dict:
    """Linear fit of measured mean vin_mV vs. nominal voltage for one channel
    (across all its points), plus the residual at each point."""
    nominal_mV = points["nominal_v"].to_numpy(dtype=float) * 1000.0
    measured_mV = points["vin_mV_mean"].to_numpy(dtype=float)

    if len(nominal_mV) < 2 or np.ptp(nominal_mV) == 0:
        # Not enough distinct points to fit a line (e.g. only one voltage acquired).
        gain, offset_mV, r_squared = float("nan"), float("nan"), float("nan")
        residuals_mV = np.full_like(measured_mV, np.nan)
    else:
        gain, offset_mV = np.polyfit(nominal_mV, measured_mV, 1)
        fit_mV = gain * nominal_mV + offset_mV
        residuals_mV = measured_mV - fit_mV
        ss_res = np.sum(residuals_mV ** 2)
        ss_tot = np.sum((measured_mV - np.mean(measured_mV)) ** 2)
        r_squared = 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan")

    return {
        "adc": points["adc"].iloc[0],
        "pin": points["pin"].iloc[0],
        "n_points": len(points),
        "gain": gain,
        "gain_error_pct": (gain - 1.0) * 100.0,
        "offset_mV": offset_mV,
        "r_squared": r_squared,
        "max_abs_residual_mV": np.nanmax(np.abs(residuals_mV)) if len(residuals_mV) else float("nan"),
        "avg_noise_std_mV": points["vin_mV_std"].mean(),
        "avg_enob_bits_est": points["enob_bits_est"].mean(),
        "_residuals_mV": residuals_mV,  # kept for plotting, dropped before CSV export
        "_nominal_mV": nominal_mV,
        "_measured_mV": measured_mV,
    }


def print_and_save_tables(points_df: pd.DataFrame, channels_df: pd.DataFrame, outdir: Path) -> None:
    outdir.mkdir(parents=True, exist_ok=True)

    pd.set_option("display.width", 160)
    pd.set_option("display.max_columns", 20)

    print("\n=== Riepilogo per punto (ADC, canale, tensione nominale) ===")
    print(points_df.round(3).to_string(index=False))

    print("\n=== Riepilogo per canale (fit lineare su tutti i punti) ===")
    channels_export = channels_df.drop(columns=[c for c in channels_df.columns if c.startswith("_")])
    print(channels_export.round(4).to_string(index=False))

    points_df.to_csv(outdir / "summary_per_point.csv", index=False)
    channels_export.to_csv(outdir / "summary_per_channel.csv", index=False)
    print(f"\nSalvati: {outdir / 'summary_per_point.csv'}")
    print(f"Salvati: {outdir / 'summary_per_channel.csv'}")


def make_plots(channels: list[dict], outdir: Path, show: bool) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n(matplotlib non installato: salto i grafici. `pip install matplotlib` per averli.)")
        return

    outdir.mkdir(parents=True, exist_ok=True)

    # One fit+residual figure per channel.
    for ch in channels:
        if len(ch["_nominal_mV"]) < 2:
            continue
        fig, (ax_fit, ax_res) = plt.subplots(2, 1, figsize=(6, 7), sharex=True)

        nominal_v = ch["_nominal_mV"] / 1000.0
        ax_fit.plot(nominal_v, ch["_measured_mV"], "o", label="Misurato (media)")
        fit_line = ch["gain"] * ch["_nominal_mV"] + ch["offset_mV"]
        ax_fit.plot(nominal_v, fit_line, "-", label=f"Fit (guadagno={ch['gain']:.4f}, offset={ch['offset_mV']:.2f} mV)")
        ax_fit.set_ylabel("Vin misurata (mV)")
        ax_fit.set_title(f"ADC{ch['adc']} {ch['pin']}: guadagno/offset")
        ax_fit.legend()
        ax_fit.grid(True, alpha=0.3)

        ax_res.axhline(0, color="black", linewidth=0.8)
        ax_res.plot(nominal_v, ch["_residuals_mV"], "o-", color="tab:red")
        ax_res.set_xlabel("Tensione nominale (V)")
        ax_res.set_ylabel("Residuo (mV)")
        ax_res.grid(True, alpha=0.3)

        fig.tight_layout()
        fig.savefig(outdir / f"ADC{ch['adc']}_{ch['pin']}_fit.png", dpi=150)
        if not show:
            plt.close(fig)

    # Cross-channel comparison bar charts.
    labels = [f"ADC{ch['adc']}\n{ch['pin']}" for ch in channels]
    fig, axes = plt.subplots(3, 1, figsize=(8, 9), sharex=True)

    axes[0].bar(labels, [ch["gain_error_pct"] for ch in channels])
    axes[0].set_ylabel("Errore di guadagno (%)")
    axes[0].grid(True, axis="y", alpha=0.3)

    axes[1].bar(labels, [ch["offset_mV"] for ch in channels], color="tab:orange")
    axes[1].set_ylabel("Offset (mV)")
    axes[1].grid(True, axis="y", alpha=0.3)

    axes[2].bar(labels, [ch["avg_noise_std_mV"] for ch in channels], color="tab:green")
    axes[2].set_ylabel("Rumore medio - std (mV)")
    axes[2].grid(True, axis="y", alpha=0.3)

    fig.suptitle("Confronto tra i 10 canali")
    fig.tight_layout()
    fig.savefig(outdir / "all_channels_comparison.png", dpi=150)
    if not show:
        plt.close(fig)

    print(f"\nGrafici salvati in: {outdir}")

    if show:
        plt.show()


def main() -> int:
    parser = argparse.ArgumentParser(description="Analizza i CSV prodotti da adc_quality_test.py")
    parser.add_argument("folder", nargs="?", default=None,
                         help="Cartella con i file ADC<n>_<pin>_<v>V.csv (se omessa, si apre un selettore)")
    parser.add_argument("--plot", action="store_true", help="Genera anche i grafici PNG")
    parser.add_argument("--show", action="store_true", help="Mostra i grafici a schermo (richiede --plot)")
    args = parser.parse_args()

    folder = select_folder(args.folder)
    if not folder.is_dir():
        print(f"Cartella non trovata: {folder}")
        return 1

    print(f"Analisi della cartella: {folder}\n")
    print_config(folder)
    print()

    entries = discover_series(folder)
    if not entries:
        print("Nessun file ADC<n>_<pin>_<v>V.csv trovato in questa cartella.")
        return 1
    print(f"Trovate {len(entries)} serie.")

    points = [analyze_point(e) for e in entries]
    points_df = pd.DataFrame(points).sort_values(["adc", "pin", "nominal_v"]).reset_index(drop=True)

    channels = [
        analyze_channel(group)
        for _, group in points_df.groupby(["adc", "pin"], sort=False)
    ]
    channels_df = pd.DataFrame(channels).sort_values(["adc", "pin"]).reset_index(drop=True)

    outdir = folder / "analysis"
    print_and_save_tables(points_df, channels_df, outdir)

    # Flag anything that looks off, so it doesn't get buried in the tables.
    print("\n=== Osservazioni ===")
    any_flag = False
    for ch in channels:
        if not np.isnan(ch["gain_error_pct"]) and abs(ch["gain_error_pct"]) > 1.0:
            print(f"  ADC{ch['adc']} {ch['pin']}: errore di guadagno elevato ({ch['gain_error_pct']:+.2f}%)")
            any_flag = True
        if not np.isnan(ch["offset_mV"]) and abs(ch["offset_mV"]) > 20.0:
            print(f"  ADC{ch['adc']} {ch['pin']}: offset elevato ({ch['offset_mV']:+.1f} mV)")
            any_flag = True
    for p in points:
        if abs(p["drift_mV_1st_vs_2nd_half"]) > 5.0:
            print(f"  ADC{p['adc']} {p['pin']} @ {p['nominal_v']}V: deriva sospetta tra 1a e 2a metà "
                  f"della serie ({p['drift_mV_1st_vs_2nd_half']:+.2f} mV)")
            any_flag = True
    if not any_flag:
        print("  Nessuna anomalia sopra le soglie di default (guadagno >1%, offset >20mV, deriva >5mV).")

    if args.plot:
        make_plots(channels, outdir, args.show)

    return 0


if __name__ == "__main__":
    sys.exit(main())
