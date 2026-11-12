# Tab5 "In riproduzione" per Apple Music

Un M5Stack Tab5 da scrivania che si accende da solo quando parte un brano su Apple Music e mostra copertina, minuti, tasti e il testo della canzone a tempo.

> **English:** a desk "Now Playing" display for Apple Music on an M5Stack Tab5 (ESP32-P4). It reads the current track from an iPhone/iPad over Bluetooth LE (Apple Media Service) or from a Mac over Wi-Fi, and shows cover art, progress, transport buttons and time-synced lyrics. A small macOS companion supplies artwork and lyrics. The documentation and on-screen text are in Italian. Not affiliated with Apple.

## Cosa fa

- Mostra il brano in riproduzione su **iPhone o iPad** (via Bluetooth) oppure sul **Mac** (via Wi-Fi).
- Tasti indietro, play/pausa e avanti. Con la musica dal Mac la barra di avanzamento si può trascinare.
- A destra il **testo sincronizzato**: la riga cantata è in evidenza e il testo scorre da solo.
- Se per un brano il testo non esiste, la copertina diventa grande a sinistra e titolo, barra e tasti vanno a destra.
- **Standby:** schermo spento senza musica; in pausa resta acceso 3 minuti; un tocco lo riaccende per 30 secondi.

## Com'è fatto

| Parte | Dove | Cosa fa |
|---|---|---|
| Firmware | `firmware/` | Gira sul Tab5 (ESP-IDF 5.5, LVGL 9). Riceve i dati del brano dall'iPhone e disegna lo schermo. |
| Compagno | `companion/` | Programma in background sul Mac. Fornisce copertina, colori di sfondo, testo e lo stato dell'app Musica del Mac. |

Il compagno cerca la copertina nella libreria dell'app Musica e, se non la trova, nel catalogo pubblico iTunes. Il testo arriva da [lrclib.net](https://lrclib.net), un archivio pubblico curato da volontari: per ogni brano il Mac invia a quel sito titolo, artista, album e durata.

## Replicare il progetto da zero

Serve:

- un **M5Stack Tab5** e un cavo USB-C dati;
- un **Mac** sulla stessa rete Wi-Fi del Tab5 (rete a 2,4 GHz), con gli strumenti da riga di comando di Xcode (`xcode-select --install`);
- un iPhone o iPad con Apple Music, oppure l'app Musica del Mac.

1. Scarica il progetto:

```bash
git clone https://github.com/75davide75/tab5-apple-music.git && cd tab5-apple-music
```

2. Installa ESP-IDF 5.5 (una volta sola; servono `cmake` e `ninja`, ad esempio con `brew install cmake ninja`):

```bash
mkdir -p ~/esp && git clone -b v5.5.6 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf && ~/esp/esp-idf/install.sh esp32p4
```

3. Copia il modello della configurazione Wi-Fi e scrivici nome e password della tua rete:

```bash
cp firmware/main/wifi_secrets.example.h firmware/main/wifi_secrets.h
```

4. Collega il Tab5 e trova il nome della porta (sostituiscilo nei comandi più sotto se è diverso da `/dev/cu.usbmodem2101`):

```bash
ls /dev/cu.usbmodem*
```

5. Compila e carica il firmware come descritto in "Cambiare rete Wi-Fi o ricaricare il firmware".
6. Installa il compagno sul Mac con `companion/install.sh`.
7. Abbina l'iPhone o l'iPad come descritto in "Abbinare un iPhone o un iPad".

Il progetto è stato provato su un solo Tab5 (ESP32-P4 revisione 1.3, firmware di fabbrica del chip radio). Su altre revisioni possono servire le modifiche descritte in "Note tecniche su questo Tab5".

## Uso quotidiano

Non c'è niente da fare: il Tab5 resta alimentato via USB e il compagno parte da solo al login del Mac.

Dopo un'interruzione di corrente il Tab5 impiega circa 10 secondi per il Bluetooth e circa 30 per il Wi-Fi.

