# SPI / I²C / MOT – 2. 11. 2026

Výchozí čistý HEAD: `28097fdd1ed8dbc6414f21884738896c6db230d5`.
Implementováno v pracovním stromu; bez commitů, push, flashování či zápisu testovacích
dat do EEPROM. Poslední přímý pokyn uživatele **zakazuje přetaktování** a má přednost
před agresivními profily 40/32MHz TFT a 1MHz I²C v přiloženém zadání.

## Finální takty: žádný overclock

Jediný zdroj nastavení je `src/BusConfig.h`:

| Klient | Nastavení | Limit specifikace | Předpokládaný takt této desky | HW ověřeno |
| --- | ---: | ---: | ---: | --- |
| Si4684 host SPI | 10000000 Hz | 10 MHz | 10 MHz při APB80MHz | ne |
| ST7735R 4-wire write | floor(1000000000/66) = 15151515 Hz | perioda nejméně66ns | 80MHz/6 ≈13,333333MHz | ne |
| EEPROM I²C na klasickém ESP32 | 400000 Hz | ESP32 master nejvýše400kHz | nominálně400kHz | ne |

TFT není nastaven na 16/20/32/40/80MHz. Při 80MHz zdrojovém taktu je dělič5
již16MHz, tedy nad limitem ST7735R, a dělič6 je nejvyšší dosažitelná nižší volba.
Audit místního Arduino ESP32 `spiFrequencyToClockDiv()` potvrzuje, že nevybírá takt
vyšší než požadovaný; nezávislý host test prohledává všechny povolené děliče.
Výsledek je výpočet podle konfigurace, nikoli měření osciloskopem.

