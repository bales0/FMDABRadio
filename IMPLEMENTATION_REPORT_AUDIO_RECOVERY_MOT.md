# DAB audio activation/recovery a MOT — 2026-11-02

Navazující schválená SLS etapa mění zde popsanou původní ochranu cache pouze
segmentem 0. Aktuální lifecycle a výsledky: SLS_MOT_IMPLEMENTATION_REPORT_CZ.md.

## Výchozí stav a rozsah

Skutečný lokální HEAD: `1f196446e96488f6188840b5fb3134b55c892ee9`, shodný se
zadáním. Počáteční worktree byl čistý, rozdíl proti uvedenému SHA žádný.
Srovnávací revize: `28097fdd1ed8dbc6414f21884738896c6db230d5`.
Změny zůstávají necommitnuté. Žádný push, flash, připojení COM ani zápis do
EEPROM při práci/testech. Starší původní zadání CTS/SLS/EPG již nejsou na
dříve uvedených cestách ani v tomto HEAD; návaznost byla ověřena proti kódu,
historii, zachovaným implementačním zprávám a pokynům z konverzace.

Zachovány BusConfig (Si4684 10 MHz), TFT/I²C takty a clock script, GPIO/CS/IRQ,
PWREN/reset a firmware adresy, EEPROM, tlačítka a dekodéry. Beze změny MOT
arena 51200 B a decoder workspace 76800 B. Žádné nové runtime monitor příkazy,
RAW dumpy nebo pravidelné logy. Použity stávající logovací cesty. EPG L1 zůstává,
programme Core/Now-Next/acquisition/UI nejsou přidány.

## Příčina a nejistoty

Původní START končil po osmi **opakováních** s 25ms odstupem. Po selhání se
driver vrátil do Ready (idle command engine), ale neexistovala další aktivace
požadované stanice. Pozdější stabilní RF a service list ji samy nespustily.
Log uživatele prokazuje odmítnutý START a pozdější růst databáze z 10 na 17
služeb. Konkrétní důvod ERR 0x01 z tohoto logu nelze definitivně určit; proto
recovery také používá aktuální CID z čerstvého seznamu, bez změny EEPROM.

Druhá chyba: po DIGRAD bylo pokračování založené na starém `error`, ne na
výsledku parsování, valid+acquired, frekvenci a generaci. Třetí: 3s timeout
deferred požadavku se kontroloval jen při `Dab.ready()`, takže trvale obsazený
command channel mohl UI ponechat v Starting bez vypršení deadline.

Přijatý START a audio-info NOT_AVAILABLE jsou jiná větev než odmítnutý START.
`_activeServiceValid` dále znamená přijetí START, `_dabAudioInfoValid` aktuální
metadata; malý `dab_audio::Activation` navíc pamatuje potvrzení a recovery budget.
Nenulová audio metadata nejsou měřením slyšitelného analogového výstupu.

## Audit přechodů a implementace

| Cesta/událost | Ověření a výsledný stav | Chyba/generace/ownership |
| --- | --- | --- |
| beginAsync → boot → band ready | původní image/property/XPAD readback, queued volume | audio supervisor zrušen, nová generation; HW sekvence beze změn |
| handleRadioEvents → deferred DAB_SetChannel | jen volný command channel, nejvýše 3 s na přijetí | deadline platí i při busy; po vypršení žádný pozdní restore |
| requestDabService | requested SID/index/CID, nulový SID odmítnut, metadata/DLS/MOT reset jako dosud | nová `_generation`, `_dabSwitch.requestId`, nový RF/sample budget |
| stejný mux | cached index/valid může ušetřit tune | před START vždy nový DIGRAD; cached valid samotný nestačí |
| jiný mux / aktivní data/audio | původní STOP data → STOP audio → 200ms settle → tune | OK/0x03 inactive, BUSY/timeout a omezený backoff zachovány; nevynucuje se START po nepotvrzeném STOP |
| tune → STC → DIGRAD | parse OK, valid+acquired, správný index a kHz | parser vrací bool, neplatný výsledek nastaví error/invalid; žádný stale success |
| RF čekání před START | nový stav DabRfWait, recheck po 100 ms | 5s okno od požadavku; po vypršení Exhausted, volné UI |
| START rejected | krátká transient větev nebo idle cooldown | UI operation failure kontrakt zůstává, requested recovery přežije |
| START accepted | `_activeServiceValid=true`, Accepted, operation success | původní význam dokončení zachován; není to audio verified |
| audio info ready | nenulový bitrate/rate a přesný aktivní SID/CID | Verified, zrušení retry; stale command generation nemůže změnit nový stream |
| opakované 0x03 bez ready | metadata retry 400/500/1000/2000 ms a pomalá větev zachovány | po minimálně 30 s a 5 odpovědích lze jednou obnovit audio |
| RF reacquisition / reconfiguration | nové ověření audia; nové RF/list pro recovery | reconfiguration invaliduje generation a pending retry; žádný automatický hard reset |
| user tune / scan / FM / boot recovery | cancelAudioActivation, nové tokeny | scan ruší plán před mute; pozdní completion se ignoruje, aktivní transport se neabortuje násilím |
| SLS OFF/ON | původní XPAD/property a image pravidla | audio supervisor se nerestartuje z absence DLS/MOT |

