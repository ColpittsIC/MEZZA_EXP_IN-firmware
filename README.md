# MEZZA_EXP_IN - Firmware di test

Firmware di test per la scheda MEZZA_EXP_IN, basata sul microcontrollore **STM32C552CEU6** (famiglia STM32C5).

Il firmware esegue tre test hardware in loop, riportando i risultati via UART:

1. **Test ADC**: lettura ciclica di 10 ingressi analogici.
2. **Test LED**: accensione sequenziale dei 10 LED pilotati in Charlieplexing.
3. **Test USART3**: invio periodico di un messaggio verso l'altro microcontrollore e ricezione della sua risposta, interamente a interrupt.

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
- Baud rate: 115200, 8N1, nessun controllo di flusso

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

## Comportamento del firmware

All'avvio (`main.c`):

1. Inizializzazione del sistema (clock, ADC1, ADC2, UART5, USART3) tramite `mx_system_init()`.
2. Messaggio di boot su UART5.
3. Attivazione e calibrazione di ADC1 e ADC2.
4. Inizializzazione dei 4 pin di Charlieplexing (tutti a riposo in Hi-Z, LED spenti).
5. Armamento della ricezione a interrupt su USART3 (`HAL_UART_ReceiveToIdle_IT`).

Poi, in loop continuo (1 iterazione al secondo):

1. Lettura dei 10 canali ADC (modalità discontinua, 1 rank per trigger) e stampa via UART di raw, tensione al pin ADC (mV) e tensione reale in ingresso (V).
2. Invio non bloccante (`HAL_UART_Transmit_IT`) di un messaggio `PING <n>` su USART3 verso l'altro microcontrollore.
3. Accensione di **un solo LED alla volta**, in ordine DL2 -> DL11 (poi si ripete), con stampa via UART di quale LED è acceso e quale coppia di pin lo pilota. Sulla stessa riga viene riportato anche l'ultimo messaggio ricevuto su USART3 dall'ultima iterazione (`USART3: <testo>`), oppure `USART3: No-Message` se non è arrivato nulla nel frattempo. La ricezione vera e propria avviene in background, in interrupt (`HAL_UART_RxCpltCallback`), quindi non blocca né rallenta le altre operazioni.

In caso di errore su un passo di inizializzazione o di conversione ADC, il firmware stampa un messaggio diagnostico su UART (con indicazione del canale/rank e del codice di stato HAL) e si ferma.

## Struttura del progetto

Progetto generato con STM32CubeMX (nuovo modello di generazione basato su CMake), poi esteso a mano per aggiungere i due test:

- `main.c` / `main.h` — logica applicativa dei test (scritta a mano).
- `generated/hal/mx_adc1.c`, `mx_adc2.c` — inizializzazione ADC1/ADC2, incluse le configurazioni canale/sequencer completate a mano (non generabili automaticamente da CubeMX in questo progetto).
- `generated/hal/mx_uart5.c` — inizializzazione UART5 (generata da CubeMX, non modificata).
- `generated/hal/mx_usart3.c` — inizializzazione USART3 (scritta a mano, non essendo stata selezionata nel progetto CubeMX originale; per questo è referenziata direttamente in `cmake/files.cmake` invece che nel meccanismo di generazione CMSIS-pack).
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
3. Aprire un terminale seriale a 115200 8N1.
4. Al reset, verificare il messaggio di boot, poi i risultati periodici di ADC e LED.
