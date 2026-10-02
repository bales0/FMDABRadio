# FMDABRadio – fix report

Datum ověření: 2026-10-02

## Opravené chyby

- MOT assembler odmítne segmenty mimo rozsah `0..255` ještě před přístupem
  do bitmapy nebo pole délek. Kontrola velikosti arény je provedena bez
  podtečení a zachovává packed 50 KiB buffer, duplicate/LAST/repeated-zero i
  přepnutí objektu.
- Každý FM tune a seek nyní invaliduje `PI`, `PTY` a `ECC` společně s již
  resetovanými PS, RadioTextem, RDS sync, TP/TA, CT validitou a AF seznamem.
  AF tedy nemůže přijmout kandidáta podle PI předchozí frekvence.
- Automatický AF probe je povolen pouze v běžném textovém listening view.
  Nespustí se během scanu, startu pásma, menu, seznamu stanic, slideshow ani
  preview stanice.
- První tlačítková událost během AF se uloží do jednoho bounded deferred slotu.
  Po návratu na původní frekvenci se znovu zpracuje; další události zůstávají
  ve frontě `Controls`. Platí i pro BAND.
- Úspěšně parsovaný DAB service list zvyšuje monotónní generation counter.
  DAB scan po validním tune počká 1 s, zachytí první platnou generaci, jednou
  vyžádá explicitní refresh a uloží až novější generaci. Při 6s hard timeoutu
  uloží poslední platný seznam. Prázdný label již neblokuje multiplex a nadále
  dostane stabilní fallback název.
- Service-list generace nyní zachovávají nejlepší neprázdný label pro stejné
  `SID + COMP_ID`. Při běžném poslechu se nové názvy aktuálního multiplexu po
  jednom záznamu synchronizují do globálního EEPROM seznamu; prázdná data nikdy
  nepřepíší známý název.
- MOT header core nastavuje skutečnou `BodySize`, takže UI může během příjmu
  zobrazovat procenta. Dokončený transportní identifikátor se pamatuje a stejný
  objekt se po zobrazení znovu nesbírá. Stejný obrazový obsah pod novým ID se
  nerozkresluje podruhé a pracovní buffer se přitom korektně uvolní.
- Při DAB tune/service requestu se ihned invaliduje kodek, bitrate, sample rate,
  audio mode a PTY. Do přijetí čerstvých údajů UI zobrazuje `Audio loading...`.

## Zachované chování

- Transactional EEPROM commit, cancel/no-stations ochrana staré databáze,
  raw 16B DAB label a charset, pouze audio služby s nenulovým SID.
- 16bitové COMP_ID maskování a kompatibilita starých EEPROM záznamů.
- Dvoufázový FM scan a ochrana proti dynamic PS.
- Si4684 NVSPI boot, adresy firmware, GPIO/SPI, EEPROM layout, audio gain,
  hlasitost, renderer a MOT/JPEG/PNG architektura.
- Slideshow decoder byl prověřen: null workspace se odmítá před nastavením
  `decoderBusy`, po jeho nastavení všechny cesty končí společným uvolněním a UI
  zahazuje publikovaný objekt po úspěšném i neúspěšném renderu.

## Testy a build

- `test_radio_scheduler`: PASS
- `test_fm_features`: PASS
- `test_dab_service_switch`: PASS
- `test_mot_assembly`: PASS
- `test_jpeg`: PASS
- PlatformIO `env:FMDABRadio`: SUCCESS
  - RAM: 104184 / 327680 B (31.8 %)
  - Flash: 439605 / 1310720 B (33.5 %)

Testy MOT pokrývají segmenty 0, 255, odmítnutí 256 a 32767, duplicate,
LAST, repeated segment 0 bez LAST, změnu objektu a hranici 50 KiB arény.
Policy testy dále ověřují generační podmínky DAB scanu, AF validaci PI,
timeouty, zákaz AF v modálních stavech a deferred user event.

Hardwarový příjem nebyl v tomto prostředí dostupný; chování živého DAB
service-listu, RDS a MOT je proto nutné potvrdit na přijímači po nahrání
vytvořeného firmware.

## Změněné soubory

- `src/RadioDAB.ino`
- `src/Ui.ino`
- `src/eeprom.ino`
- `src/si4684.cpp`
- `src/si4684.h`
- `src/dab_scheduler_policy.h`
- `src/fm_af_policy.h`
- `src/mot_assembly_policy.h`
- `tests/test_radio_scheduler.cpp`
- `tests/test_fm_features.cpp`
- `tests/test_mot_assembly.cpp`
- `tests/README.md`
- `FIX_REPORT.md`