`_operation`, `_commandPending`, `_state`, `_stateAfterTune`, STOP resume a
`_serviceTransitionPending` zůstávají řízeny jediným existujícím command enginem.
Nový helper je časová politika, nikoli druhý scheduler nebo task. `_stcPending`
se dál zvedá z STATUS a před tune se ruší. `_rfGeneration/_rfSampleMs` navíc
vyžadují poslední úspěšný DIGRAD aktuálního požadavku starý nejvýše 1500 ms.
Recovery vždy ještě vydá nový DIGRAD bezprostředně před STARTem.
Pouhé reconfiguration warning pozastaví aktivaci a vyžádá RF, ale nemění
stream generation a nemaže partial PAD. Generaci invaliduje až skutečný
reconfiguration event; host test kontroluje zachování a dokončení partial.

### Rozpočet

- Pro transient START 0x03/0x18 nejvýše **8 pokusů celkem**, mezi nimi 25 ms.
  Stabilní větev s jedním či dvěma odmítnutími nezískala dodatečné čekání.
- Potom nejvýše **4 jednotlivé** pokusy po 500/1000/2000/4000 ms. ERR 0x01 a
  ostatní chyby nejdou do rychlé smyčky; čekají na databázi/cooldown.
  Nominální součet pomalých prodlev je 7,5 s, rychlých 175 ms; navíc běží
  skutečné příkazy a priority. Nejde o zaručený čas náběhu na RF.
- Bez nové události po vyčerpání není další periodický START. Nové získání RF
  či skutečně jiná verze seznamu s požadovanou audio službou mohou znovu otevřít
  budget odmítnutého STARTu; stejný refresh/label jej neobnovuje.
- START přijatý, ale opakovaně nedostupné audio info: minimálně 30 s od přijetí,
  alespoň 5x 0x03 a poslední taková odpověď nejvýše 15 s stará, správný čerstvý RF,
  žádná pozitivní informace pro současný RF/servisní kontext. Jednou 1s cooldown
  a **jeden** řízený restartovací cyklus na uživatelský požadavek. Jeho START
  chyby mají stejný konečný retry budget. Druhé dlouhé selhání je Exhausted.
- Jediná občasně chybějící odpověď po Verified nezpůsobuje restart. Nulové či
  vadné hodnoty se nevydávají za ready; samy nejsou důkazem pro destruktivní retry.

Pro řízenou obnovu je použit audio START bez STOP jiných služeb. DAB část
[AN649 rev.2, str.172](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/an649.pdf)
popisuje nahrazení předchozí audio služby novým STARTem. Běžné uživatelské
přepínání zachovává dosavadní STOP/settle politiku. Partial PAD se invaliduje až
při skutečném restartu; hotový cache obrázek stejného endpointu lze ponechat.
Při změně endpointu se cache zruší. Nečinnost příjmu partial MOT nikdy nemaže.

Audio potvrzování dostává low-priority slot před ostatními metadaty a EPG.
Výběr burstu DSRV/RF zůstává původní: nejvýše 4 DSRV před odloženým RF či
jedním metadata slotem. EPG a nová separátní data aktivace čekají během
audio aktivace/ověřování. EPG L1 se opět uvolní po potvrzení nebo degradaci.
Nulové DLS/MOT/IRQ nejsou vstupem audio recovery. PAD `src=1, DSCTy=60`
zůstává původní cestou; supervisor nezapíná cizí packet-data endpoint.

