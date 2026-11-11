# Tab5 "In riproduzione" per Apple Music — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Un M5Stack Tab5 che si accende da solo quando parte un brano su Apple Music (iPhone, iPad o Mac) e mostra copertina, avanzamento, comandi e l'elenco dei brani della playlist.

**Architecture:** Il firmware del Tab5 riceve i dati di riproduzione da iPhone/iPad come periferica Bluetooth LE (Apple Media Service) e li mostra con LVGL. Un programma Swift in background sul Mac ("compagno"), scoperto via mDNS, fornisce via HTTP copertine, playlist e lo stato dell'app Musica del Mac. La logica pura (parsing AMS, orologio di riproduzione, standby, scelta della fonte, abbinamento playlist) vive in moduli senza dipendenze hardware, testati sul Mac.

**Tech Stack:** ESP-IDF 5.x (versione stabile più recente supportata dal componente `espressif/m5stack_tab5`), LVGL 9, NimBLE host, ESP-Hosted (ESP32-C6), Swift 6 / Swift Package Manager, ScriptingBridge verso Music.app, LaunchAgent.

**Spec:** `docs/superpowers/specs/2026-11-11-tab5-apple-music-design.md`

## Global Constraints

- Schermo 1280 × 720 in orizzontale. Interfaccia in italiano.
- Font Inter. Nessun logo o marchio Apple, nessun font San Francisco. Icone disegnate in proprio.
- Il Tab5 reagisce solo quando il lettore segnalato da AMS è l'app Musica (nomi accettati: `Music`, `Musica`).
- Le righe dell'elenco non sono toccabili per avviare un brano, con nessuna fonte.
- Barra di avanzamento: solo indicatore con fonte iPhone/iPad; trascinabile con fonte Mac.
- Standby: schermo spento senza riproduzione; in pausa resta acceso 3 minuti; un tocco a schermo spento lo riaccende 30 secondi.
- Fonte attiva: quella passata a "in riproduzione" per ultima.
- Con casuale attiva l'elenco è intitolato con il nome della playlist e l'etichetta "Casuale", mai "Prossimi brani".
- Wi-Fi: SSID e password in `firmware/main/wifi_secrets.h`, non versionato.
- Il Tab5 non memorizza playlist né copertine su flash.
- L'utente non ha mai usato ESP-IDF: ogni passo che richiede un suo gesto (collegare il cavo, confermare l'abbinamento, concedere il permesso Automazione sul Mac) va chiesto esplicitamente e in italiano.
- Mockup di riferimento: `.superpowers/brainstorm/8640-1794437360/content/now-playing.html`.

## Review Focus

1. Titoli o artisti con virgole, emoji, caratteri accentati o più lunghi di 200 byte (AMS li tronca e lo segnala con un flag): il testo va mostrato senza corruzione, con puntini se troncato. → Task 2.
2. Brano senza durata (radio, live) o durata 0: la barra resta vuota e i tempi mostrano `--:--`, nessuna divisione per zero. → Task 2.
3. Il compagno Mac sparisce a metà sessione (Mac in stop): la parte sinistra continua a funzionare, la destra passa a "brano N di M", e si riprende da sola quando il Mac torna. → Task 6.
4. Due playlist con lo stesso numero di brani che contengono lo stesso brano: la scelta è deterministica e non cambia a ogni brano. → Task 5.
5. iPhone che esce dal raggio Bluetooth mentre suona: il Tab5 non resta acceso con il tempo che continua a scorrere; dopo la disconnessione la fonte è considerata ferma. → Task 3.

## File Structure

