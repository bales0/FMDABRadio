# BAND, menu a živé EPG L2/L3 — 3. 11. 2026

## Stav a výchozí revize

Lokálně je implementována produkční cesta příjmu SI/PI, EPG Now/Next, seznam,
detail, nové ovládání BAND, potvrzovaný režim v menu a tříbarevný indikátor.
Host test prochází skutečným ovladačem a runtime, nikoli UI naplněným demo daty.
**Živý RF příjem na desce a stabilita 11A/11B zatím ověřeny nejsou. HW acceptance
proto zůstává otevřená.** Postup: `TEST_PLAN_EPG_LIVE_HW_CZ.md`.

Skutečný HEAD při zahájení: `6be39997181d8e725c4c2774b8c368e174fa28d6`;
pracovní strom byl čistý. Lokální referenční projekt má požadovaný HEAD
`df0c8bc9eef4c4598742fd462216675d1e885d81` a nebyl upraven. Porovnány byly jeho
runtime, ingress, Core/Bytes/Buffer, UI, discovery/probe, čas a indikátor GUI.
Čistý cílový HEAD byl pro srovnání také sestaven z `git archive` v TEMP se stejnými
verzemi knihoven. Žádný commit, push, flash, COM přístup nebo změna verze.

## Ovládání

| Kontext | BAND krátce / dlouze | SELECT krátce / dlouze | CH± |
|---|---|---|---|
| DAB | Now/Next / rezervováno | Text/SLS nebo potvrzení stanice / Settings | dosavadní stanice |
| EPG Now/Next | zavřít / rezervováno | seznam / Settings | spotřebuje se bez ladění |
| EPG seznam | zavřít / rezervováno | detail / Settings | výběr s wrap |
| EPG detail | zavřít / rezervováno | zpět na seznam / Settings | posun názvu a popisu |
| FM | RDS / rezervováno | dosavadní funkce / Settings | dosavadní stanice |
| FM RDS | zavřít / rezervováno | bez změny / Settings | dosavadní procházení stanic |

BAND zavírá informační modal do pohledu, ze kterého byl otevřen, včetně seznamu
stanic nebo SLS. Menu má vlastní návratový pohled; přepsaný modal se při návratu
znovu vykreslí po pruzích. Zastaralý detail po změně generace, ladění nebo scanu
se zavře. SLS Auto otevírá pouze Text, takže otevřené EPG nepřekrývá.
VOL± mimo Settings ovládají hlasitost; v Settings mění hodnotu vybrané položky.

BAND používá odložený short po release a long 800 ms. Long nevygeneruje short,
počáteční držení při bootu se ignoruje do uvolnění, debounce zůstává 25 ms.
SELECT 800 ms, SCAN 1000 ms, piny a externí pull-up GPIO35 zůstaly zachovány.
BAND během scanu, bootu, service/tune transition, AF probe a preview nekoná akci.
Samotná ztráta RF nebrání přečtení informační stránky inicializovaného rádia.

Settings mají 11 položek; původních deset má stejné indexy a význam. Podle
nejnovějšího upřesnění uživatele CH± přejdou na další/předchozí položku (s wrap), VOL± mění její hodnotu a
dlouhý SELECT potvrdí změny zavřením menu. Krátký SELECT je v menu bez funkce;
mimo menu si zachovává dosavadní chování. Hodnoty nastavení mají živý náhled, jejich snapshot k uložení
vzniká až při odchodu. `Radio mode / Rezim radia` má kandidáta FM/DAB; na rozdíl
od náhledu jasu se rádio skutečně přepne teprve po dlouhém SELECT při odchodu,
a to i při ukončení menu z jiné položky. Běžný background busy potvrzení odloží
nejvýše 3 s. Přijatý přechod použije stávající `startBandTransition()` jednou a
`saveModeToEEPROM` až při úspěšném band-ready. Timeout, změna kontextu či selhání
bootu nezapíšou neúspěšný nový režim. Znovuotevření Settings zruší dosud čekající
přepnutí. Serial control si zachovává původní příkazy a význam.