## Srovnání MOT

`run_mot_benchmark.py` kompiluje matching historický `.cpp/.h` v TEMP pro
28097fd, 1f19644 OFF/ON a aktuální OFF/ON. Jediný historický header adapter
přidává friend pro přístup testu; netransformuje produkční algoritmus. Sdílené
vendor, scheduler a Slideshow.cpp se mezi srovnávanými revizemi nezměnily.
Pro starý 30s reproducer se nyní také používá odpovídající historická hlavička.

Model má minimální CTS 3 ms pro DSRV a 2 ms pro metadata, skutečnou polling
politiku driveru a milisekundový fake clock. Není to SPI/RF benchmark ani
měření CPU dekódu. FIFO hostu je neomezená; nemodeluje RF buffer kapacitu.
Byte fixtures mají JPEG signaturu, ověřuje se přesná assembly/memcmp/hash,
nikoli dekódování těchto syntetických dat. Reálné JPEG/PNG testy běží zvlášť.

| Scénář | Aktuální OFF/ON: první přijatý blok → complete | Aktivní fronta OFF/ON | Výsledek |
| --- | ---: | ---: | --- |
| 15588 B, 31 body segmentů | 171 / 171 ms | 176 / 176 ms | stejné bajty, 34 DSRV včetně header/bad/duplicate |
| 16309 B, 32 body segmentů | 176 / 176 ms | 181 / 181 ms | stejné bajty, 35 DSRV, dva metadata sloty |
| obrácené pořadí 32 segmentů | 171 / 171 ms | 181 / 181 ms | úplné, LAST před header a body0 |
| idle polling bez ISR, příchod mezi polly | 170 / 170 ms | 200 / 200 ms | žádný prázdný GET; 50ms fallback funguje |
| segment 7 až za 45/90/180 s | 45000/90000/180000 ms shodně | 181 / 181 ms | partial zachován; stará 28097fd jej po 30 s ztratí |
| dva TID, stejný obraz | 900 / 900 ms | 361 / 363 ms | oba TID kompletní, hash stejný; +2ms za již běžící UA |
| syntetické 421ms nevolání task | 597 / 597 ms | 602 / 602 ms | proti 32-segmentové větvi přesně +421 ms |

Každý průchod obsahuje záměrnou duplicitu a vadný zkrácený paket, přesný
nepoškozený výsledek, bursty 4+, ISR nebo polling. ON obslouží všech 17 UA
komponent (u dlouhých testů i další 60s refresh). Bytes/read count/completion
jsou ON/OFF shodné; v modelu již rozběhnutý UA může odložit frontu o jeden
nepreemptibilní command slot. Z toho nelze odvodit stejnou milisekundovou mez
na skutečném přijímači.

Nový TID nad hotovým cached obrázkem musí kvůli jediné aréně počkat na body0,
než začne přepisovat cache. Úvodní LAST31 se pak může vrátit až v příštím cyklu;
v two-TID fixture se vrací v t=1000 ms. Jde o zachovanou ownership politiku
obou starých revizí, ne důkaz nové ztráty nebo pomalé SPI.

**Skutečné zpomalení MOT na RF: neprokázáno.** Drain scheduler, sběrnice a
dekodéry nebyly kvůli domněnce optimalizovány. Synchronní renderer volá yield,
nikoli Dab.task; test jeho 421ms pauzy dokládá host gap, nikoli reálné overflow.
Bezpečná změna vyžaduje odděleně vyřešit resumable decode nebo transport pump
s prokázanou nereentrancí a single-owner arénou. Proto zde není nový callback
s reentrantním Dab.task ani druhý framebuffer. Potřebu určit HW měřením.

## Problém → mechanismus → změna → důkaz

