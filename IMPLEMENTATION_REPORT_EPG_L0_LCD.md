# EPG L0 a dílčí překreslování LCD — 2026-11-02

Tento dokument zachycuje výsledek L0. Navazující schválené změny (L1 discovery
a malý textový buffer) popisuje IMPLEMENTATION_REPORT_EPG_L1_TEXT.md.

Schválená etapa přidává samostatné EPG jádro pro hostové replay testy a omezuje
LCD překreslování na změněná pole. Živý příjem EPG ještě není připojen.

## LCD

Původní dirty flags zůstávají pro plánování. `UiTextCache.h` navíc porovnává
text, barvu, polohu, velikost a šířku každého pole. `Ui.ino` maže a obnovuje jen
změněné pole; statické popisky a oddělovací čáry ponechává. DLS včetně plynulého
pixelového posunu obnovuje pouze svůj 13px pás. Přechod obrazovky nebo plné
vymazání invaliduje cache. Menu a seznam si zachovávají vlastní dílčí rendering.

Test skutečných produkčních vykreslovacích funkcí s TFT spy ověřuje 100
opakování bez změn bez jediného TFT zápisu a izolované změny hlasitosti,
signálu, FIC, MOT, kodeku, AF, názvu a DLS. Nejde o měření fyzického displeje;
měnící se pole se stále nejprve vymaže a pak vykreslí, bez framebufferu.

## EPG: převzato a upraveno

Zdroj: `SI4684-FMDAB-Receiver`, revize
`df0c8bc9eef4c4598742fd462216675d1e885d81`; oba projekty GPL-3.0.
Převzato omezené assembly/parser/cache jádro a časové helpery. Používá
byte-addressable DRAM, společné CRC16/CRC32, pevné limity a pět alokací při
konstrukci. Při neúplné alokaci vše uvolní a zůstane bezpečně neaktivní.
Příjem a pump nepřidávají další alokace. Statistické čítače saturují.
Opravena identita SCIdS v krátkém i dlouhém bearer zápisu a aritmetika browseDay.

`EPG_ENABLE` je výchozí 0. Hodnota 1 umožní kompilaci jádra, ale nevytváří
instanci a nezapíná RF příjem. Žádné nové tuner příkazy, serial monitor,
EEPROM, TFT stránka nebo automatické runtime alokace. Komprimovaný logo buffer
zůstává součástí testovaného core API; není připojen nový image decoder.

Odloženo: live discovery, SID/CID a tune-generation routing, acquisition
scheduler, důvěryhodný broadcast clock, runtime OFF/ON, UI a serial integrace.
Interní expiry EPG assembly z reference není SLS timeout a před živým zapojením
vyžaduje RF ověření. Existující SLS cache a 30s indikace příjmu se tím nemění.

## Ověření a paměť

- `python tests/run_ui_tests.py`: PASS.
- `python tests/run_epg_tests.py`: PASS; historický directory/SI/PI replay,
  42 programů, multi-TID, reorder/duplicity, cache tlak, OOM všech pěti alokací,
  nulové hot-path alokace, CRC/TLV/UTF-8, identita, čas/offset a millis wrap.
- `python tests/run_sls_tests.py --mot-baseline 28097fdd1ed8dbc6414f21884738896c6db230d5`:
  PASS včetně reprodukce původního zahazování neúplného MOT po pauze.
- PlatformIO default build: RAM 110576 B, flash 452577 B. Proti bezprostřednímu
  výchozímu stavu této etapy 108880/451625 B jde o +1696/+952 B.
- Xtensa compile-time layout: `ListeningCache` 1696 B; EPG Core 14424 B,
  jeho pět bufferů dohromady 49152 B, celkem případně 63576 B. V aktuálním
  firmware se žádná instance Core ani tyto buffery nevytvářejí.
- PlatformIO s `EPG_ENABLE=1`: PASS, RAM 110576 B, flash 452925 B;
  kompilace jádra neaktivuje jeho živý příjem.

Statická RAM již zahrnuje 51200 B MOT arénu. Decoder používá dalších 76800 B
heap. S případným budoucím EPG by účet těchto částí byl
110576 + 76800 + 63576 = 250952 B; není to měření volného heap ani záruka,
že bude za provozu dostupný dostatečný souvislý blok. Host x64 má jiné sizeof.

Velký RAW oracle s 1087 programy nebyl spuštěn; replay pokrytí je uvedeno výše.
Žádné nahrání do zařízení ani HW test. Zbývá vizuální kontrola LCD, free/min/
largest heap, CTS a dlouhodobá RF regrese. Živá EPG integrace je další etapa.