## Skutečná L2 datová cesta

1. L1 inventory zachovává celý SID, wire metadata a skutečný 16bit COMP_ID.
   `DAB_GET_COMPONENT_INFO` musí potvrdit UA `0x007`; odděleně se pamatuje UA SLS.
   Název služby se k výběru EPG nepoužívá. CA zdroj se neparsuje.
2. Packet EPG: jednoznačný, nešifrovaný packet-MOT endpoint s potvrzenou SPI UA.
   START proběhne pouze ve volném dosavadním datovém slotu, po audio/recovery a
   důležitějších metadatech. `_dataServiceId` se pro EPG nepřepisuje. Pokud již
   běžící heuristicky zvolený endpoint prokáže čistou SPI UA, dostane explicitní
   EPG ownership a jeho data už nemohou vstoupit do SLS.
3. Funkční samostatná SLS datová služba se kvůli EPG nezastavuje. Další současný
   data slot nebyl na cílovém HW prokázán, proto se nespekuluje o jeho kapacitě.
   Pokud je slot obsazený SLS, packet EPG čeká. Společný SPI+SLS packet endpoint
   je kvůli nejednoznačnosti ponechán SLS; samotná signalizace může být žlutá.
4. Audio PAD EPG: musí souhlasit aktuální audio SID/CID, nešifrovaná UA odpověď,
   DATA_SRC=1, DSCTy=0/60 a wire UATYPE=7. To rozliší SPI od SLS i na společné
   audio komponentě. Přednost PAD zamezí míchání dvou EPG karuselů. DLS se
   nepředává EPG parseru. Packet DATA_SRC=0 vyžaduje DSCTy=60 a vlastněný endpoint.
5. Existující DSRV příkaz přečte celých `24+BYTE_COUNT`, znovu zkontroluje délku,
   physical/payload error, service state, identitu a kontext. Přijímá plný wire
   CID nebo jeho přesnou zero-extended 16bit podobu, nikoli libovolné maskování.
   Staré command generations jsou odříznuty společnou obsluhou ovladače.
6. Vlastněné skupiny jdou do `epg_live::payload` → `Core::feed`: CRC, group
   framing, directory, omezené skládání dvou objektů, out-of-order/LAST/duplicitní
   segmenty. SLS používá nadále pouze svou původní arénu a render lease.
7. `epg_live::pump` v hlavní smyčce provede nejvýše 16 TLV kroků. SI a PI se
   dekódují stávajícím Core; aktuální bearer se hledá podle audio SID/EID/ECC,
   s odmítnutím konfliktu/nejednoznačného SCIdS. Cizí programme cache se nezobrazí.
8. Now/Next, seznam a detail čtou přímo vybranou cache Core. Bez důvěryhodného
   času se NOW/NEXT neodhadují; list/detail označí časy UTC. Chybějící NEXT zůstane
   chybějící. Text používá stávající UTF-8 mapování a posun po codepointech.

Živý runtime má v cílovém buildu `EPG_ENABLE=1`. Legacy host testy optují do L2
výslovně; build `-DEPG_ENABLE=0` zůstává použitelný. Discovery lze vypnout zvlášť.
`epg_live.cpp` a `EpgUi.ino` jsou součástí firmware, nejde o soubory pouze pro testy.

Zdrojové detaily ověřeny také proti [Skyworks AN649 Rev. 2.0](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/an649.pdf):
START/STOP DAB, DSRV UATYPE/DATA_SRC, GET_TIME a DAB_XPAD_ENABLE. Osvědčených
`0x0097` zůstává zachováno a při L2 zůstane povoleno i při vypnutém SLS, aby se
neodpojil PAD SPI. Vypnutí SLS neresetuje EPG vlastníka datové služby.

## Čas, cache, scheduler a chyby

Čas je párovaný přes serializované UTC GET_TIME (typ 1) a následující LOCAL
GET_TIME (typ 0). Offset se odvozuje z dvojice ve stejné generaci do 2 s;
hlavní obrazovka dále dostává local kalendář. Neplatný pár, DST nesoulad,
ztráta kontextu či vzorek starý 5 minut nedávají trusted UTC. Nepoužívá se
natvrdo časové pásmo ani čas původních globálních Year/Hours jako důkaz UTC.

