#!/usr/bin/env python3
"""
ADC quality test - offline analysis of the CSV series produced by
adc_quality_test.py: local ADC channels ("ADC<n>_<pin>_<v>V.csv", columns
sample_index,raw,adc_mV,vin_mV) and the other board's remote 4-20mA
current-loop channels ("CURRENT_CH<c>_<i>mA.csv", columns
sample_index,raw,adc_mV,current_uA). Both are analyzed the same way: each
has a nominal reference value (a voltage in V, or a loop current in mA) and
a measured value in the CSV's last column, scaled x1000 from the nominal
unit (mV for voltage channels, uA for current channels) - see
discover_series()/analyze_point() for how the two kinds are unified.

For every channel, across all its nominal points, this computes:

  Per point (one CSV file):
    - mean / std / min / max / peak-to-peak of raw, adc_mV, and the measured
      value column (vin_mV or current_uA)
    - an approximate "effective resolution" from the raw code spread
      (see estimate_enob_bits() docstring for the exact, informal formula
      and its limitations - this is a DC noise indicator, not a rigorous
      SNR-based ENOB, which requires an AC/sine input)

  Per channel (all its points together):
    - a linear fit of the measured mean value vs. the nominal value
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

FILENAME_RE_VOLTAGE = re.compile(r"^ADC(\d+)_([A-Za-z0-9]+)_(\d+)V\.csv$", re.IGNORECASE)
FILENAME_RE_CURRENT = re.compile(r"^CURRENT_(CH\d+)_(\d+)mA\.csv$", re.IGNORECASE)

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
    """Find every ADC<n>_<pin>_<v>V.csv (local channel) and
    CURRENT_CH<c>_<i>mA.csv (remote 4-20mA current-loop channel) file in
    folder, and parse its name into a common shape: "device"/"pin" identify
    the channel (e.g. "ADC1"/"PA0" or "CURRENT"/"CH5"), "nominal" is the
    reference value in "nominal_unit" (V or mA), and "value_col"/"value_unit"
    name the CSV column holding the measured value, scaled x1000 from
    nominal_unit (vin_mV for voltage channels, current_uA for current ones)."""
    entries = []
    for path in sorted(folder.glob("*.csv")):
        m_v = FILENAME_RE_VOLTAGE.match(path.name)
        m_c = FILENAME_RE_CURRENT.match(path.name)
        if m_v is not None:
            entries.append({
                "device": f"ADC{m_v.group(1)}",
                "pin": m_v.group(2),
                "nominal": int(m_v.group(3)),
                "nominal_unit": "V",
                "value_col": "vin_mV",
                "value_unit": "mV",
                "path": path,
            })
        elif m_c is not None:
            entries.append({
                "device": "CURRENT",
                "pin": m_c.group(1),
                "nominal": int(m_c.group(2)),
                "nominal_unit": "mA",
                "value_col": "current_uA",
                "value_unit": "uA",
                "path": path,
            })
        else:
            print(f"  (ignorato, nome non riconosciuto: {path.name})")
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
    """Load one CSV series and compute its point-level statistics. Works the
    same for a local voltage channel or a remote current-loop channel: the
    measured quantity is always entry["value_col"] (vin_mV or current_uA),
    reported here under generic "value_*" keys plus "value_unit" so the two
    kinds can share one summary table."""
    df = pd.read_csv(entry["path"])
    value_col = entry["value_col"]

    half = len(df) // 2
    first_half_mean = df[value_col].iloc[:half].mean() if half > 0 else float("nan")
    second_half_mean = df[value_col].iloc[half:].mean() if half > 0 else float("nan")

    return {
        "device": entry["device"],
        "pin": entry["pin"],
        "nominal": entry["nominal"],
        "nominal_unit": entry["nominal_unit"],
        "value_unit": entry["value_unit"],
        "n_samples": len(df),
        "raw_mean": df["raw"].mean(),
        "raw_std": df["raw"].std(),
        "raw_ptp": df["raw"].max() - df["raw"].min(),
        "adc_mV_mean": df["adc_mV"].mean(),
        "adc_mV_std": df["adc_mV"].std(),
        "value_mean": df[value_col].mean(),
        "value_std": df[value_col].std(),
        "value_ptp": df[value_col].max() - df[value_col].min(),
        "enob_bits_est": estimate_enob_bits(df["raw"].max() - df["raw"].min()),
        "drift_1st_vs_2nd_half": second_half_mean - first_half_mean,
    }


def analyze_channel(points: pd.DataFrame) -> dict:
    """Linear fit of the measured mean value vs. nominal value for one
    channel (across all its points), plus the residual at each point.
    Nominal and measured are both scaled to the same base (nominal x1000 ==
    the measured column's unit: V->mV for voltage channels, mA->uA for
    current channels), so gain/offset/residuals come out directly in
    points["value_unit"] regardless of which kind this channel is."""
    nominal_scaled = points["nominal"].to_numpy(dtype=float) * 1000.0
    measured = points["value_mean"].to_numpy(dtype=float)

    if len(nominal_scaled) < 2 or np.ptp(nominal_scaled) == 0:
        # Not enough distinct points to fit a line (e.g. only one nominal value acquired).
        gain, offset, r_squared = float("nan"), float("nan"), float("nan")
        residuals = np.full_like(measured, np.nan)
    else:
        gain, offset = np.polyfit(nominal_scaled, measured, 1)
        fit = gain * nominal_scaled + offset
        residuals = measured - fit
        ss_res = np.sum(residuals ** 2)
        ss_tot = np.sum((measured - np.mean(measured)) ** 2)
        r_squared = 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan")

    return {
        "device": points["device"].iloc[0],
        "pin": points["pin"].iloc[0],
        "value_unit": points["value_unit"].iloc[0],
        "n_points": len(points),
        "gain": gain,
        "gain_error_pct": (gain - 1.0) * 100.0,
        "offset": offset,
        "r_squared": r_squared,
        "max_abs_residual": np.nanmax(np.abs(residuals)) if len(residuals) else float("nan"),
        "avg_noise_std": points["value_std"].mean(),
        "avg_enob_bits_est": points["enob_bits_est"].mean(),
        "_residuals": residuals,  # kept for plotting, dropped before CSV export
        "_nominal_scaled": nominal_scaled,
        "_measured": measured,
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


def _make_comparison_plot(plt, channels: list[dict], title: str, out_path: Path, show: bool) -> None:
    """Gain error / offset / noise bar chart across a set of channels that
    all share the same value_unit (voltage-in-mV or current-in-uA) - see
    make_plots(). Does nothing if the list is empty (e.g. no current-loop
    data acquired yet)."""
    if not channels:
        return

    unit = channels[0]["value_unit"]
    labels = [f"{ch['device']}\n{ch['pin']}" for ch in channels]
    fig, axes = plt.subplots(3, 1, figsize=(8, 9), sharex=True)

    axes[0].bar(labels, [ch["gain_error_pct"] for ch in channels])
    axes[0].set_ylabel("Errore di guadagno (%)")
    axes[0].grid(True, axis="y", alpha=0.3)

    axes[1].bar(labels, [ch["offset"] for ch in channels], color="tab:orange")
    axes[1].set_ylabel(f"Offset ({unit})")
    axes[1].grid(True, axis="y", alpha=0.3)

    axes[2].bar(labels, [ch["avg_noise_std"] for ch in channels], color="tab:green")
    axes[2].set_ylabel(f"Rumore medio - std ({unit})")
    axes[2].grid(True, axis="y", alpha=0.3)

    fig.suptitle(title)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    if not show:
        plt.close(fig)


def make_plots(channels: list[dict], outdir: Path, show: bool) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n(matplotlib non installato: salto i grafici. `pip install matplotlib` per averli.)")
        return

    outdir.mkdir(parents=True, exist_ok=True)

    # One fit+residual figure per channel.
    for ch in channels:
        if len(ch["_nominal_scaled"]) < 2:
            continue
        fig, (ax_fit, ax_res) = plt.subplots(2, 1, figsize=(6, 7), sharex=True)

        nominal_native = ch["_nominal_scaled"] / 1000.0
        nominal_unit = "V" if ch["device"] != "CURRENT" else "mA"
        ax_fit.plot(nominal_native, ch["_measured"], "o", label="Misurato (media)")
        fit_line = ch["gain"] * ch["_nominal_scaled"] + ch["offset"]
        ax_fit.plot(nominal_native, fit_line, "-",
                    label=f"Fit (guadagno={ch['gain']:.4f}, offset={ch['offset']:.2f} {ch['value_unit']})")
        ax_fit.set_ylabel(f"Misurato ({ch['value_unit']})")
        ax_fit.set_title(f"{ch['device']} {ch['pin']}: guadagno/offset")
        ax_fit.legend()
        ax_fit.grid(True, alpha=0.3)

        ax_res.axhline(0, color="black", linewidth=0.8)
        ax_res.plot(nominal_native, ch["_residuals"], "o-", color="tab:red")
        ax_res.set_xlabel(f"Valore nominale ({nominal_unit})")
        ax_res.set_ylabel(f"Residuo ({ch['value_unit']})")
        ax_res.grid(True, alpha=0.3)

        fig.tight_layout()
        fig.savefig(outdir / f"{ch['device']}_{ch['pin']}_fit.png", dpi=150)
        if not show:
            plt.close(fig)

    # Cross-channel comparison bar charts: voltage and current channels use
    # different units (mV vs uA) and typically different counts (10 vs 8), so
    # each kind gets its own figure rather than mixing them in one chart.
    voltage_channels = [ch for ch in channels if ch["device"] != "CURRENT"]
    current_channels = [ch for ch in channels if ch["device"] == "CURRENT"]

    _make_comparison_plot(plt, voltage_channels, "Confronto tra i canali di tensione",
                           outdir / "all_channels_comparison_voltage.png", show)
    _make_comparison_plot(plt, current_channels, "Confronto tra i canali di corrente",
                           outdir / "all_channels_comparison_current.png", show)

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
        print("Nessun file ADC<n>_<pin>_<v>V.csv o CURRENT_CH<c>_<i>mA.csv trovato in questa cartella.")
        return 1
    print(f"Trovate {len(entries)} serie.")

    points = [analyze_point(e) for e in entries]
    points_df = pd.DataFrame(points).sort_values(["device", "pin", "nominal"]).reset_index(drop=True)

    channels = [
        analyze_channel(group)
        for _, group in points_df.groupby(["device", "pin"], sort=False)
    ]
    channels_df = pd.DataFrame(channels).sort_values(["device", "pin"]).reset_index(drop=True)

    outdir = folder / "analysis"
    print_and_save_tables(points_df, channels_df, outdir)

    # Flag anything that looks off, so it doesn't get buried in the tables.
    print("\n=== Osservazioni ===")
    any_flag = False
    for ch in channels:
        label = f"{ch['device']} {ch['pin']}"
        if not np.isnan(ch["gain_error_pct"]) and abs(ch["gain_error_pct"]) > 1.0:
            print(f"  {label}: errore di guadagno elevato ({ch['gain_error_pct']:+.2f}%)")
            any_flag = True
        if not np.isnan(ch["offset"]) and abs(ch["offset"]) > 20.0:
            print(f"  {label}: offset elevato ({ch['offset']:+.1f} {ch['value_unit']})")
            any_flag = True
    for p in points:
        if abs(p["drift_1st_vs_2nd_half"]) > 5.0:
            print(f"  {p['device']} {p['pin']} @ {p['nominal']}{p['nominal_unit']}: deriva sospetta tra 1a e 2a "
                  f"metà della serie ({p['drift_1st_vs_2nd_half']:+.2f} {p['value_unit']})")
            any_flag = True
    if not any_flag:
        print("  Nessuna anomalia sopra le soglie di default (guadagno >1%, offset >20 unità, deriva >5 unità - "
              "mV per i canali di tensione, uA per i canali di corrente).")

    if args.plot:
        make_plots(channels, outdir, args.show)

    return 0


if __name__ == "__main__":
    sys.exit(main())
