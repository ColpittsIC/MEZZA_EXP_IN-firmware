# MEZZA_EXP_IN - Firmware di test

Firmware di test per la scheda MEZZA_EXP_IN, basata sul microcontrollore **STM32C552CEU6** (famiglia STM32C5).

Il firmware esegue quattro test hardware in loop, riportando i risultati via UART:

1. **Test ADC**: lettura ciclica di 10 ingressi analogici.
2. **Test LED**: accensione sequenziale dei 10 LED pilotati in Charlieplexing.
3. **Test USART3**: invio periodico di un messaggio verso l'altro microcontrollore e ricezione della sua risposta, interamente a interrupt.
4. **Test SPI2**: scambio periodico di un byte full-duplex con l'altro microcontrollore (questa scheda è SPI Master), interamente a interrupt.

Esiste inoltre una **modalità di build separata**, selezionata dal flag `TEST_ADC_QUALITY` in cima a `main.c` (0 = firmware normale sopra, 1 = procedura di qualifica ADC pilotata da uno script Python, che sostituisce interamente il loop normale). Oltre ai 10 canali locali, questa modalità legge anche 8 canali di corrente 4-20mA dell'altra scheda tramite SPI2. Vedi [`adc_quality_test/README.md`](adc_quality_test/README.md).

## Hardware

### Ingressi ADC (10 canali)

| Pin  | Pin package | Canale ADC  |
|------|:-----------:|-------------|
| PA0  | 10          | ADC1_IN0    |
| PA1  | 11          | ADC1_IN1    |
| PA2  | 12          | ADC1_IN2    |
| PA3  | 13          | ADC1_IN3    |
| PA4  | 14          | ADC1_IN4    |
| PA5  | 15          | ADC1_IN5    |
| PA6  | 16          | ADC1_IN6    |
| PA7  | 17          | ADC1_IN7    |
| PB0  | 18          | ADC2_IN6    |
| PB1  | 19          | ADC2_IN7    |

- VREF+ esterna: **3.3 V**
- Risoluzione: 12 bit
- Tra l'ingresso della scheda e il pin dell'ADC c'è un fattore di attenuazione di **0.2875** (es. 10 V in ingresso -> 2.875 V al pin ADC). Il firmware applica il fattore inverso per calcolare la tensione reale in ingresso.

### UART di debug

Tutti i risultati dei test vengono stampati come testo su **UART5**:

