# ADC quality test

Procedura interattiva PC + firmware per acquisire dati ADC grezzi da usare per
qualificare i 10 ingressi ADC della scheda MEZZA_EXP_IN (accuratezza, rumore,
ripetibilità), usando come riferimento un alimentatore da banco (es. Rigol
DP932A) con letture note.

Nella stessa procedura sono acquisiti anche gli **8 canali di ingresso in
corrente 4-20mA** dell'altra scheda (stesso microcontrollore), letti da questa
scheda tramite SPI2 con un protocollo a comando/risposta - vedi la sezione
dedicata più sotto.

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

> **Baud rate: 921600** (non più 115200), definito una sola volta in
> `MX_UART5_BAUD_RATE` (`generated/hal/mx_uart5.h`) — lo script Python lo usa
> già come default. Il collo di bottiglia della procedura è la trasmissione
> UART dei 10000 campioni per punto, non l'ADC (che è ordini di grandezza più
> veloce - vedi `adc_max_sample_rate_sps` nel blocco CONFIG più sotto), quindi
> alzare il baud rate accelera il test quasi proporzionalmente. Se il tuo
> adattatore USB-seriale supporta velocità ancora maggiori, quel `#define` è
> l'unico punto da cambiare (ricordati di aggiornare anche `DEFAULT_BAUD` in
> `adc_quality_test.py`).

## 3. Esecuzione

```
pip install -r requirements.txt

python adc_quality_test.py COM5                    # test completo: 10 canali locali + 8 canali corrente remoti
python adc_quality_test.py COM5 --ADC1 --CH3        # solo ADC1, canale 3 (PA3)
python adc_quality_test.py COM5 --ADC2 --CH1        # solo ADC2, canale 1 (PB1)
python adc_quality_test.py COM5 --ADC_CURRENT --CH5 # solo canale di corrente remoto 5 (altra scheda)
```

Sostituisci `COM5` con la tua porta seriale. `--ADC1`/`--ADC2`/`--ADC_CURRENT` e
`--CH0`..`--CH7` vanno usati insieme (o nessuno dei due, per il test completo);
`--ADC2` accetta solo `--CH0` o `--CH1` (ADC2 ha solo quei due canali
collegati), `--ADC_CURRENT` accetta `--CH0`..`--CH7` (8 canali remoti). Quando
selezioni un solo canale, la scheda salta all'istante tutti gli altri (nessuna
attesa, nessuna trasmissione) — non paghi il tempo delle combinazioni non
richieste.

Prima di tutto, la scheda manda un blocco con le impostazioni ADC di questa
sessione (VREF, risoluzione, clock, sampling time, fattore di attenuazione,
elenco canali/tensioni, ...): lo script lo salva subito in
`data/adc_config.json`, **prima** di qualunque file di dati, così resta un
riferimento di cosa era configurato quando i dati sono stati acquisiti. Subito
dopo, la scheda chiede quale selezione fare e lo script risponde in automatico
in base a `--ADC1/--ADC2/--CH...` (o "tutto", se non li hai passati).

Poi, per ciascuna combinazione selezionata lo script:

1. Ti chiede di impostare quella tensione (o quella corrente di loop, per i
   canali remoti) sul canale indicato e di premere Enter.
2. Manda "vai" alla scheda, che acquisisce 10000 campioni di quel solo canale
   (`ADC_QUALITY_SAMPLES_PER_POINT` in `main.c`) e li trasmette in streaming.
3. Salva la serie in un CSV dentro `data/`:
   - canali locali: `ADC<n>_<pin>_<v>V.csv` (es. `data/ADC1_PA0_2V.csv`),
     tensioni nominali 0/2/4/6/8/10 V (`adc_quality_voltages_v[]` in `main.c`),
     colonne `sample_index,raw,adc_mV,vin_mV`.
   - canali di corrente remoti: `CURRENT_CH<c>_<i>mA.csv` (es.
     `data/CURRENT_CH5_12mA.csv`), correnti nominali 4/8/12/16/20 mA
     (`adc_current_nominal_ma[]` in `main.c`), colonne
     `sample_index,raw,adc_mV,current_uA`.