Specifikační podklady: [Si4684-A10 Table3](https://shop.richardsonrfpd.com/docs/rfpd/si4684.pdf),
[ST7735R Table8.4.1](https://cdn-shop.adafruit.com/datasheets/ST7735R_V0.2.pdf),
[ESP32 I²C master](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html).
I když AT24C256C umožňuje1MHz při odpovídajícím napájení, limit řadiče ESP32 se
nepřekračuje. Osazení konkrétní EEPROM a skutečné hrany na vodičích nebyly ověřeny.
Nevznikl žádný experimentální overclock profil.

Nižší takty lze zvolit build flags, bez změny funkcí EEPROM či radio transportu:

```ini
build_flags =
    -DFMDAB_TFT_SPI_HZ=8000000UL
    -DFMDAB_RADIO_SPI_HZ=2000000UL
    -DFMDAB_EEPROM_I2C_HZ=400000UL
```

Static assertions nepovolí překročit limity ani tímto override. Default je výše
uvedené maximum, nikoli fallback. Žádný automatický zátěžový test ani autotuning.

### Důležité: initR také musí dodržet takt

Adafruit ST77xx 1.11.0 používá v `initR→commonInit→begin` natvrdo výchozích32MHz.
Pouhé `setSPISpeed()` **po** initR by tedy podmínku uživatele nesplnilo.
PlatformIO pre-script `scripts/st77xx_clock.py` proto pro kompilaci vytvoří lokální
kopii pouze `Adafruit_ST77xx.cpp` v `.pio/build/FMDABRadio/configured_st77xx/` a její
jedinou default-frequency definici napojí na `board_bus::TFT_SPI_HZ`. Ostatní kód,
CS ovládání, command tables i MIT notice zůstávají beze změny. Include původní
hlavičky je absolutní kvůli umístění generovaného souboru.

Není změněna knihovna v libdeps ani globální knihovní cache. Při změně upstream
definice se build zastaví a vyžádá audit místo tichého návratu na32MHz. Stejná
volba platí pro cold init, ruční LCDRESET i pozdější `setSPISpeed()`.
Použito oficiální [PlatformIO build middleware](https://docs.platformio.org/en/latest/scripting/middlewares.html).

Radio dál používá samostatné SPISettings, mode0 a původní framing. Zachovány oba
CS-high kroky, prepareTftSpi, begin/endTransaction a single-owner loop bez ISR SPI.
ExtEEPROM.begin podle kontroly zdroje Wire frekvenci nepřepisuje; jediný setClock
ve startEEPROM nyní používá centrální konstantu. Write-cycle, ACK timeout, page
writes, adresování ani EEPROM layout se nemění. Vyšší SCL by interní write-cycle
stejně nezkrátilo; žádný testovací zápis do zařízení neproběhl.

## MOT: životnost podle událostí, ne podle stáří

- Odstraněna konstanta30s i destruktivní idle blok z DAB::task. Odstraněn pouze
  jeho nyní neplatný timeout log. **Žádný nový provozní UART/LCD výpis ani příkaz.**
- Partial objekt ve stávající51200B aréně zůstává neomezeně dlouho. Pauza není
  memory leak; neprobíhá žádná nová alokace ani background timeout task.
- Stejný platný header TID/SID/CID/BodySize zachová bitmapu, délky a zabalené bytes.
  Chybějící segment se původním storeSlideshowSegment vloží na správné místo.
- Platný nový objekt může partial nahradit; neplatný/truncated cizí header jej
  nezničí. LAST header nesmí deklarovat header delší než přijatá kompletní data.
  Konfliktní známý BodySize ukončí rozpracovaný collector bez smíchání bytes.
- Dokončení vyžaduje úplnou souvislou bitmapu a shodu se známým BodySize. Ten
  umožní bezpečné dokončení bez LAST, případně i při headeru přijatém až po těle.
  Bez BodySize dál platí původní LAST/repeated-zero pravidla, nikdy přes vnitřní díru.
- Zachovány duplicate byte/length checks a LAST boundaries. Shodný duplicate
  nepřidává bytes a může doplnit LAST. Progress je nejvýše99 do validního publish.
- Collector uchovává SID, canonical CID a generation. CID0 se normalizuje na
  aktivní endpoint. Paket druhého souběžně aktivního endpointu nesmí smíchat obraz;
  opuštěný endpoint se invaliduje. Původní processDsrvReply filtr zůstává a stale
  command completion se dál odmítá přes command generation.
- Tune/service, band boot/recovery a OFF ruší partial kontext. Boot/recovery/OFF
  nyní také ruší dokončené TID/hash dedup metadata, aby stejné TID v nové generaci
  nebylo omylem blokované. Stejné dokončené TID ve stejném kontextu zůstává dedup.
- Dokud UI vlastní publikovaný objekt, nové bytes ho nepřepíšou. Po acknowledge
  zůstává cache; nový header sám ji nemaže. První validní nové body data mohou
  jedinou arénu znovu použít. JPEG/PNG/Adam7 validace a rendering se nemění.

## Ověření

```powershell
pio run
python tests/run_sls_tests.py
# Navíc důkaz regrese starého skutečného driveru:
python tests/run_sls_tests.py --mot-baseline 28097fdd1ed8dbc6414f21884738896c6db230d5
```

- Baseline i finální PlatformIO env FMDABRadio: SUCCESS.
- Adam7:92 validních a197 hostile vstupů PASS; noninterlaced PNGdec:9 fixtures PASS.
- Skutečný slideshow renderer:100 reject/reopen cyklů, formátový overlay,
  validate-before-TFT a ownership/render failure PASS.
- radio_scheduler, fm_features, dab_service_switch, mot_assembly, jpeg PASS.
- Bus konfigurace a nižší8MHz/2MHz fallback PASS. Overclock32/40MHz TFT,20MHz radio
  a1MHz I²C jsou očekávaně odmítnuty při kompilaci. Test kontroluje i transformaci
  knihovny při initR a absenci idle resetu v DAB::task.
- Nový harness kompiluje **skutečný src/si4684.cpp**, používá fake millis/micros,
  SPI/GPIO a binární DSRV feed přes processDsrvReply, nikoli kopii assembleru.
  Test20 segmentů, chybí#7: pauzy45/90/180s se stejným headerem i bez něj, exact
  bytes,100% a jediný publish. Dále wraparound, absence headeru, několik karuselů
  s reorder/duplicate, opožděný LAST, BodySize bez LAST a header po celém těle.
- Reálný driver testuje nový platný/neplatný TID, konfliktní délku/duplicate/LAST,
  SID/CID a parallel endpoint, stale command generation, tune/service/band/recovery,
  OFF→ON, reuse stejného TID v novém kontextu,51200B hranici a segmenty255/256.
  Publikovaná bytes jsou chráněna během UI ownership, header nad cachí nemění data.
- Skutečné writeCommand/readReply callbacky se testují s oběma CS původně LOW:
  pořadí HIGH/HIGH před transakcí, radio LOW při přenosu, oba HIGH na konci,
  clock/mode0 i framing byte a command/argument bytes jsou ověřeny.
- Baseline reproducer skutečně sestaví starý driver z uvedeného SHA a požaduje
  konkrétní selhání zachování608B po45s. Opravený driver stejný scénář splní.
- Host build ukazuje existující warning resultName: enum Aborted není explicitně
  uveden ve switch. Pro tento integrační test je pouze -Wno-error=switch;
  nesouvisející logování ve firmware se neměnilo. Není to nový runtime výpis.
- ASan/UBSan runtime v místním Windows GCC chybí, viz původní resource report;
  sanitizer ověření ani elektrické vlastnosti sběrnic nejsou vykazovány jako PASS.

## Zdroje a HW podmínky

| Metrika | Výchozí HEAD build | Výsledek | Rozdíl |
| --- | ---: | ---: | ---: |
| Statická RAM |108888 B|108896 B|+8 B|
| Flash |451381 B|451709 B|+328 B|
| MOT arena |51200 B|51200 B|0|
| Shared image workspace |76800 B|76800 B|0|

Přírůstek RAM je generation metadata a zarovnání, nikoli druhý image buffer.
Host fake hardware není elektrická simulace ani měření heap/latence na ESP32.

**HW nebyl dostupný; nic nebylo nahráno.** Na opravené mechanicky stabilní desce
ověřit tuto matici (vše pouze v mezích specifikace):

| Oblast | Nastavení k porovnání | Scénáře / důkaz |
| --- | --- | --- |
| Si4684 |10MHz proti původním2MHz|NVSPI boot, DAB/FM, RDS/DLS/DSRV/MOT, recovery,50+ přepnutí|
| TFT |≈13,33MHz proti8MHz|menu/list, dlouhé JPEG/PNG/Adam7 SLS, LCDRESET, obraz/orientace bez zbělení|
| EEPROM |400kHz, případně nižší100kHz|jen opakované čtení stejných stanic a rekordů přes page/address hranice|
| MOT |libovolný povolený takt|chybějící blok a karusel delší30s na11A/11B; progress do99 a jediné dokončení|
| Signály |měřit skutečné SCK/SCL, CS, hrany|dodržení zatížení, setup/hold/rise/fall a stabilního napájení|

Neprovádět40/32MHz TFT ani1MHz I²C experiment podle starého textu zadání; uživatel
je výslovně odmítl. Neprovádět automatické zápisy či endurance test EEPROM.
Vyšší povolený takt není důkaz odstranění dřívější mechanické závady po tlaku na PCB.

## Přesný seznam souborů této změny

- `platformio.ini`
- `src/BusConfig.h` (nový)
- `scripts/st77xx_clock.py` (nový)
- `src/RadioDAB.ino`
- `src/eeprom.ino`
- `src/si4684.cpp`
- `src/si4684.h`
- `tests/run_sls_tests.py`
- `tests/test_mot_assembly.cpp`
- `tests/test_mot_driver.cpp` (nový)
- `tests/test_bus_config.cpp` (nový)
- `tests/test_bus_build.py` (nový)
- `tests/radio_shims/Arduino.h` (nový)
- `tests/radio_shims/SPI.h` (nový)
- `tests/README.md`
- `IMPLEMENTATION_REPORT_SPI_I2C_MOT.md` (nový)

EPG/UI backlog, nové serial monitory, GPIO, EEPROM schema, firmware adresy,
audio/AF, decode aréna a kooperativní radio scheduler nejsou měněny.

## Následné schválené doplnění: LCD příjem vs. uložený partial

Uživatel dodatečně schválil 30s timeout **jen indikace příjmu**, nikoli dat.
`mot_assembly::RECEPTION_ACTIVITY_MS=30000` a `DAB::slideshowReceiving(now)`
rozlišují platný rozpracovaný collector od aktuální aktivity. Používá se čas
posledního skutečně akceptovaného headeru, segmentu nebo shodného duplicate,
nikoli syrový MOT packet counter (ten zahrnuje i odmítnuté pakety).

Po přesně30s bez platných dat LCD změní indikaci na šedé `MOT --`, bez RX/procent.
Partial bytes, bitmapa, uložený progress a možnost pozdějšího dokončení zůstanou.
Další platný paket aktivní indikaci obnoví. Dostupný dokončený obrázek má prioritu
a zůstává zelené `OK`, ne procenta rozpracovaného příjmu. Odstraněny původní
3s raw-packet activity a5s ready UI hold časovače, aby po ztrátě platnosti cache
nevznikalo falešné RX/OK. Timeout přepíná pouze status dirty region, ne celý displej.
Žádné nové UART logy, automatické resety ani EPG integrace.

Další upravené soubory: `src/Ui.ino`, `src/mot_assembly_policy.h`; rozšířeny již
uvedené `si4684.cpp/.h`, MOT testy a runner (baseline test výslovně vynechá nové API).
Build s tímto doplněním:108880B RAM a451625B flash. Předchozí tabulka zachycuje
P0 stav před doplněním LCD, nikoli aktuální součet. Host testy ověřují hranici
29999/30000ms, timestamp0, uint32 wrap, neobnovení po vadném/cizím paketu,
obnovení po validním duplicate/headeru, zachování608B/95% a dokončení po další
90s pauze. Hotová cache zůstává dostupná i po180s. Vše bez HW ověření a flashování.