Platná SI/PI aktualizace dostane vlastní čítač/čas dokončení. Jediný duplicitní
segment neprodlužuje čerstvost cache. Dříve dokončený SI/PI TID se nejdříve po
60 s znovu skládá a validuje celý; cache používá vysílanou verzi pořadu. Adresář
se změněným obsahem invaliduje původní metadata. Cache freshness je 15 minut;
skládání má vlastní 5minutový výchozí/adaptivní timeout dle Core (2–30 minut
podle signálovaného karuselu). Tyto lhůty nejsou timeoutem LCD čekacího textu.

Periodické ověření UA nad stejným inventory ponechává dosavadní potvrzený důkaz
platný do konkrétní nové odpovědi. Samotné opětovné dotazování nevynuluje PI,
nerozbije partial a nezmění zelenou na šedou každou minutu. Nový inventory,
změna tune nebo negativní/selhaná UA odpověď důkaz skutečně odvolají. Počáteční
L1 Pending bez předchozího důkazu zůstává šedé.

Při stabilním nekončícím backlogu dostane L1 maximálně jednu UA příležitost za
sekundu až po existujícím burstu čtyř DSRV paketů. Audio/RF/STOP/recovery mají
přednost. Nezměnily se `DAB_MAX_DSRV_BURST`, IRQ latch ani takty. Žádný druhý
command scheduler a žádné reentrantní `Dab.task()`.

EPG START má omezený pokus a po chybě explicitní cleanup možné aktivace.
STOP používá dosavadní omezené retries; po jejich vyčerpání je endpoint v
karanténě, data se neparsují a background STOP se neopakuje donekonečna.
Tyto EPG chyby samy nezvyšují sérii pro reset rádia; skutečné CTS chyby zůstávají
v diagnostických počtech. Uživatelský tune dále podléhá stávajícím STOP pravidlům.

EPG stránky se vykreslují po jednom změněném pruhu na průchod, takže mezi řádky
znovu běží hlavní loop a rádio. Krajní pruhy smažou také okraje obrazovky;
otevření EPG nevyžaduje full clear. Běžná aktualizace porovnává text i barvu.
Badge je vlastní cache v obdélníku x=138..159, y=94..109, vedle RSSI;
nezasahuje do jména, textového canvasu ani audio/MOT/signal polí.

| Barva | Přesná podmínka |
|---|---|
| Šedá `0x8410` | Neplatný/stará generace kontextu nebo žádná potvrzená SPI UA. Samotné čekání L1/SLS/DLS nestačí. Ve FM se nekreslí. |
| Žlutá `0xFFE0` | Platný kontext a potvrzená SPI signalizace, ale není použitelný aktuální PI: čekání, cizí bearer, OOM, nejistý čas, stale nebo neúplný příjem. CA signalizace je přítomnost, nikoli přípustný obsah. |
| Zelená `0x07E0` | Přesný zvolený bearer bez konfliktu, platná PI cache a Core `ReadyLive`: trusted clock, platné datum, freshness, použitelný NOW nebo NEXT. Nové fragmenty samy barvu nemění. |

Diagnostika `[EPG/L2]` uvádí důvod, UA answered/total/confirmed, selected bearer,
počet pořadů, route/reject/busy/CRC, directory/SI/PI, poslední validní update,
maxBacklog/overflow a free/largest/min8/stack. Perioda je 10 s nebo změna barvy;
nejde o log každého segmentu. `[EPG/RAM] before/after` zachycuje alokační bránu.

## Paměť a sestavení

Měřené velikosti cílovým Xtensa kompilátorem: Core **14 456 B**, runtime buffery
**36 864 B**, dohromady **51 320 B** dynamicky. Dvě 12 288B assembly, 4096B directory
a 8192B text zůstávají zachovány; pouze nepoužívané EPG logo se nealokuje.
Úspora proti plné variantě je 12 288 B. Plný Core pro offline testy má stále logo
a celkem 63 608 B na cílovém layoutu. Host 64bit sizeof není zaměňován s ESP32.