4. Passa alla combinazione successiva, finché non arriva `ALL_DONE` (prima
   tutti i canali locali selezionati, poi tutti quelli di corrente).

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
file `ADC<n>_<pin>_<v>V.csv` e `CURRENT_CH<c>_<i>mA.csv` nella cartella scelta
(canali locali e remoti finiscono nelle stesse tabelle: le colonne di
tensione/corrente sono generiche - `nominal`/`nominal_unit`, `value_mean`/
`value_unit`, ecc. - proprio per poterle confrontare insieme) e calcola:

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
`summary_per_channel.csv`), stampati anche a schermo. Con `--plot`, sempre in
`<cartella>/analysis/`: un grafico guadagno/offset + residui per ciascun canale
(serve almeno 2 punti nominali acquisiti per quel canale, altrimenti viene
saltato), più due confronti a barre separati - uno per i canali di tensione
(`all_channels_comparison_voltage.png`) e uno per i canali di corrente
(`all_channels_comparison_current.png`), dato che le due famiglie usano unità
diverse (mV vs uA) e tipicamente un numero diverso di canali (10 vs 8). Se una
delle due famiglie non ha ancora dati, il relativo confronto viene
semplicemente omesso.

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
(lo script manda "ALL", "ADC<n>,CH<c>" oppure "CURRENT,CH<c>")
[SELECT_INVALID - solo se la risposta non era valida: si procede con "ALL"]
READY,ADC<n>,<pin>,<v>V               -> la scheda si blocca qui (canale locale)
READY,CURRENT,CH<c>,<i>mA             -> la scheda si blocca qui (canale remoto)
(operatore prepara il riferimento/loop, poi lo script manda un qualsiasi dato)
START,ADC<n>,<pin>,<v>V,<count>
<index>,<raw>,<adc_mV>,<vin_mV>       -> ripetuta <count> volte
END,ADC<n>,<pin>,<v>V
START,CURRENT,CH<c>,<i>mA,<count>
<index>,<raw>,<adc_mV>,<current_uA>   -> ripetuta <count> volte
END,CURRENT,CH<c>,<i>mA
... (si ripete per ogni combinazione selezionata: prima tutti i canali
     locali, poi tutti quelli di corrente) ...
