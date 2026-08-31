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

python adc_quality_test.py COM5              # test completo: tutti i 10 canali
python adc_quality_test.py COM5 --ADC1 --CH3 # solo ADC1, canale 3 (PA3)
python adc_quality_test.py COM5 --ADC2 --CH1 # solo ADC2, canale 1 (PB1)
```

Sostituisci `COM5` con la tua porta seriale. `--ADC1`/`--ADC2` e `--CH0`..`--CH7`
vanno usati insieme (o nessuno dei due, per il test completo); `--ADC2` accetta
solo `--CH0` o `--CH1` (ADC2 ha solo quei due canali collegati). Quando selezioni
un solo canale, la scheda salta all'istante tutti gli altri (nessuna attesa,
nessuna trasmissione) — non paghi il tempo delle combinazioni non richieste.

Prima di tutto, la scheda manda un blocco con le impostazioni ADC di questa
sessione (VREF, risoluzione, clock, sampling time, fattore di attenuazione,
elenco canali/tensioni, ...): lo script lo salva subito in
`data/adc_config.json`, **prima** di qualunque file di dati, così resta un
riferimento di cosa era configurato quando i dati sono stati acquisiti. Subito
dopo, la scheda chiede quale selezione fare e lo script risponde in automatico
in base a `--ADC1/--ADC2/--CH...` (o "tutto", se non li hai passati).

Poi, per ciascuna combinazione selezionata (ADC, canale, tensione nominale —
attualmente 0/2/4/6/8/10 V, definite in `adc_quality_voltages_v[]` in `main.c`)
lo script:

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

Se presente, stampa a inizio report anche il contenuto di `adc_config.json`
della cartella (le impostazioni ADC valide per quei dati). Poi legge tutti i
file `ADC<n>_<pin>_<v>V.csv` nella cartella scelta e calcola:

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
CONFIG_BEGIN                          -> una volta sola, a inizio sessione
key=value                              (una riga per parametro, vedi sotto)
...
CONFIG_END
SELECT_PROMPT                         -> la scheda si blocca qui
(lo script manda "ALL" oppure "ADC<n>,CH<c>")
[SELECT_INVALID - solo se la risposta non era valida: si procede con "ALL"]
READY,ADC<n>,<pin>,<v>V               -> la scheda si blocca qui
(operatore prepara il riferimento, poi lo script manda un qualsiasi dato)
START,ADC<n>,<pin>,<v>V,<count>
<index>,<raw>,<adc_mV>,<vin_mV>       -> ripetuta <count> volte
END,ADC<n>,<pin>,<v>V
... (si ripete per ogni combinazione selezionata) ...
ALL_DONE
```

Parametri inclusi nel blocco CONFIG: `firmware_build`, `test_id`, `adc_vref_mV`,
`adc_resolution_bits`, `adc_kernel_clock_Hz`, `adc_sampling_time_cycles`,
`adc_conv_cycles_x10` (x10 per evitare i float, es. 125 = 12,5 cicli),
`atten_factor_num`/`atten_factor_den` (fattore = num/den, es. 10000/2875 =
0,2875), `samples_per_point`, `uart_baud`, `voltages_v` (lista), `channels`
(lista `ADC<n>:<pin>`). Lo script aggiunge anche `_host_acquisition_datetime`,
`_host_serial_port`, `_host_baud` lato PC.

Il contenuto del comando mandato dallo script per "sbloccare" la scheda dopo
una `READY` non viene interpretato dal firmware: basta che arrivi *qualcosa*.
La risposta a `SELECT_PROMPT` invece viene interpretata (deve essere `ALL` o
`ADC<1|2>,CH<n>` esatto).