Core/metadata patří do INTERNAL|8BIT DRAM, buffery do INTERNAL|32BIT|EXEC IRAM.
Přenesený Bytes/Buffer provádí na IRAM pouze zarovnané volatile 32bit přístupy;
byte pointer do této paměti se parserům ani kreslení nepředává. Alokace probíhá
jednou, po startovní alokaci sdíleného SLS dekodéru. Brána požaduje DRAM
`free >= sizeof(Core)+16000`, `largest >= sizeof(Core)` a IRAM
`free >= 36864+4096`, `largest >= 12288`. Každá skutečná alokace se dále kontroluje.
OOM uvolní všechny částečné bloky i Core a zablokuje opakované pokusy do restartu;
audio, DAB/FM, SLS a menu zůstávají použitelné. Žádná PSRAM ani velký RGB buffer.

Velikosti při dokončení základní L2/L3 etapy; novější oprava ovládání/LCD je změřena níže.

| Sestavení | Statická RAM | Flash |
|---|---:|---:|
| Čistý HEAD 6be3999 | 112 344 B | 460 785 B |
| Nový kód EPG OFF | 113 960 B | 465 429 B |
| Nový default EPG ON | 114 064 B | 492 237 B |

Default proti čistému HEAD: **+1720 B statické RAM, +31 452 B flash**.
EPG ON proti stejnému novému UI s EPG OFF: +104 B statické RAM a +26 808 B flash.

SLS aréna 51 200 B je již součástí statické RAM; znovu se k ní nepřičítá.
Sdílený JPEG/PNG workspace 76 800 B zůstává samostatnou původní heap alokací.
**Free/largest/min heap a stack HWM na fyzické desce nebyly zde měřeny.**
Statické linker výsledky a host OOM test nejsou HW důkazem volné IRAM.
Runtime inicializaci dovolí až skutečné on-device měření a úspěch všech bloků;
při nesplněné bráně ukazuje OOM/žlutou a žádná programová data nepředstírá.

## Provedené kontroly

| Příkaz | Výsledek |
|---|---|
| `pio run -e FMDABRadio` | PASS, default EPG ON |
| Totéž s `PLATFORMIO_BUILD_FLAGS=-DEPG_ENABLE=0` | PASS; příznak byl jen v procesu, konfigurace projektu nezměněna |
| EPG ON + `-DEPG_DISCOVERY_ENABLE=0` | Cílové kontrolní sestavení PASS; host L2 s discovery OFF také PASS |
| `python tests/run_audio_tests.py` | PASS; cold boot, RF/START retries, audio accepted/verified/recovery, STOP, boot UI, mode EEPROM success/failure |
| `python tests/run_sls_tests.py` | PASS; JPEG baseline/progressive, PNG/Adam7, lease, A→B→C, TID replay, 45/90/180s mezery, DSRV race, bus limity |
| `python tests/run_epg_tests.py` | PASS; L0/L1, packet i PAD přes skutečné DSRV, SI/PI→bearer→NOW/NEXT, cizí/generace/CRC, souběžná partial SLS, cache stale/full revalidation, UTC/local a cleanup timeout bez audio resetu |
| `python tests/run_ui_tests.py` | PASS; skutečné render/navigační funkce, 160×128 bounds, UTF-8, badge/row cache, kooperativní pruhy, BAND/SELECT/SCAN hranice, menu candidate/cancel/busy/rollback |
| `python tests/run_mot_benchmark.py` | PASS; syntetický čas, přesné hashe a bytes; stejné MOT výsledky discovery ON/OFF |
| `git diff --check` | PASS |