ALL_DONE
```

Parametri inclusi nel blocco CONFIG: `firmware_build`, `test_id`, `adc_vref_mV`,
`adc_resolution_bits`, `adc_kernel_clock_Hz`, `adc_sampling_time_cycles`,
`adc_conv_cycles_x10` (x10 per evitare i float, es. 125 = 12,5 cicli),
`adc_conv_time_ns` (durata di UNA conversione a singolo canale: sampling +
conversione SAR, ai clock dell'ADC), `adc_max_sample_rate_sps` (il suo
reciproco: velocità massima teorica dell'ADC per un singolo canale, in
campioni/secondo), `test_effective_sample_rate_adc1_sps` /
`test_effective_sample_rate_adc2_sps` (velocità *effettiva* per un canale
durante questo test specifico: essendo letto tramite la scansione round-robin
dell'intera sequenza — 8 rank su ADC1, 2 su ADC2 — la velocità realmente
ottenuta per quel canale è quella massima divisa per 8 o per 2),
`atten_factor_num`/`atten_factor_den` (fattore = num/den, es. 10000/2875 =
0,2875), `samples_per_point`, `uart_baud`, `voltages_v` (lista), `channels`
(lista `ADC<n>:<pin>`), `current_channel_count` (8), `current_shunt_ohm` (150,
la resistenza di precisione in serie al loop di corrente, sull'altra scheda),
`current_nominal_mA` (lista, es. `4,8,12,16,20`). Lo script aggiunge anche
`_host_acquisition_datetime`, `_host_serial_port`, `_host_baud` lato PC.

> Anche alla velocità *effettiva* (decine/centinaia di migliaia di
> campioni/secondo), l'ADC resta ordini di grandezza più veloce della UART:
> il tempo di una serie da 10000 campioni è dominato quasi interamente dalla
> trasmissione seriale, non dall'acquisizione.

Il contenuto del comando mandato dallo script per "sbloccare" la scheda dopo
una `READY` non viene interpretato dal firmware: basta che arrivi *qualcosa*.
La risposta a `SELECT_PROMPT` invece viene interpretata (deve essere `ALL`,
`ADC<1|2>,CH<n>`, `CURRENT,CH<n>` oppure `DYNAMIC,<device>,CH<n>,<duration_ms>`
esatto - quest'ultima è descritta nella sezione seguente).

## Acquisizione dinamica a tempo (`adc_dynamic_acquisition.py`)

Oltre alla procedura di qualifica sopra (canali fissi, punti nominali fissi,
10000 campioni per punto), c'è un secondo script per un'acquisizione **di un
solo canale, per una durata scelta da riga di comando**, invece che per un
numero di campioni fisso:

```
python adc_dynamic_acquisition.py COM5 --ADC1 --CH3 --duration 10        # ADC1 canale 3 (PA3), 10 s
python adc_dynamic_acquisition.py COM5 --ADC2 --CH1 --duration 2.5       # ADC2 canale 1 (PB1), 2,5 s
python adc_dynamic_acquisition.py COM5 --ADC_CURRENT --CH5 --duration 30 # canale di corrente remoto 5, 30 s
```

`--ADC1`/`--ADC2`/`--ADC_CURRENT` e `--CH0`..`--CH7` sono **obbligatori** (a
differenza di `adc_quality_test.py`, qui non esiste un modo "tutti i canali":
questa modalità è pensata per guardare un solo segnale nel tempo). `--duration`
è la durata in secondi, decimali ammessi (es. `2.5`).

La scheda non conta più i campioni in anticipo: acquisisce e trasmette in
loop finché non è trascorso `--duration` secondi (misurati sulla scheda con
`HAL_GetTick()`), poi si ferma - vedi `adc_quality_run_dynamic()` in `main.c`.
Il numero di campioni risultante quindi **non è noto in anticipo** e non è lo
stesso tra un canale locale e uno di corrente remoto: un canale locale è
molto più rapido da campionare (una scansione ADC) di un canale di corrente
remoto (un intero scambio SPI START+POLL...POLL con l'altra scheda per ogni
campione) - a parità di durata richiesta, aspettati molti meno campioni per
`--ADC_CURRENT`.

Il file viene salvato in `dynamic_data/` (non in `data/`, per non mischiarlo
con le acquisizioni di qualifica), con lo stesso schema di nome usato per la
qualifica ma con la durata al posto della tensione/corrente nominale:
`ADC<n>_<pin>_<durata>s.csv` (es. `dynamic_data/ADC1_PA3_10s.csv`) oppure
`CURRENT_CH<c>_<durata>s.csv` (es. `dynamic_data/CURRENT_CH5_2.5s.csv`),
colonne `sample_index,raw,adc_mV,vin_mV` (locale) o
`sample_index,raw,adc_mV,current_uA` (corrente) - identiche a quelle della
qualifica. Viene salvato anche un `dynamic_data/adc_config.json`, come per
`adc_quality_test.py`.

Wire protocol:

```
CONFIG_BEGIN / ... / CONFIG_END, SELECT_PROMPT       -- come sopra
(lo script manda "DYNAMIC,<device>,CH<c>,<duration_ms>", es. "DYNAMIC,ADC1,CH3,10000")
READY,DYNAMIC,<device>,<pin>,<duration_ms>ms         -- la scheda si blocca qui
(prepara quello che vuoi catturare, poi lo script manda un qualsiasi dato)
START,DYNAMIC,<device>,<pin>,<duration_ms>ms
<index>,<raw>,<adc_mV>,<vin_mV o current_uA>          -- finché non trascorre <duration_ms>
END,DYNAMIC,<device>,<pin>,<duration_ms>ms,<count>
ALL_DONE
```

`<device>` è `ADC1`, `ADC2` o `CURRENT`. `analyze_adc_data.py` **non** legge
ancora questi file (è pensato per i punti di calibrazione a tensione/corrente
nota, non per una serie temporale) - se in futuro serve analizzare/plottare
anche i dati di `dynamic_data/`, è un'estensione separata da chiedere.

## Canali di corrente remoti (4-20mA, altra scheda)

L'altra scheda ha 8 ingressi in corrente 4-20mA; questa scheda li legge uno
alla volta su richiesta, tramite un protocollo a comando/risposta sulla
stessa SPI2 già usata per il test SPI del firmware normale (questa scheda è
sempre Master). Frame fissi da 4 byte:

```
Master TX: [CMD, CH, 0x00, 0x00]
Slave  TX : [STATUS, RAW_HI, RAW_LO, CH_ECHO]   (riflette la transazione PRECEDENTE - vedi nota SPI full-duplex nel README principale)
```

- `CMD` = `0x01` (START, avvia la conversione sul canale `CH`) oppure `0x02`
  (POLL, chiede se il risultato è pronto).
- Sequenza per una lettura: un frame START, poi frame POLL ripetuti finché la
  risposta ha `STATUS = 0x02` (READY) **e** `CH_ECHO` combacia con il canale
  richiesto (protezione da risposte residue/disallineate) - a quel punto
  `RAW_HI`/`RAW_LO` (big-endian) sono il codice ADC a 12 bit di quella
  conversione, con lo stesso formato/VREF di questa scheda (stesso
  microcontrollore).
- Il raw viene convertito in tensione al pin ADC con la stessa formula dei
  canali locali (VREF 3.3 V, 12 bit), poi in corrente con la legge di Ohm
  sulla resistenza di precisione da 150 ohm in serie al loop: `corrente_uA =
  tensione_mV * 1000 / 150` (4-20 mA corrispondono a 0,6-3,0 V, ampiamente
  dentro i 3,3 V di fondo scala).
- Vedi `adc_current_read_channel()` / `spi_current_xfer()` in `main.c` per
  l'implementazione lato Master (questa scheda); il firmware Slave
  sull'altra scheda è descritto nel prompt cross-board dedicato.