- TX: PB6
- RX: PB5
- Baud rate: **921600**, 8N1, nessun controllo di flusso (`MX_UART5_BAUD_RATE` in `generated/hal/mx_uart5.h` — unico punto da cambiare se serve un'altra velocità; ricordati di aggiornare anche il terminale che usi per leggerla)
- RX è usata solo in modalità `TEST_ADC_QUALITY` (vedi sotto), per ricevere i comandi dallo script Python; nel firmware normale la UART5 è usata solo in trasmissione.

### LED - Charlieplexing (10 LED, 4 pin di pilotaggio)

| Segnale   | Pin  |
|-----------|------|
| LED_ROW1  | PB7  |
| LED_ROW2  | PC13 |
| LED_ROW3  | PA15 |
| LED_ROW4  | PC14 |

Mappatura LED (anodo/catodo):

| LED  | Anodo | Catodo |
|------|-------|--------|
| DL2  | ROW2  | ROW1   |
| DL3  | ROW3  | ROW1   |
| DL4  | ROW4  | ROW1   |
| DL5  | ROW1  | ROW2   |
| DL6  | ROW3  | ROW2   |
| DL7  | ROW4  | ROW2   |
| DL8  | ROW1  | ROW3   |
| DL9  | ROW2  | ROW3   |
| DL10 | ROW4  | ROW3   |
| DL11 | ROW1  | ROW4   |

> Nota: PA15 è di default la funzione JTDI (JTAG). Se il debug/programmazione avviene in SWD (tipico con ST-Link) non ci sono conflitti.

### USART3 - link verso l'altro microcontrollore

| Segnale     | Pin |
|-------------|-----|
| USART3_TX   | PB3 |
| USART3_RX   | PB4 |

- Baud rate: 115200, 8N1, nessun controllo di flusso
- Gestione **interamente a interrupt** (nessun polling/timeout): trasmissione con `HAL_UART_Transmit_IT`, ricezione con `HAL_UART_ReceiveToIdle_IT` (chiude la ricezione sia a buffer pieno sia al rilevamento di linea idle, quindi va bene anche per messaggi di risposta di lunghezza variabile).

> Nota: l'alternate function usata per USART3 su PB3/PB4 è **AF11** (`HAL_GPIO_AF11_USART3`), confermata contro il datasheet STM32C552xx.

### SPI2 - link verso l'altro microcontrollore

| Segnale     | Pin  |
|-------------|------|
| SPI2_NSS/CS | PB12 |
| SPI2_SCK    | PB13 |
| SPI2_MISO   | PB14 |
| SPI2_MOSI   | PB15 |

- Alternate function: **AF5** (`HAL_GPIO_AF5_SPI2`)
- Questa scheda è configurata come **SPI Master**; l'altra scheda deve essere **SPI Slave**.
- Modalità SPI 0 (CPOL=0, CPHA=0), 8 bit/frame, MSB first, NSS gestito in hardware dal master (`HAL_SPI_NSS_PIN_MGMT_OUTPUT`, un impulso di CS per ogni transfer).
- Baud rate: PCLK1/64 (~2.25 MHz con PCLK1 a 144 MHz).
- Gestione **interamente a interrupt** (`HAL_SPI_TransmitReceive_IT`), un solo transfer da 1 byte al secondo.
- **Nota sul pipelining full-duplex**: essendo la SPI sincrona e full-duplex, il byte ricevuto in un dato transfer è quello che lo slave aveva già preparato *prima* dell'inizio del transfer stesso, cioè corrisponde alla risposta al byte del ciclo *precedente*, non a quello appena inviato. Non è un baco: è una proprietà normale dei protocolli SPI full-duplex (identica a un registro a scorrimento).
- In modalità `TEST_ADC_QUALITY` la stessa SPI2 (stesso ruolo Master) viene usata anche per un protocollo a comando/risposta a 4 byte, per leggere 8 canali di corrente 4-20mA dell'altra scheda - vedi [`adc_quality_test/README.md`](adc_quality_test/README.md) per il protocollo completo.
- Il modulo SPI non era selezionato nel progetto CubeMX originale: `USE_HAL_SPI_MODULE` è stato abilitato a mano in `generated/hal/stm32c5xx_hal_conf.h`, e sia `generated/hal/mx_spi2.c` sia il driver `stm32c5xx_drivers/hal/stm32c5xx_hal_spi.c` sono referenziati direttamente in `cmake/files.cmake` (stesso motivo di USART3, vedi sotto).

## Comportamento del firmware

All'avvio (`main.c`):

1. Inizializzazione del sistema (clock, ADC1, ADC2, UART5, USART3, SPI2) tramite `mx_system_init()`.
2. Messaggio di boot su UART5.
3. Attivazione e calibrazione di ADC1 e ADC2.
4. Inizializzazione dei 4 pin di Charlieplexing (tutti a riposo in Hi-Z, LED spenti).
5. Armamento della ricezione a interrupt su USART3 (`HAL_UART_ReceiveToIdle_IT`).

Poi, in loop continuo (1 iterazione al secondo):

1. Lettura dei 10 canali ADC (modalità discontinua, 1 rank per trigger) e stampa via UART di raw, tensione al pin ADC (mV) e tensione reale in ingresso (V).
2. Invio non bloccante (`HAL_UART_Transmit_IT`) di un messaggio `PING <n>` su USART3 verso l'altro microcontrollore.
3. Accensione di **un solo LED alla volta**, in ordine DL2 -> DL11 (poi si ripete), con stampa via UART di quale LED è acceso e quale coppia di pin lo pilota. Sulla stessa riga viene riportato anche l'ultimo messaggio ricevuto su USART3 dall'ultima iterazione (`USART3: <testo>`), oppure `USART3: No-Message` se non è arrivato nulla nel frattempo. La ricezione vera e propria avviene in background, in interrupt (`HAL_UART_RxCpltCallback`), quindi non blocca né rallenta le altre operazioni.
4. Report del transfer SPI2 completato durante l'iterazione precedente (`SPI2 test: sent 0x.., received 0x..`), poi avvio non bloccante (`HAL_SPI_TransmitReceive_IT`) di un nuovo transfer da 1 byte.

In caso di errore su un passo di inizializzazione o di conversione ADC, il firmware stampa un messaggio diagnostico su UART (con indicazione del canale/rank e del codice di stato HAL) e si ferma.

## Script Python (modalità `TEST_ADC_QUALITY`)

Nella cartella [`adc_quality_test/`](adc_quality_test/) ci sono tre script Python che parlano con la scheda via UART5 quando è flashata con `TEST_ADC_QUALITY = 1`. Installa le dipendenze una volta (da dentro quella cartella):

```
pip install -r requirements.txt
```

| Script | Cosa fa | Dove serve la scheda collegata? |
|---|---|---|
| `adc_quality_test.py` | Procedura di qualifica completa: tutti i 10 canali locali + gli 8 canali di corrente remoti, ciascuno su più punti nominali (tensioni/correnti fisse), 10000 campioni per punto. | Sì |
| `adc_dynamic_acquisition.py` | Acquisizione di **un solo canale per una durata a scelta** (secondi, anche decimali), invece di punti nominali fissi - pensata per guardare un segnale nel tempo. | Sì |
| `analyze_adc_data.py` | Analisi offline dei CSV prodotti da `adc_quality_test.py` (fit lineare guadagno/offset, rumore, grafici). **Non legge** i file prodotti da `adc_dynamic_acquisition.py`. | No (lavora sui CSV già salvati) |

Le note complete su protocollo, formato dei CSV e dettagli di ciascuno sono in [`adc_quality_test/README.md`](adc_quality_test/README.md); qui sotto solo un riepilogo dei comandi e - soprattutto - **quali flag valgono per quale script**, perché `adc_quality_test.py` e `adc_dynamic_acquisition.py` condividono gli stessi flag di selezione canale ma con regole diverse.

### `adc_quality_test.py` - qualifica completa

```
python adc_quality_test.py COM5                       # tutto: 10 canali locali + 8 remoti in corrente
python adc_quality_test.py COM5 --ADC1 --CH3           # solo ADC1 canale 3 (PA3)
python adc_quality_test.py COM5 --ADC2 --CH1           # solo ADC2 canale 1 (PB1)
python adc_quality_test.py COM5 --ADC_CURRENT --CH5    # solo canale di corrente remoto 5
```

- `--ADC1` / `--ADC2` / `--ADC_CURRENT` e `--CH0`..`--CH7` sono **opzionali**: se li ometti entrambi, testa tutto. Se usi uno dei tre, devi passare anche un `--CHn` (e viceversa).
- `--ADC2` accetta solo `--CH0` o `--CH1` (ha solo quei due canali collegati); `--ADC1` e `--ADC_CURRENT` accettano `--CH0`..`--CH7`.
- Salva in `data/` (default; cambiabile con `--outdir`).

### `adc_dynamic_acquisition.py` - un canale, durata a scelta

```
python adc_dynamic_acquisition.py COM5 --ADC1 --CH3 --duration 10          # ADC1 canale 3 (PA3), 10 s
python adc_dynamic_acquisition.py COM5 --ADC2 --CH1 --duration 2.5         # ADC2 canale 1 (PB1), 2,5 s
python adc_dynamic_acquisition.py COM5 --ADC_CURRENT --CH5 --duration 30   # canale di corrente remoto 5, 30 s
python adc_dynamic_acquisition.py COM5 --ADC1 --CH3 --duration 10 --no-plot  # come sopra, senza generare il PNG
```

- A differenza di `adc_quality_test.py`, qui `--ADC1`/`--ADC2`/`--ADC_CURRENT` **e** `--CH0`..`--CH7` sono **obbligatori** - non esiste un "tutti i canali", questo script guarda sempre un solo segnale.
- `--duration` (secondi, decimali ammessi) è **obbligatorio**.
- `--ADC2` accetta solo `--CH0`/`--CH1`, come sopra.
- Genera in automatico anche un grafico PNG del segnale acquisito - passa `--no-plot` per saltarlo.
- Salva in `dynamic_data/` (default; cambiabile con `--outdir`), **non** in `data/`.

### `analyze_adc_data.py` - analisi offline

```
python analyze_adc_data.py               # apre un selettore di cartella
python analyze_adc_data.py data          # analizza data/ (l'output di adc_quality_test.py)
python analyze_adc_data.py data --plot   # + grafici PNG
python analyze_adc_data.py data --plot --show   # + li mostra anche a schermo
```

- Prende come argomento posizionale la **cartella** con i CSV (tipicamente `data/`, non `dynamic_data/` - vedi sopra).
- `--plot` genera i grafici PNG in `<cartella>/analysis/`; `--show` li mostra anche a schermo, ma **ha effetto solo insieme a `--plot`**.
- Nessun flag di selezione canale: analizza sempre tutti i CSV trovati nella cartella indicata.

## Struttura del progetto

Progetto generato con STM32CubeMX (nuovo modello di generazione basato su CMake), poi esteso a mano per aggiungere i due test:

- `main.c` / `main.h` — logica applicativa dei test (scritta a mano).
- `generated/hal/mx_adc1.c`, `mx_adc2.c` — inizializzazione ADC1/ADC2, incluse le configurazioni canale/sequencer completate a mano (non generabili automaticamente da CubeMX in questo progetto).
- `generated/hal/mx_uart5.c` — inizializzazione UART5 (generata da CubeMX, non modificata).
- `generated/hal/mx_usart3.c` — inizializzazione USART3 (scritta a mano, non essendo stata selezionata nel progetto CubeMX originale; per questo è referenziata direttamente in `cmake/files.cmake` invece che nel meccanismo di generazione CMSIS-pack).
- `generated/hal/mx_spi2.c` — inizializzazione SPI2 Master (scritta a mano, stesso motivo di USART3 sopra; anche il driver `stm32c5xx_hal_spi.c` è referenziato a mano in `cmake/files.cmake` per lo stesso motivo).
- `stm32c5xx_drivers/` — driver HAL/LL STM32C5 (libreria ST, non modificare).
- `stm32c5xx_dfp/`, `arch/cmsis/` — CMSIS e device support pack STM32C5 (libreria ST, non modificare).
- `cmake/`, `CMakeLists.txt`, `CMakePresets.json` — configurazione build CMake.

## Build

Richiede: CMake, Ninja e il toolchain `arm-none-eabi-gcc` 14.3.1 (es. quelli inclusi in STM32CubeIDE).

```
cmake --preset debug_GCC_STM32C552CEU6
cmake --build build/debug_GCC_STM32C552CEU6
```

L'output è `build/debug_GCC_STM32C552CEU6/MEZZA_EXP_IN.elf` (la cartella `build/` non è tracciata in git).

### Generare .hex / .bin per il flash

La build CMake produce solo l'`.elf`. Per ottenere `.hex`/`.bin`:

```
arm-none-eabi-objcopy -O ihex   build/debug_GCC_STM32C552CEU6/MEZZA_EXP_IN.elf build/debug_GCC_STM32C552CEU6/MEZZA_EXP_IN.hex
arm-none-eabi-objcopy -O binary build/debug_GCC_STM32C552CEU6/MEZZA_EXP_IN.elf build/debug_GCC_STM32C552CEU6/MEZZA_EXP_IN.bin
```

## Flash e test

1. Programmare `MEZZA_EXP_IN.elf` (o `.hex`) sulla scheda tramite ST-Link (es. STM32CubeProgrammer o da STM32CubeIDE).
2. Collegare un adattatore USB-seriale a UART5 (TX scheda = PB6 -> RX adattatore, RX scheda = PB5 <- TX adattatore, GND comune).
3. Aprire un terminale seriale a 921600 8N1.
4. Al reset, verificare il messaggio di boot, poi i risultati periodici di ADC e LED.