Runtime OOM je otestován v každém z pěti kroků (Core + čtyři bloky), včetně
cleanup/latch, zachování partial SLS a nemožnosti zeleného stavu. Plná Core
varianta s pěti buffery má nadále samostatné původní OOM a hostile testy.
V produkční přijímací cestě nejsou opakované alokace. Testované obrazovky používají
TFT spy; nedokládá se tím vizuální kvalita fyzického LCD ani skutečná délka SPI.
Host varování pro dosud nepojmenovaný vendorizovaný `Result::Aborted` existovalo
už před změnou a nezpůsobuje selhání testů.

Syntetický MOT benchmark zachoval: 31/32 segmentů 171/176 ms, dlouhé mezery
45/90/180 s bez ztráty partial, dva TID 421 ms OFF / 423 ms ON. Stávající
421ms blokující image decode model nadále znamená max GET gap 426 ms;
resumable JPEG/PNG renderer nebyl touto úlohou přepsán.

## Otevřené HW limity

Nutno potvrdit reálný SI/PI karusel, UA/PAD na vysílači, UTC/local/DST, dostupnou
IRAM po dekodéru a chování CTS/FIFO na 11A/11B. Obsazený SLS data slot, více
nejednoznačných packet SPI endpointů, CA či neznámá bearer identita vedou ke
žluté bez vymyšleného programu. Nepodporované/oversize SPI objekty se odmítají
v mezích dosavadního Core. EPG loga se v textových stránkách nezobrazují.
Denní browsing se nepřidává bez jednoznačného data; seznam odpovídá vybranému
dni Core. Při změně directory může být nová cache dočasně prázdná.

Původní single-buffer SLS algoritmus, render lease, MOT deduplikace, dekodéry,
audio supervisor, EEPROM layout, firmware blob, piny, IRQ, radio SPI 10 MHz,
TFT clock i I2C clock se nepřepisovaly. Přenos více současných packet-data služeb
se bez ověření na cílovém HW nezapíná. Celá HW acceptance zatím není uzavřena.


## Navazující oprava SELECT/BAND a kompaktní LCD

Uživatel schválil opravu po hlášení nefunkčního potvrzení režimu a nerozlišených
EPG stránek; zároveň požádal o jeden řádek běžícího textu a EPG vedle signálu.

- VOL± nadále volí kandidáta FM/DAB; SELECT jej potvrdí. Pokud kandidát odpovídá
  aktuálnímu režimu, menu zůstane otevřené a ukáže nápovědu VOL/SELECT.
- Potvrzený požadavek se při background busy nezahodí. Čeká nejvýše 3 s a obslouží
  se před `Dab.task()`, takže zachytí první volný command slot před dalším
  background příkazem. `FM ... / DAB ...` a footer ukazují čekání. Opakovaný
  SELECT nevytvoří druhý požadavek ani neprodlouží timeout.
- Změna kandidáta, jiná položka menu, long SELECT, změna generace, scan/AF/tune
  požadavek zruší; po timeoutu nedojde k pozdnímu bootu. Po start rejection zůstává
  původní režim; EEPROM se nadále mění až při skutečném band-ready.
- Short BAND a long BAND 800 ms jdou odlišnými cestami. Prázdný Now/Next nově
  výslovně uvádí aktuální/další pořad; prázdný seznam uvádí prázdný seznam pořadů.
  Log `[KEY] BAND short -> EPG Now/Next` / `BAND long -> EPG list` dovolí ověřit
  skutečnou detekci na desce. Detektor nebyl naslepo nahrazen jiným časováním.
- Textový pás má 13 px na y=44..56, se třemi prázdnými pixely nad i pod ním;
  původní 260B monochromatický canvas a plynulý scrolling zůstaly stejné.
  Následuje separator y=60, kodek na y=64, CNR/SNR na y=80, RSSI na y=96 a
  původní patička RX/FIC/MOT/stanice na y=113. EPG je vpravo od RSSI na y=96.
  RSSI hodnota i jednotka se vejdou celé; clear obdélníky signálu nezasahují EPG.
  Menu footer se obnovuje jen při změně skutečného stavu/nápovědy.

