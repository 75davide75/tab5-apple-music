# Tab5 "In riproduzione" per Apple Music — progetto

Data: 2026-11-11

## A cosa serve

Un M5Stack Tab5 appoggiato sulla scrivania, davanti all'utente, che mostra il brano in riproduzione su Apple Music e permette di cambiare canzone senza prendere in mano il telefono e senza cambiare finestra sul Mac. L'uso è domestico. La riproduzione parte di solito da iPhone o iPad, a volte dal Mac. L'utente ascolta quasi sempre playlist proprie con riproduzione casuale attiva.

L'utente non ha mai programmato un ESP32: l'ambiente di sviluppo va installato da zero su questo Mac e il caricamento del firmware lo fa chi costruisce. All'utente si chiede solo di collegare il cavo USB-C e confermare l'abbinamento Bluetooth sui propri dispositivi.

## Obiettivi

1. Quando parte un brano su iPhone, iPad o Mac, il Tab5 si accende da solo e mostra la schermata "In riproduzione".
2. Quando non c'è musica, il Tab5 resta con lo schermo spento.
3. A sinistra: copertina, titolo, artista, barra di avanzamento con minuti trascorsi e rimanenti, tasti indietro / play-pausa / avanti funzionanti.
4. A destra: elenco dei brani (vedi "Colonna di destra").
5. Aspetto il più possibile vicino alla schermata "In riproduzione" dell'app Apple Music: sfondo sfumato con i colori della copertina, copertina grande con angoli arrotondati e ombra, tipografia pulita, testo bianco con livelli di opacità.

## Cosa non fa

- Non è uno schermo aggiuntivo del Mac.
- Non riproduce audio.
- Niente controllo del volume, ricerca, navigazione della libreria, testi.
- Non funziona fuori casa né con dispositivi fuori dal raggio Bluetooth.
- Non mostra la coda reale dell'iPhone quando la riproduzione casuale è attiva (Apple non la espone in nessun modo).

## Cosa lo renderebbe un fallimento