## Abbinare un iPhone o un iPad

1. Sul dispositivo apri **Impostazioni → Bluetooth**.
2. Tocca **Tab5 Music** tra gli altri dispositivi e conferma l'abbinamento.

Possono restare collegati due dispositivi insieme. Per abbinarne un terzo, spegni il Bluetooth di uno dei due.

Per ripartire da zero: sul dispositivo scegli "Dissocia questo dispositivo" per Tab5 Music, poi abbinalo di nuovo.

## Cambiare rete Wi-Fi o ricaricare il firmware

1. Scrivi nome e password della rete in `firmware/main/wifi_secrets.h` (il file non viene salvato in git; il modello è `wifi_secrets.example.h`).
2. Collega il Tab5 al Mac con un cavo USB-C dati. Se il Mac non lo vede, tieni premuto Reset per 2 secondi finché il LED verde lampeggia.
3. Dal terminale:

```bash
. ~/esp/esp-idf/export.sh
```

```bash
cd firmware && idf.py -p /dev/cu.usbmodem2101 build flash
```

Se dopo il caricamento lo schermo resta fermo, il Tab5 è ancora in modalità di caricamento: premi Reset una volta.

## Compagno sul Mac

Installare o aggiornare:

```bash
companion/install.sh
```

Disinstallare:

```bash
companion/uninstall.sh
```

Il compagno apre l'app Musica al login e ogni volta che deve leggerne una copertina: è normale vederla nel Dock.

Il registro è in `~/Library/Logs/tab5-companion.log`. Al primo avvio macOS chiede il permesso di controllare l'app Musica: va accettato.

## Test

```bash
firmware/host_tests/run.sh
```

```bash
cd companion && swift test
```

## Limiti noti

- **Solo l'app Musica.** Video, podcast e altre app sull'iPhone non accendono il Tab5. L'app viene riconosciuta dal nome, che dipende dalla lingua dell'iPhone: l'elenco dei nomi è in `firmware/components/core/ams_parse.c`. Solo l'italiano è stato provato su un dispositivo vero; se nella tua lingua il Tab5 resta spento, aggiungi lì il nome con cui l'iPhone chiama l'app.
- **Barra di avanzamento con iPhone/iPad:** mostra i minuti ma non si trascina. Il Bluetooth di Apple accetta solo play/pausa, avanti e indietro.
- **Coda e prossimi brani:** l'iPhone non li comunica, quindi non sono mostrati.
- **Testo:** è sincronizzato riga per riga, non parola per parola. Per i brani meno noti può mancare o non essere a tempo.
- **Mac spento o irraggiungibile:** restano brano, minuti e tasti; mancano copertina, sfondo e testo.
- **Copertina in ritardo:** arriva un paio di secondi dopo il cambio brano.

## Note tecniche su questo Tab5

Scoperte durante la costruzione, utili se si aggiorna qualcosa:

- Il processore è un ESP32-P4 revisione 1.3: servono `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` e `CONFIG_ESP32P4_REV_MIN_100`.
- Il chip radio ESP32-C6 ha il firmware di fabbrica. È compatibile solo con `esp_hosted` 2.13 sul lato P4, non con la 3.x.
- Su questa revisione della scheda il reset del C6 vuole `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH`, al contrario di quanto indica il preset della libreria.
- Il collegamento con il C6 perde i pacchetti che arrivano a raffica. Per questo la finestra di ricezione TCP è tenuta a 2440 byte (`CONFIG_LWIP_TCP_WND_DEFAULT`): senza, i download scendono a circa 4 KB al secondo.
- Aprire la porta seriale USB riavvia il Tab5.

## Licenza

Codice sotto licenza [MIT](LICENSE). Il font Inter incluso in `firmware/components/ui/fonts/` è distribuito con la SIL Open Font License (vedi `Inter-LICENSE.txt` nella stessa cartella). Apple Music, iPhone, iPad e Mac sono marchi di Apple Inc.; questo progetto non è affiliato ad Apple.
