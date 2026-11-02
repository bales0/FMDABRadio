# LCD / shared SPI – cílené stabilizační úpravy

## Změny

- `DAB::configureSharedSpiPeer(TFT_CS)` nastavuje druhý CS; bez konfigurace
  zůstává peer `0xFF`. Před každým radio `writeCommand()` / `readReply()` jsou
  TFT CS i radio CS nastaveny HIGH, poté se aktivuje pouze radio CS. Po přenosu
  se radio CS vrací HIGH před ukončením SPI transakce.
- `prepareTftSpi()` nastavuje oba CS HIGH před hlavními vykreslovacími vstupy
  TEXT, slideshow, menu, seznamu, aktualizací dirty oblastí a `clearScreen()`.
  Poslední vstup pokrývá také full-screen scan. Částečné překreslování zůstává.
- TFT má po startovním i ručním `initR()` explicitně **8 MHz**. Rádio zůstává
  na **2 MHz**. Zkušebních 16 MHz je pouze komentovaná možnost, nikoli automatika.
- Z `DAB_SetChannel()` a `FMsetChannel()` odstraněny opakované volume requesty.
  Hlasitost se žádá jednou při události band-ready: cold boot, DAB/FM image boot
  i recovery. Během skenu zůstává mute; dosavadní obnovení po skenu i explicitní
  uživatelské změny hlasitosti zůstávají. Mapování Si4684/TPA se nemění.
- Ruční serial příkazy `LCDREDRAW` a `LCDRESET` zachovávají aktuální obrazovku
  včetně otevřeného menu/seznamu a jejich výběru. První pouze překresluje,
  druhý volá `initR()`, obnoví 8 MHz, rotation 3 a vypnutý text wrap a překreslí.
  Odpovědi jsou `!OK LCDREDRAW` / `!OK LCDRESET`.
  Během radio operace, skenu, přechodu pásma nebo AF vracejí `!ERR BUSY`.
  Knihovní `initR()` je blokující ruční diagnostika, nikoli automatická recovery;
  může krátce zdržet příjem metadat i na jinak připraveném rádiu.

## Audit přepínání DAB

- Aktivní samostatná data se zastavují před audiem. Stejný mux pokračuje po
  potvrzeném STOP a 200 ms settle na START; jiný mux nejprve na TUNE a jeho
  dokončení. Bez aktivních dat se vynechá pouze data STOP.
- Pokud po data STOP není aktivní audio, probíhající přechod stále čeká 200 ms.
  Při požadavku z klidového stavu bez jakéhokoli aktivního streamu není co
  zastavovat a dosavadní přímý TUNE/START zůstává.
- Device error `0x03` potvrzuje již neaktivní stream a pokračuje bezpečnou cestou.
  Ostatní chyby mají nejvýše tři opakování s dosavadním odstupem 250/500/750 ms.
  Backoff explicitně obnovuje data/audio STOP. Po vyčerpání se operace ukončí
  neúspěšně, nikoli STARTem nového streamu. Diagnostický čítač command errors
  nadále započítává i odpověď `0x03`; její nefatální zpracování se nemění.
- Po audio START zůstává 200 ms před vyhodnocením/spuštěním dat.
- Driver nepřijímá druhý switch během STOP/settle: `ready()` a pending-command
  guard jej odmítnou ještě před přepsáním cíle. Nejde o frontu „nejnovější vyhrává“.
  Generation kontrola dále odmítá stará dokončení po změně generace bootu.
  Request-id policy testy ověřují supersession pomocných stavů; nejsou důkazem,
  že produkční driver přijímá superseding požadavky během rozpracované operace.
- STOP klasifikace a rozhodnutí po data STOP jsou sdílené testovatelné helpery.
  Celá state machine ani scheduler nebyly přepsány.

Zůstává single-owner cooperative loop: žádný nový task, mutex nebo SPI z ISR.
GPIO, EEPROM, scan databáze, MOT, dekodéry, firmware adresy a audio gain logika
se nemění. Odstraněná TECH obrazovka se nevrací.

## Ověření

- PlatformIO `pio run`: SUCCESS, prostředí `FMDABRadio`.
- RAM: 108888 / 327680 B (33,2 %); flash: 444369 / 1310720 B (33,9 %).
- Všech pět host testů sestaveno pomocí `g++ -std=c++11 -Wall -Wextra -pedantic`
  a úspěšně spuštěno: radio_scheduler, fm_features, dab_service_switch,
  mot_assembly a jpeg (s `src/JPEGdecoder.cpp`). Binární testy jsou v TEMP.
- Rozšířené switch testy pokrývají STOP dispatch, OK/0x03/ostatní chyby,
  oba STOP retry směry a vyčerpání, oba 200ms intervaly, supersession během
  STOP/settle, staré command request-id a wraparound. Jde o policy testy,
  nikoli integraci s fyzickým Si4684 či test elektrických průběhů CS.

**Skutečný ESP32 HW test nebyl dostupný.** Úpravy tedy nepotvrzují odstranění
zbělení na konkrétní desce ani kvalitu SPI signálu. Na zařízení ověřit rychlé
listování a SELECT v DAB (stejný/jiný mux) i FM (AF off/on), menu, scan,
slideshow auto/manual a hlasitost po DAB↔FM/recovery. Logickým analyzátorem
ověřit CS před transakcemi a nepřítomnost současného LOW obou CS. Při závadě
porovnat účinek LCDREDRAW proti LCDRESET; automatický reset není zaveden.

## Změněné soubory

- `src/si4684.h`
- `src/si4684.cpp`
- `src/RadioDAB.ino`
- `src/Display.ino`
- `src/Ui.ino`
- `src/dab_service_switch_policy.h`
- `tests/test_dab_service_switch.cpp`
- `tests/README.md`
- `FIX_LCD_SPI_REPORT.md`