| Problém | Mechanismus / změna | Test | Stav |
| --- | --- | --- | --- |
| audio nenaskočí po startu | bounded START supervisor + aktuální CID | cold boot, osm odmítnutí, následný START bez uživatele | host PASS; HW neověřeno |
| RF valid zaměněno za službu | fresh generation/index/kHz + oddělené Accepted/Verified | parse failure, acquired=0, wrong index/kHz, deadline | host PASS |
| Audio loading trvale | jediné restartovací okno, pozdější ready ruší recovery | 0x03/zero/ready, druhé selhání vyčerpané | host PASS |
| nekonečné retry / stará stanice | 8+4 limit, cancel a generace | permanent fail, nová služba, scan/cancel, stale, wrap | host PASS |
| Starting nikdy nevyprší při busy | 3s kontrola i mimo Ready | skutečné handleRadioEvents/finishBandTransition | host PASS |
| STOP/switch regrese | původní potvrzování, backoff, settle | reálný driver: OK/0x03/BUSY/timeout + policy suite | host PASS |
| PAD slideshow po START | přesný aktivní audio endpoint, bez nové data služby | skutečný START → audio info → 15588/31 a 16309/32 MOT | host PASS |
| MOT timeout ztrácí partial | žádná změna collector timeout pravidel | 45/90/180 s, 30s LCD-only, historický reproducer | host PASS |
| pomalý MOT | A/B real-driver replay, EPG OFF/ON, pauza rendereru | deterministický benchmark výše | RF zpomalení neprokázáno |
| LCD problikávání | pouze audio stav v existující field cache | loading → unavailable → kodek, okolní pole beze změn | host PASS; vizuální HW neověřeno |

## Build, testy a zdroje

- `pio run`: PASS, baseline 112192 B RAM / 457461 B flash; výsledek
  **112240 B RAM / 459501 B flash**, rozdíl +48 B / +2040 B.
- `python tests/run_audio_tests.py`: PASS; skutečný driver/command engine a
  skutečné app funkce s host doubles. Nejde o simulaci analogového audia.
- `python tests/run_sls_tests.py`: PASS; také samostatný běh
  `--mot-baseline 28097fdd1ed8dbc6414f21884738896c6db230d5` PASS.
- `python tests/run_epg_tests.py`: PASS, L0 a L1 ON/OFF.
- `python tests/run_ui_tests.py`: PASS.
- `python tests/run_mot_benchmark.py`: PASS; všechny srovnávané revize v TEMP.
- Host harness ponechává existující warning `Aborted` v resultName jako warning,
  ostatní host warnings jsou errors; sanitizer zde spuštěn nebyl.

Žádná nová heap alokace ve supervisoru; image arény beze změny. MOT arena je
již v BSS, nelze ji přičítat podruhé. Free/min/largest heap a stack za běhu na
ESP32 nejsou změřeny. Syntetický host FIFO/vektory se nepočítají do FW RAM.

## HW plán (neproveden)

1. 20–50 skutečných power-on startů, stejné uložené SID/mux, silný i slabší
   stabilní signál. Archivovat stávající boot, START/result, DIGRAD a audio-info
   výstup. Rozlišit accepted, metadata verified a skutečně slyšitelné audio.
2. Přepínat stejný/jiný mux, během cooldownu i po selhání, DAB/FM, scan/cancel,
   SLS OFF/ON. Zkontrolovat konečný degraded stav a žádný starý SID/retry/reset.
3. Několik minut RockRadio Stage PAD-SLS, několik TID i stejný obrázek,
   SELECT reopen, záměrná RF pauza >30 s. Cache/partial smí zmizet pouze při
   skutečné změně kontextu/error/restartu, nikoli nečinnosti.
4. Stejná stanice/podmínky, discovery ON a build override
   `-DEPG_DISCOVERY_ENABLE=0` (programme `EPG_ENABLE` je jiný přepínač).
   Porovnat existující DSRV/MOT/OVF, IRQ, CTS genuine/hostStarved, serialDrop,
   dokončení objektů a reakce ovládání. Zvlášť manual bez renderu a auto render.
5. Časové značky přidat na PC, například monitorovým filtrem `time` při
   ručně zvoleném COM/baud; příjem logu není timestamp RF segmentu a obsahuje
   UART/queue latenci. Při práci byl ověřen pouze CLI help, COM se neotevíral.
   Zaznamenat existující SLS/RAM údaje a ověřit free/min/largest/stack trend.

Bez těchto kroků nejsou provozní RF výkon, 421ms FIFO kapacita, fyzické audio
ani vizuální výsledek LCD vykazovány jako ověřené.
