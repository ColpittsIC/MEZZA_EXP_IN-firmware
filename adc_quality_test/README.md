# ADC quality test

Procedura interattiva PC + firmware per acquisire dati ADC grezzi da usare per
qualificare i 10 ingressi ADC della scheda MEZZA_EXP_IN (accuratezza, rumore,
ripetibilità), usando come riferimento un alimentatore da banco (es. Rigol
DP932A) con letture note.

## 1. Build del firmware in modalità test

In `main.c`, imposta:

```c
#define TEST_ADC_QUALITY         1U
#define ADC_QUALITY_TEST_ID      1U   /* TEST_1, l'unico esistente oggi */
```

Poi ricompila e flasha normalmente (vedi il README principale del progetto).
In questa modalità il firmware **non** avvia LED/USART3/SPI2 - resta
concentrato sulla UART5 e sul test ADC.

> Ricordati di rimettere `TEST_ADC_QUALITY` a `0` quando vuoi tornare al
> firmware demo normale.

## 2. Collegamenti

- UART5 di debug (PB5/PB6) collegata al PC via adattatore USB-seriale, come già
  fai per leggere i log — ora però la userai anche per mandare comandi alla
  scheda (la UART è già full-duplex).
- Uscita dell'alimentatore collegata al pin ADC indicato dal messaggio
  `READY` corrente (uno alla volta).

## 3. Esecuzione

```
pip install -r requirements.txt
python adc_quality_test.py COM5        # sostituisci con la tua porta seriale
```

Per ciascuna combinazione (ADC, canale, tensione nominale — attualmente
0/2/4/6/8/10 V, definite in `adc_quality_voltages_v[]` in `main.c`) lo script:

1. Ti chiede di impostare quella tensione sul canale indicato e di premere Enter.
2. Manda "vai" alla scheda, che acquisisce 10000 campioni di quel solo canale
   (`ADC_QUALITY_SAMPLES_PER_POINT` in `main.c`) e li trasmette in streaming.
3. Salva la serie in `data/ADC<n>_<pin>_<v>V.csv` (es. `data/ADC1_PA0_2V.csv`),
   con colonne `sample_index,raw,adc_mV,vin_mV`.
4. Passa alla combinazione successiva, finché non arriva `ALL_DONE`.

Tutti i messaggi non riconosciuti (es. il boot del firmware) vengono comunque
stampati a schermo, quindi non perdi visibilità su cosa fa la scheda.

## 4. Analisi dei dati

```
python analyze_adc_data.py               # apre un selettore di cartella
python analyze_adc_data.py data          # oppure specifica la cartella direttamente
python analyze_adc_data.py data --plot   # + grafici PNG
python analyze_adc_data.py data --plot --show   # + li mostra anche a schermo
```

Legge tutti i file `ADC<n>_<pin>_<v>V.csv` nella cartella scelta e calcola:

- **Per ogni punto** (un file): media, deviazione standard, min/max/picco-picco
  di raw/adc_mV/vin_mV; una stima grezza della risoluzione effettiva dal
  picco-picco dei codici; un controllo di deriva (media prima metà vs seconda
  metà della serie, per scoprire eventuali derive termiche durante
  l'acquisizione).
- **Per ogni canale** (tutti i suoi punti insieme): fit lineare tensione
  misurata vs tensione nominale -> **errore di guadagno**, **offset**, R²,
  e il residuo (non linearità) ad ogni punto.
- Un elenco di **anomalie** rispetto a soglie di default (guadagno >1%,
  offset >20mV, deriva >5mV) - solo per farti notare in fretta cosa guardare
  prima nelle tabelle.

Risultati: due CSV di riepilogo in `<cartella>/analysis/` (`summary_per_point.csv`,
`summary_per_channel.csv`), stampati anche a schermo. Con `--plot`: un grafico
guadagno/offset + residui per ciascun canale, più un confronto a barre tra
tutti i canali, sempre in `<cartella>/analysis/`.

> Nota sulla "risoluzione effettiva stimata": è un indicatore informale basato
> sulla dispersione dei codici a tensione costante (DC), non l'ENOB standard
> da spettro/FFT (che richiede un ingresso sinusoidale) - utile solo per
> confrontare il rumore tra i canali, non come specifica di laboratorio.

## Protocollo seriale (per riferimento)

```
READY,ADC<n>,<pin>,<v>V              -> la scheda si blocca qui
(operatore prepara il riferimento, poi lo script manda un qualsiasi dato)
START,ADC<n>,<pin>,<v>V,<count>
<index>,<raw>,<adc_mV>,<vin_mV>      -> ripetuta <count> volte
END,ADC<n>,<pin>,<v>V
... (si ripete per ogni combinazione) ...
ALL_DONE
```

Il contenuto del comando mandato dallo script per "sbloccare" la scheda non
viene interpretato dal firmware: basta che arrivi *qualcosa*.