```
firmware/
  CMakeLists.txt, sdkconfig.defaults, partitions.csv
  main/            app_main.c, wifi_secrets.h (ignorato), wifi_secrets.example.h
  components/
    core/          logica pura, nessun include ESP-IDF
      ams_parse.{c,h}      decodifica notifiche AMS
      playback_clock.{c,h} tempo trascorso calcolato localmente
      standby.{c,h}        macchina a stati dello schermo
      source_arbiter.{c,h} scelta della fonte attiva
      model.h              struct now_playing_t condivisa
    ams_ble/       periferica NimBLE + client AMS
    companion/     mDNS + client HTTP verso il Mac, fallback iTunes Search
    ui/            schermata LVGL, font, icone
  host_tests/      CMake nativo che compila components/core e i test
companion/
  Package.swift
  Sources/CompanionCore/   PlaylistMatcher, modelli, ImagePrep
  Sources/tab5-companion/  MusicBridge (ScriptingBridge), HTTPServer, main
  Tests/CompanionCoreTests/
  launchd/com.tab5music.companion.plist, install.sh
```

---

### Task 1: Ambiente e prova Bluetooth (CANCELLO)

Se questo task fallisce ci si ferma e si torna dall'utente con le alternative. Nessun altro task parte prima.

**Files:**
- Create: `.gitignore`, `firmware/CMakeLists.txt`, `firmware/sdkconfig.defaults`, `firmware/main/app_main.c`, `firmware/main/CMakeLists.txt`, `firmware/main/idf_component.yml`, `firmware/components/ams_ble/{ams_ble.c,ams_ble.h,CMakeLists.txt}`

**Interfaces:**
- Produces (in `ams_ble.h`):
  - `typedef void (*ams_raw_cb_t)(uint8_t entity, uint8_t attr, uint8_t flags, const char *value, size_t len);`
  - `typedef void (*ams_conn_cb_t)(bool connected);`
  - `esp_err_t ams_ble_start(ams_raw_cb_t on_update, ams_conn_cb_t on_conn);`
  - `esp_err_t ams_ble_send_command(uint8_t cmd);` con `AMS_CMD_PLAY=0, AMS_CMD_PAUSE=1, AMS_CMD_TOGGLE=2, AMS_CMD_NEXT=3, AMS_CMD_PREV=4`

Dati AMS da usare:
- Servizio `89D3502B-0F36-433A-8EF4-C502AD55F8DC`; Remote Command `9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2`; Entity Update `2F7BABE3-0749-4C39-B70C-8CF50A4E4F4B`; Entity Attribute `C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7`.
- Il Tab5 è periferica GAP: pubblicizza con *service solicitation* per il servizio AMS, nome `Tab5 Music`. Dopo la connessione richiede sicurezza (bonding, LE Secure Connections), poi scopre AMS sul telefono come client GATT.
- Iscrizione: scrivere su Entity Update `[entity, attr...]` per Player(0): 0,1 — Queue(1): 0,1,2 — Track(2): 0,1,2,3; poi abilitare le notifiche.
- Notifica: byte 0 entity, byte 1 attr, byte 2 flags (bit 0 = troncato), resto UTF-8.

- [ ] **Step 1:** `git init`; `.gitignore` con `.superpowers/`, `firmware/build/`, `firmware/managed_components/`, `firmware/sdkconfig`, `firmware/main/wifi_secrets.h`, `companion/.build/`. Commit di spec e piano.
- [ ] **Step 2:** Installare ESP-IDF in `~/esp/esp-idf` (clone del tag stabile scelto, `./install.sh esp32p4`). Verifica: `. ~/esp/esp-idf/export.sh && idf.py --version` stampa la versione.
- [ ] **Step 3:** Creare il progetto con target `esp32p4`, dipendenze `espressif/m5stack_tab5`, `espressif/esp_hosted`, `espressif/esp_wifi_remote`. In `sdkconfig.defaults`: NimBLE host abilitato con trasporto verso il controller del C6 via ESP-Hosted, ruolo periferica, bonding con persistenza su NVS, PSRAM abilitata. Verifica: `idf.py build` termina senza errori.
- [ ] **Step 4:** Chiedere all'utente di collegare il Tab5 via USB-C. Verifica: compare una porta `/dev/cu.usbmodem*`; `idf.py -p <porta> flash monitor` mostra il log di avvio.
- [ ] **Step 5:** Implementare `ams_ble` secondo l'interfaccia sopra. `app_main` accende lo schermo con il BSP e mostra con etichette LVGL di default: stato connessione, nome lettore, titolo, artista, PlaybackInfo grezzo, indice/totale coda, modalità casuale; due pulsanti a schermo inviano `AMS_CMD_TOGGLE` e `AMS_CMD_NEXT`.
- [ ] **Step 6: Verifica sull'hardware con l'utente.** Chiedergli di aprire Impostazioni → Bluetooth sull'iPhone, toccare `Tab5 Music`, confermare l'abbinamento, poi far partire un brano da una playlist con casuale attiva. Deve risultare, annotando l'esito di ciascun punto nel messaggio di commit:
  1. l'abbinamento riesce e il log mostra collegamento cifrato;
  2. titolo e artista a schermo corrispondono;
  3. PlaybackInfo cambia a pausa/ripresa;
  4. indice e totale coda sono valorizzati e il totale coincide con il numero di brani della playlist;
  5. il nome del lettore è `Musica` o `Music`, e cambia aprendo un video in un'altra app;
  6. i due pulsanti comandano l'iPhone;
  7. dopo `idf.py monitor` → reset del Tab5, si ricollega da solo entro 30 secondi senza toccare l'iPhone;
  8. spegnendo e riaccendendo il Bluetooth dell'iPhone, si ricollega da solo.