- Mostrare come "prossimi brani" un elenco che non corrisponde a ciò che suonerà. Con la casuale attiva l'elenco deve presentarsi come contenuto della playlist, non come coda.
- Richiedere all'utente azioni manuali ogni volta (riabbinare, riavviare, aprire app) per farlo funzionare.
- Restare acceso tutto il giorno senza musica.
- Usare loghi o marchi Apple, o il font San Francisco (la licenza non ne permette l'uso su hardware non Apple).

## Vincoli tecnici noti

- **Tab5:** ESP32-P4, schermo 5" 1280 × 720 usato in orizzontale, touch capacitivo, 16 MB flash, 32 MB PSRAM. Wi-Fi e Bluetooth LE passano per un ESP32-C6 secondario collegato via SDIO (ESP-Hosted). Solo Bluetooth LE, niente Bluetooth classico.
- **Apple non offre un servizio "cosa sta suonando sul mio Apple ID".** Le fonti disponibili sono due:
  - **iPhone / iPad:** Apple Media Service (AMS) su Bluetooth LE. Il Tab5 fa da periferica abbinata; iOS fornisce titolo, artista, album, durata, stato di riproduzione con tempo trascorso e velocità, posizione nella coda (indice e totale), modalità casuale e ripetizione. Accetta comandi play, pausa, brano successivo, brano precedente. **Non** fornisce copertina, titoli dei brani in coda, né un comando per saltare a un punto preciso del brano o a un brano specifico.
  - **Mac:** l'app Musica è leggibile e comandabile via scripting locale (brano corrente, posizione, playlist corrente e suoi brani, copertine, libreria con le playlist sincronizzate da iCloud). La lista "Prossimi" dell'app non è esposta.
- Il tempo trascorso arriva da AMS solo ai cambi di stato: il Tab5 lo fa avanzare localmente.

## Come è fatto (decisioni di chi progetta, delegate dall'utente)

L'utente ha lasciato tutte le scelte tecniche a chi costruisce. Quelle che seguono sono scelte mie.

### Tre parti

1. **Firmware Tab5** — ESP-IDF + LVGL, con il supporto scheda ufficiale M5Stack per il Tab5.
2. **Programma sul Mac ("compagno")** — processo in background che parte al login, senza finestra. Legge l'app Musica e risponde al Tab5 sulla rete Wi-Fi di casa. Si annuncia via Bonjour/mDNS, così il Tab5 lo trova senza configurare indirizzi.
3. **Configurazione** — nome e password del Wi-Fi in un file di configurazione non versionato, inserito nel firmware alla compilazione.

### Disponibilità del Mac

L'utente conferma che il Mac è quasi sempre acceso e sveglio quando ascolta dalla scrivania. Quindi il Tab5 **non** memorizza playlist né copertine al suo interno: senza Mac vale il comportamento ridotto descritto più sotto, tenuto al minimo.

### App Musica sul Mac

L'utente accetta che il compagno apra l'app Musica sul Mac e la lasci aperta nel Dock (senza portarla in primo piano e senza riprodurre nulla) ogni volta che serve interrogarla. Non serve quindi leggere la libreria per altre vie.

### Flusso dei dati

- **iPhone/iPad → Tab5 (Bluetooth LE, AMS):** brano, tempi, stato, indice/totale coda, casuale sì/no. Tab5 → iPhone: play/pausa, avanti, indietro.
- **Tab5 → compagno Mac (Wi-Fi):** "sta suonando *titolo / artista / album*, brano N di M, casuale sì/no". Il compagno risponde con: copertina del brano, playlist individuata, elenco brani con miniature.
- **Riproduzione dal Mac:** il compagno comunica direttamente brano, tempi e stato; i tasti del Tab5 passano dal compagno.
- **Fonte attiva:** quella che ha iniziato a suonare per ultima.

### Individuare la playlist

Il compagno cerca tra le playlist dell'utente quelle che contengono il brano e hanno M brani. Senza casuale deve anche coincidere la posizione N. A parità, vince quella riprodotta più di recente; la scelta resta fissa finché M non cambia e i brani successivi continuano ad appartenerle. Se nessuna playlist corrisponde (album, radio, brano singolo), la colonna di destra mostra solo "brano N di M".

### Colonna di destra

| Situazione | Intestazione | Elenco |
|---|---|---|
| Playlist, casuale attiva | Nome playlist · "brano N di M" · etichetta "Casuale" | Tutti i brani della playlist nell'ordine originale; brano in corso evidenziato; brani già ascoltati in questa sessione attenuati. Scorre col dito. |
| Playlist, senza casuale | "Prossimi brani" · nome playlist | I brani dalla posizione N+1 in avanti, in ordine. |
| Nessuna playlist trovata, o Mac non raggiungibile | "brano N di M" | Nessun elenco. |

Le righe non sono toccabili per far partire un brano, con nessuna fonte: con iPhone/iPad AMS non lo consente, e con il Mac si rinuncia per semplicità e coerenza (scelta mia; l'utente non l'ha chiesto).

### Audio che non è Apple Music

AMS segnala qualunque app stia riproducendo audio sull'iPhone (video, podcast, altre app musicali). Il Tab5 reagisce **solo** quando il lettore segnalato è l'app Musica; per tutto il resto resta in standby (scelta mia, coerente con la richiesta "la mia musica su Apple Music"). La prova Bluetooth deve verificare che il nome del lettore fornito da AMS basti a distinguerla, tenendo conto che è localizzato ("Musica" / "Music").

### Copertina e sfondo

Il compagno prepara le immagini già nella dimensione e nel formato più comodi per il Tab5 (copertina grande circa 400 × 400, miniature circa 52 × 52, e i colori o l'immagine sfocata per lo sfondo). Se il Mac non è raggiungibile, il Tab5 cerca da solo la copertina sul catalogo pubblico iTunes Search usando titolo, artista e album; se anche questo fallisce, mostra un segnaposto neutro.

### Barra di avanzamento

Mostra minuti trascorsi a sinistra e minuti rimanenti a destra. Con fonte iPhone/iPad è **solo indicatore**: AMS non permette di spostarsi a un punto preciso. Con fonte Mac può essere trascinata.

### Standby

- Schermo spento (retroilluminazione a zero) quando nessuna fonte sta riproducendo.
- Si accende appena una fonte passa a "in riproduzione".
- In pausa resta acceso 3 minuti, poi si spegne.
- Un tocco sullo schermo spento lo riaccende mostrando l'ultimo brano, per 30 secondi.
- Si assume alimentazione via USB alla scrivania: Wi-Fi e Bluetooth restano attivi in standby, non serve risparmio batteria spinto.

### Aspetto

Riferimento: mockup approvato dall'utente in `.superpowers/brainstorm/8640-1794437360/content/now-playing.html`. Colonna sinistra circa un terzo della larghezza, colonna destra il resto. Interfaccia in italiano. Font: Inter (o equivalente libero vicino a San Francisco). Icone disegnate in proprio.

## Ordine di costruzione

1. **Prova Bluetooth (da fare per prima, è il rischio principale).** Firmware minimo che: si abbina all'iPhone, mostra a schermo titolo, artista, stato, tempo trascorso, indice e totale coda, e invia play/pausa e avanti. Da verificare esplicitamente: che il Bluetooth LE del Tab5 via ESP-Hosted regga abbinamento con cifratura; che il Tab5 si ricolleghi da solo dopo un riavvio e quando l'iPhone torna nel raggio; che l'app Musica popoli davvero indice e totale coda. Se la prova fallisce ci si ferma e si torna dall'utente con le alternative.
2. Schermata completa con dati da iPhone, copertina dal catalogo pubblico.
3. Compagno Mac: copertine, playlist, colonna di destra.
4. Riproduzione dal Mac come seconda fonte.
5. Standby e rifiniture grafiche.

## Lasciato a chi costruisce

- Linguaggio del compagno Mac (preferenza: Swift, nessuna dipendenza da installare) e protocollo tra Tab5 e compagno.
- Formato delle immagini e modo di ottenere lo sfondo sfumato.
- Se tenere collegati iPhone e iPad contemporaneamente o uno alla volta; entrambi devono comunque poter essere abbinati.
- Struttura del codice, test, gestione degli errori di rete.