Ověření po této opravě: `pio run -e FMDABRadio` PASS (RAM **114072 B**, flash
**493773 B**, proti předchozímu defaultu +8/+1536 B); `python tests/run_ui_tests.py`
PASS a `python tests/run_audio_tests.py` PASS. UI test nově propojuje skutečný
Controls stavový automat s produkční obsluhou a rendererem: dlouhý stisk otevře
LIST, krátký NOW/NEXT, uvolnění po long nevygeneruje short a prázdné stránky
mají odlišný obsah. Dále ověřuje single pending commit, busy→idle, cancel,
3s timeout přes millis wrap, změnu generace, rollback, žádný pozdní boot a
nepřekrývání RSSI/EPG obdélníků. Fyzické GPIO/LCD nadále vyžaduje HW ověření;
žádný flash ani push nebyl proveden. Core, data transport, SLS a takty nezměněny.


## Aktuální sjednocení menu a rezervace dlouhého BAND

Tato novější mapa výslovně nahrazuje předchozí varianty SELECT/VOL i dlouhého
BAND popsané v historických etapách výše:

- Krátký SELECT: další položka Settings, na konci wrap na první.
- CH±: změna hodnoty vybrané položky, včetně kandidáta FM/DAB.
- VOL±: pouze audio hlasitost, také během otevřeného menu.
- Dlouhý SELECT: snapshot nastavení a odchod. Změněný FM/DAB se teprve nyní
  potvrdí do jediné fronty a při nejbližším volném radio slotu provede. Short
  SELECT, pouhé procházení a CH preview nepřepínají rádio ani neukládají režim.
  Při pending/timeoutu se stav objeví v audio řádku hlavního pohledu.
- Krátký BAND zůstává DAB Now/Next nebo FM RDS a zpět. Seznam a detail EPG jsou
  dostupné krátkým SELECT uvnitř EPG. Dlouhý BAND je v obou pásmech bez funkce,
  rezervovaný do budoucna. Detektor 800 ms zůstává zachován, po jeho long události
  ani při uvolnění nenásleduje krátká akce. V menu i AF cestě se long neodkládá.

Po této změně: `pio run -e FMDABRadio` PASS, RAM 114072 B, flash 493393 B.
`python tests/run_ui_tests.py` a `python tests/run_audio_tests.py` PASS. Host test
spojuje skutečné Controls s produkční obsluhou a ověřuje long BAND bez akce ve
FM i DAB, short po release, přístup SELECT do EPG seznamu, SELECT navigaci 11
položkami, CH hodnoty, VOL pass-through, žádné ukládání/přeladění při preview,
potvrzení až na long exit z libovolné položky, busy→idle, timeout/wrap, změnu
kontextu, odmítnutí startu a zachované audio. Bez automatického flash a bez
fyzického HW ověření. Jednořádkový text a EPG vedle RSSI zůstávají beze změny.


## Finální oprava mapy menu po odvolání předchozího upřesnění

Výslovná poslední instrukce uživatele nahrazuje předchozí sjednocení:
CH± navigují položkami Settings, VOL± upravují hodnotu. Krátký SELECT je v menu
úmyslně spotřebován bez změny hodnoty, výběru, ukládání, ladění i kreslení.
Dlouhý SELECT potvrzuje uložením snapshotu a odchodem; kandidát FM/DAB se
potvrdí a případně počká na radio idle teprve při tomto odchodu. Mimo Settings
zůstává krátký SELECT funkční (stanice, Text/SLS, EPG list/detail) a VOL ovládají
hlasitost. Dlouhý BAND zůstává rezervovaný bez akce v DAB i FM.

Finální `pio run -e FMDABRadio`: PASS, RAM 114072 B, flash 493433 B.
`python tests/run_ui_tests.py`: PASS. Nový test prochází všech 11 položek a
ověřuje, že krátký SELECT ani jeho případný Repeat nemění strukturu nastavení,
kandidáta režimu, vybranou položku, počet zápisů, počet bootů ani LCD.
Testuje rovněž CH wrap, VOL změny hodnot a potvrzení až dlouhým SELECT.
Bez flashování; fyzický HW test nadále neproveden.