- [ ] **Step 7:** Se il firmware di fabbrica del C6 non espone il controller Bluetooth (l'host NimBLE non si sincronizza), tentare l'aggiornamento del C6 con il firmware slave ESP-Hosted con BT abilitato, seguendo la documentazione M5Stack per il Tab5. Se anche così i punti 1, 2 o 7 falliscono: **fermarsi** e riferire all'utente cosa non ha funzionato. Se fallisce solo il punto 4, proseguire e riferire che la colonna di destra dovrà abbinare la playlist senza il totale.
- [ ] **Step 8:** Commit `feat: BLE AMS spike on Tab5`.

---

### Task 2: Logica pura — parsing AMS e orologio di riproduzione

**Files:**
- Create: `firmware/components/core/{model.h,ams_parse.c,ams_parse.h,playback_clock.c,playback_clock.h,CMakeLists.txt}`, `firmware/host_tests/{CMakeLists.txt,test_ams_parse.c,test_playback_clock.c,run.sh}`

**Interfaces:**
- Produces:
  ```c
  typedef enum { PB_STOPPED, PB_PAUSED, PB_PLAYING } pb_state_t;
  typedef struct {
    char title[256], artist[256], album[256], player[64];
    bool title_truncated, artist_truncated;
    float duration_s;            // <=0 = sconosciuta
    pb_state_t state; float rate; float elapsed_at_update_s; int64_t update_ms;
    int queue_index, queue_count; // -1 = sconosciuto; index a base 0 come da AMS
    bool shuffle;
    uint32_t track_seq;          // incrementato a ogni cambio di titolo/artista/album
  } now_playing_t;
  // ritorna true se np è cambiato
  bool ams_apply_update(now_playing_t *np, uint8_t entity, uint8_t attr, uint8_t flags,
                        const char *value, size_t len, int64_t now_ms);
  bool ams_player_is_music(const now_playing_t *np);
  float playback_elapsed_s(const now_playing_t *np, int64_t now_ms); // limitato a [0, duration]
  void format_mmss(float seconds, char out[8]);                       // "--:--" se <0 o NaN
  ```

- [ ] **Step 1: Test che falliscono** in `test_ams_parse.c`:
  - `test_playback_info`: valore `"1,1.0,83.5"` → `state==PB_PLAYING`, `rate==1.0`, `elapsed_at_update_s==83.5`, `update_ms==now`.
  - `test_playback_info_paused`: `"0,0.0,12"` → `PB_PAUSED`.
  - `test_playback_info_malformed`: `""`, `"1"`, `"x,y,z"` → ritorna false, stato invariato.
  - `test_title_utf8_commas`: titolo `"Città, vuota 🎧"` copiato intatto.
  - `test_title_truncated_flag`: flags=1 → `title_truncated==true`.
  - `test_title_overlong`: 400 byte di UTF-8 multibyte → troncato su confine di carattere, terminato da NUL.
  - `test_track_seq_increments`: cambio titolo incrementa `track_seq`; stesso titolo ripetuto no.
  - `test_queue`: Queue Index `"13"`, Count `"42"`, Shuffle `"1"` → 13, 42, true; Shuffle `"0"` → false.
  - `test_duration_empty`: Duration `""` → `duration_s<=0`.
  - `test_player_is_music`: `"Musica"` e `"Music"` → true; `"YouTube"`, `""` → false.
- [ ] **Step 2: Test che falliscono** in `test_playback_clock.c`:
  - in riproduzione, `elapsed_at_update_s=10`, 5000 ms dopo → 15.0;
  - in pausa → resta 10.0;
  - oltre la durata (durata 20, 60 s dopo) → 20.0;
  - durata sconosciuta → continua a crescere senza limite superiore;
  - `format_mmss(84)` → `"1:24"`, `format_mmss(3725)` → `"62:05"`, `format_mmss(-1)` → `"--:--"`.
- [ ] **Step 3:** `firmware/host_tests/run.sh` configura e compila con il CMake nativo ed esegue i test. Eseguire: FAIL per simboli mancanti.
- [ ] **Step 4:** Implementare. Eseguire `firmware/host_tests/run.sh`: tutti PASS.
- [ ] **Step 5:** Commit `feat: AMS parsing and playback clock`.

---

### Task 3: Logica pura — standby e scelta della fonte

**Files:**
- Create: `firmware/components/core/{standby.c,standby.h,source_arbiter.c,source_arbiter.h}`, `firmware/host_tests/{test_standby.c,test_source_arbiter.c}`

**Interfaces:**
- Produces:
  ```c
  typedef struct standby standby_t;   // opaco, allocato dal chiamante
  void standby_init(standby_t *s, int64_t now_ms);
  void standby_on_playback(standby_t *s, pb_state_t state, int64_t now_ms);
  void standby_on_touch(standby_t *s, int64_t now_ms);
  bool standby_screen_on(const standby_t *s, int64_t now_ms);
  #define STANDBY_PAUSE_TIMEOUT_MS 180000
  #define STANDBY_TOUCH_TIMEOUT_MS  30000

  typedef enum { SRC_NONE, SRC_IOS, SRC_MAC } source_t;
  typedef struct source_arbiter source_arbiter_t;
  void arbiter_init(source_arbiter_t *a);
  void arbiter_report(source_arbiter_t *a, source_t src, pb_state_t state, int64_t now_ms);
  void arbiter_disconnected(source_arbiter_t *a, source_t src); // equivale a PB_STOPPED
  source_t arbiter_active(const source_arbiter_t *a);
  ```
- Regola dell'arbitro: è attiva la fonte passata a `PB_PLAYING` per ultima; se quella si ferma e l'altra sta suonando, diventa attiva l'altra; se nessuna suona, resta l'ultima attiva finché è in pausa, altrimenti `SRC_NONE`.

- [ ] **Step 1: Test che falliscono:**
  - standby: all'avvio schermo spento; `PB_PLAYING` → acceso; `PB_PAUSED` → acceso a 179 999 ms, spento a 180 000 ms; ripresa prima della scadenza azzera il timer; `PB_STOPPED` → spento subito; tocco a schermo spento → acceso per 30 000 ms poi spento; tocco durante la riproduzione non introduce scadenze.
  - arbitro: iOS suona → `SRC_IOS`; poi Mac suona → `SRC_MAC`; Mac in pausa mentre iOS suona ancora → `SRC_IOS`; entrambe in pausa → resta l'ultima attiva; `arbiter_disconnected(SRC_IOS)` mentre era attiva e suonava → `SRC_NONE` (o `SRC_MAC` se il Mac suona).
- [ ] **Step 2:** Eseguire `run.sh`: FAIL.
- [ ] **Step 3:** Implementare. Eseguire: PASS.
- [ ] **Step 4:** Commit `feat: standby state machine and source arbiter`.

---

### Task 4: Schermata "In riproduzione" con dati da iPhone

**Files:**
- Create: `firmware/components/ui/{ui.c,ui.h,ui_theme.h,icons.c,CMakeLists.txt}`, font Inter convertiti (`inter_20.c`, `inter_24.c`, `inter_28_semibold.c`) con i glifi Latin-1 + Latin Extended-A
- Modify: `firmware/main/app_main.c` (sostituisce la schermata di prova)

**Interfaces:**
- Consumes: `now_playing_t`, `playback_elapsed_s`, `format_mmss`, `standby_*`, `arbiter_*`, `ams_ble_*`.
- Produces (in `ui.h`), tutte da chiamare con il lock LVGL del BSP:
  ```c
  typedef enum { UI_CMD_TOGGLE, UI_CMD_NEXT, UI_CMD_PREV, UI_CMD_SEEK } ui_cmd_t;
  typedef void (*ui_cmd_cb_t)(ui_cmd_t cmd, float seek_s);
  void ui_init(ui_cmd_cb_t on_cmd);
  void ui_set_now_playing(const now_playing_t *np, source_t src);
  void ui_tick(float elapsed_s, float duration_s);            // chiamata ogni 250 ms
  void ui_set_cover(const uint8_t *rgb565, int w, int h);      // NULL = segnaposto
  void ui_set_background(const uint8_t *rgb565, int w, int h); // NULL = sfondo scuro neutro
  typedef struct { uint32_t id; const char *title, *artist; float duration_s; bool played, current; } ui_row_t;
  void ui_set_list(const char *header, const char *subheader, bool shuffle_badge,
                   const ui_row_t *rows, int n);               // n==0 = nessun elenco
  void ui_set_thumb(uint32_t row_id, const uint8_t *rgb565);   // 52×52
  typedef void (*ui_thumb_req_cb_t)(uint32_t row_id);          // riga diventata visibile
  void ui_on_thumb_needed(ui_thumb_req_cb_t cb);
  ```

Layout (dal mockup): margini 44 px orizzontali e 43 px verticali; colonna sinistra larga 397 px con copertina 397 × 397, raggio 13 px, ombra; titolo Inter Semibold 28 bianco su una riga con ellissi; artista Inter 24 bianco al 62%; barra alta 7 px, fondo bianco 22%, riempimento bianco 85%; tempi Inter 20 bianco 55%, a sinistra il trascorso, a destra il rimanente con segno meno; tre tasti centrati nello spazio residuo, area di tocco minima 96 × 96 px. Colonna destra a 46 px dalla sinistra: intestazione Inter Bold 24, sottotitolo Inter 20 al 60%, etichetta "Casuale" a pillola; righe alte 68 px con miniatura 52 × 52, titolo e artista; riga corrente con fondo bianco 14%; righe `played` con opacità 40%. Sopra lo sfondo un velo nero al 28%.

- [ ] **Step 1:** Implementare `ui` e collegarla in `app_main`: un task a 250 ms calcola `playback_elapsed_s`, chiama `ui_tick`, e imposta la retroilluminazione secondo `standby_screen_on`. Gli aggiornamenti AMS passano da `ams_apply_update`; se `ams_player_is_music` è falso, la fonte iOS è riportata all'arbitro come `PB_STOPPED`. `on_conn(false)` chiama `arbiter_disconnected(SRC_IOS)`. Con `SRC_IOS` la barra non accetta trascinamento. Con `duration_s<=0` barra vuota e tempi `--:--`.
- [ ] **Step 2:** `idf.py build` senza errori né warning nei componenti del progetto.
- [ ] **Step 3: Verifica sull'hardware con l'utente** (inviargli una foto o chiedergli conferma): schermo spento a riposo; si accende entro 2 secondi dall'avvio di un brano; titolo, artista e tempi corretti e allineati con l'iPhone entro 1 secondo; i tre tasti funzionano; in pausa si spegne dopo 3 minuti; un tocco lo riaccende per 30 secondi; avviando un video su un'altra app resta spento; allontanando l'iPhone fuori portata lo schermo si spegne.
- [ ] **Step 4:** Commit `feat: now-playing screen driven by iPhone`.

---

### Task 5: Compagno Mac — nucleo e abbinamento playlist

**Files:**
- Create: `companion/Package.swift`, `companion/Sources/CompanionCore/{Models.swift,PlaylistMatcher.swift,ImagePrep.swift}`, `companion/Tests/CompanionCoreTests/{PlaylistMatcherTests.swift,ImagePrepTests.swift}`

**Interfaces:**
- Produces:
  ```swift
  public struct Track: Codable, Equatable { public let id: UInt32; public let title, artist, album: String; public let durationS: Double }
  public struct Playlist { public let id: String; public let name: String; public let tracks: [Track]; public let lastPlayed: Date? }
  public struct Query { public let title, artist, album: String; public let index: Int?; public let count: Int?; public let shuffle: Bool }
  public struct Match { public let playlist: Playlist; public let currentPosition: Int }
  public final class PlaylistMatcher {
    public init()
    public func match(_ q: Query, in playlists: [Playlist]) -> Match?   // mantiene la scelta precedente
  }
  public enum ImagePrep {
    public static func rgb565(_ image: CGImage, width: Int, height: Int) -> Data          // little endian, riga per riga
    public static func blurredBackground(_ image: CGImage) -> Data                        // 160×90 RGB565
  }
  ```
- Regole di `match`: confronto di titolo e artista senza distinzione di maiuscole e di segni diacritici, spazi ai bordi rimossi. Candidata = playlist che contiene il brano e, se `count` è noto, ha esattamente `count` brani. Senza casuale e con `index` noto, il brano deve trovarsi alla posizione `index`. Tra più candidate: prima quella scelta alla chiamata precedente se ancora candidata, poi `lastPlayed` più recente, poi `id` in ordine alfabetico. Nessuna candidata → `nil`.

- [ ] **Step 1: Test che falliscono** in `PlaylistMatcherTests`:
  - una sola playlist di 42 brani con il brano → trovata, `currentPosition` corretto;
  - `count` diverso dal numero di brani → `nil`;
  - senza casuale, brano presente ma a posizione diversa da `index` → `nil`;
  - con casuale, la posizione è ignorata;
  - due candidate identiche per numero di brani: vince `lastPlayed` più recente; alla chiamata successiva con un altro brano presente in entrambe resta la stessa anche invertendo `lastPlayed`;
  - `"CITTÀ vuota "` trova `"Città Vuota"`;
  - brano in nessuna playlist → `nil`;
  - `count == nil` (totale coda non disponibile): accetta qualunque dimensione.
- [ ] **Step 2: Test che falliscono** in `ImagePrepTests`: immagine 2 × 2 con rosso, verde, blu, bianco puri → byte `00 F8`, `E0 07`, `1F 00`, `FF FF`; `blurredBackground` restituisce esattamente 160·90·2 byte.
- [ ] **Step 3:** `cd companion && swift test`: FAIL in compilazione.
- [ ] **Step 4:** Implementare. `swift test`: PASS.
- [ ] **Step 5:** Commit `feat: companion core with playlist matcher`.

---

### Task 6: Compagno Mac — servizio, e colonna di destra sul Tab5

**Files:**
- Create: `companion/Sources/tab5-companion/{main.swift,MusicBridge.swift,HTTPServer.swift}`, `companion/launchd/com.tab5music.companion.plist`, `companion/install.sh`, `firmware/components/companion/{companion.c,companion.h,itunes_fallback.c,CMakeLists.txt}`, `firmware/main/wifi_secrets.example.h`
- Modify: `firmware/main/app_main.c`

**Interfaces:**
- Protocollo HTTP, porta 8787, annunciato via Bonjour come `_tab5music._tcp`:
  - `GET /v1/resolve?title=&artist=&album=&index=&count=&shuffle=0|1` → `{"playlist":{"name":String,"count":Int}|null,"current":Int,"track_id":UInt32|null,"tracks":[{"id","title","artist","duration_s"}]}`. Senza casuale `tracks` contiene solo i brani dopo la posizione corrente; con casuale tutti.
  - `GET /v1/art/{track_id}?size=400|52` → `application/octet-stream`, RGB565 little endian `size×size`; 404 se senza copertina.
  - `GET /v1/bg/{track_id}` → RGB565 160 × 90.
  - `GET /v1/mac/state` e `POST /v1/mac/command`: definiti nel Task 7; qui rispondono 404.
- `MusicBridge` (ScriptingBridge verso `com.apple.Music`, avviata senza portarla in primo piano): `func playlists() -> [Playlist]` (solo playlist dell'utente, cache invalidata ogni 60 s), `func artwork(trackID: UInt32) -> CGImage?`.
- Produces (in `companion.h`):
  ```c
  esp_err_t companion_start(void);                 // Wi-Fi + ricerca mDNS, riprova ogni 10 s
  bool companion_available(void);
  // asincrone: il risultato arriva sulla callback, da un task di rete
  void companion_resolve(const now_playing_t *np, void (*cb)(const companion_list_t *list_or_null));
  void companion_fetch_art(uint32_t track_id, int size, void (*cb)(uint32_t id, int size, uint8_t *rgb565_or_null));
  void companion_fetch_bg(uint32_t track_id, void (*cb)(uint8_t *rgb565_or_null));
  void itunes_fetch_cover(const now_playing_t *np, void (*cb)(uint8_t *rgb565_400_or_null));
  ```
  `companion_list_t` contiene nome playlist, `current`, `track_id`, e l'array di righe (`id`, `title`, `artist`, `duration_s`). I buffer immagine sono allocati in PSRAM e passano di proprietà alla callback.

- [ ] **Step 1:** Implementare servizio e `MusicBridge`. `install.sh` compila in release, copia il binario in `~/Library/Application Support/tab5-companion/`, installa il plist in `~/Library/LaunchAgents` con `RunAtLoad` e `KeepAlive`, e lo carica.
- [ ] **Step 2: Verifica sul Mac.** Eseguire il binario a mano; avvisare l'utente che comparirà una richiesta di permesso "controllare Musica" da accettare. Poi `curl 'http://localhost:8787/v1/resolve?title=<brano reale>&artist=<artista>&album=&count=<n>&shuffle=1'` restituisce la playlist giusta; `curl -o /tmp/a.bin http://localhost:8787/v1/art/<id>?size=400` produce un file di 320 000 byte; `dns-sd -B _tab5music._tcp` elenca il servizio.
- [ ] **Step 3:** Implementare `companion` nel firmware. A ogni cambio di `track_seq`: `companion_resolve`; con risposta valida chiamare `ui_set_list` (intestazione nome playlist + "brano N di M" + etichetta Casuale se casuale; altrimenti "Prossimi brani" + nome playlist), poi copertina e sfondo; segnare `played` i brani già passati come correnti in questa sessione (insieme azzerato quando cambia playlist). Miniature richieste solo per le righe visibili, cache LRU di 64 in PSRAM. Se il compagno non è disponibile o la richiesta scade in 2 s: `ui_set_list("brano N di M", ...)` con `n=0` e copertina tramite `itunes_fetch_cover` (`https://itunes.apple.com/search?entity=song&limit=5&term=<titolo artista>`, primo risultato con artista coincidente, `artworkUrl100` con `100x100` sostituito da `400x400`, JPEG decodificato in RGB565); se fallisce, segnaposto. Una risposta arrivata per un `track_seq` ormai superato va scartata.
- [ ] **Step 4:** L'utente fornisce SSID e password: scriverli in `wifi_secrets.h` senza riportarli in chat né nei commit. `idf.py build flash`.
- [ ] **Step 5: Verifica sull'hardware con l'utente:** con casuale attiva compaiono copertina, sfondo colorato e l'elenco della playlist con il brano corrente evidenziato; dopo due "avanti" i brani precedenti sono attenuati; senza casuale l'intestazione è "Prossimi brani" e il primo in elenco è davvero il successivo; chiudendo il compagno (`launchctl bootout`) al brano successivo la destra mostra solo "brano N di M" e la copertina arriva comunque; riavviando il compagno l'elenco torna entro due brani.
- [ ] **Step 6:** Eseguire `install.sh` (non serve riavviare il Mac) e verificare con `launchctl print gui/$(id -u)/com.tab5music.companion` che sia in esecuzione.
- [ ] **Step 7:** Commit `feat: Mac companion service and playlist column`.

---

### Task 7: Il Mac come seconda fonte

**Files:**
- Modify: `companion/Sources/tab5-companion/{MusicBridge.swift,HTTPServer.swift}`, `firmware/components/companion/{companion.c,companion.h}`, `firmware/main/app_main.c`
- Create: `companion/Sources/CompanionCore/MacState.swift`, `companion/Tests/CompanionCoreTests/MacStateTests.swift`

**Interfaces:**
- `GET /v1/mac/state` → `{"state":"playing"|"paused"|"stopped","title","artist","album","duration_s","elapsed_s","index":Int|null,"count":Int|null,"shuffle":Bool,"track_id":UInt32|null}`.
- `POST /v1/mac/command` corpo `{"cmd":"toggle"|"next"|"prev"|"seek","position_s":Double?}` → 204.
- `MacState.encode(...) -> Data` in `CompanionCore` produce il JSON sopra.
- Produces: `void companion_poll_mac(void (*cb)(const now_playing_t *np_or_null));` interroga ogni 1000 ms mentre il compagno è disponibile; `void companion_mac_command(ui_cmd_t cmd, float seek_s);`

- [ ] **Step 1: Test che fallisce** `MacStateTests`: stato in riproduzione codificato con tutte le chiavi; stato fermo ha `"state":"stopped"` e `track_id` nullo; `position_s` negativa o oltre la durata in un comando `seek` viene limitata a `[0, duration]`.
- [ ] **Step 2:** `swift test`: FAIL. Implementare. `swift test`: PASS.
- [ ] **Step 3:** Firmware: lo stato del Mac alimenta un secondo `now_playing_t` e `arbiter_report(SRC_MAC, ...)`; tre interrogazioni fallite di seguito → `arbiter_disconnected(SRC_MAC)`. La schermata mostra la fonte restituita da `arbiter_active`. Con `SRC_MAC` i tasti e il trascinamento della barra passano da `companion_mac_command`; con `SRC_IOS` da `ams_ble_send_command`.
- [ ] **Step 4: Verifica sull'hardware con l'utente:** brano avviato dal Mac → il Tab5 si accende e lo mostra; trascinando la barra il brano salta a quel punto; avviando poi un brano dall'iPhone il Tab5 passa all'iPhone e la barra non è più trascinabile; mettendo in pausa l'iPhone mentre il Mac suona torna al Mac.
- [ ] **Step 5:** Commit `feat: Mac playback as second source`.

---

### Task 8: Rifinitura e consegna

**Files:**
- Modify: `firmware/components/ui/ui.c`
- Create: `README.md`

- [ ] **Step 1:** Confrontare una foto dello schermo con il mockup e correggere spaziature, opacità e dissolvenza della copertina al cambio brano (dissolvenza incrociata di 300 ms; lo schermo si accende e si spegne con rampa di luminosità di 400 ms).
- [ ] **Step 2:** Abbinare l'iPad dell'utente e verificare che, con iPhone fermo, un brano avviato dall'iPad accenda il Tab5.
- [ ] **Step 3:** `README.md` in italiano: cosa fa, come si abbina un nuovo dispositivo, come cambiare Wi-Fi e ricaricare il firmware, come disinstallare il compagno, limiti noti (coda reale non disponibile con casuale, barra non trascinabile da iPhone, solo app Musica).
- [ ] **Step 4:** Prova di una giornata: lasciare il Tab5 acceso e chiedere all'utente, il giorno dopo, se si è acceso a ogni ascolto e se è rimasto spento senza musica.
- [ ] **Step 5:** Commit `docs: README and polish`.
