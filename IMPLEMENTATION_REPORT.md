# Implementace SLS P0 – 26. 10. 2026

Implementována pouze schválená první etapa. Aktuální SHA a porovnání algoritmů
jsou v PORT_MATRIX.md. EPG L0–L4 ani návrat Tech/Tune nejsou implementovány a
nejsou vydávány za hotové funkce.

## Implementované změny

1. Nezávislý Adam7 decoder adaptovaný z referenčního png_adam7.cpp. Žádný include
   epg_core.h, žádná EPG alokace. Používá původní PNGdec inflate se stavem,
   dictionary, paletou a scanlines v existující caller-owned aréně.
2. Přijímá validní interlaced PNG nejvýše 320×320 a 51200 B. Typy 0/2/3/4/6:
   grayscale 1/2/4/8/16, paleta 1/2/4/8, RGB/gray-alpha/RGBA 8/16 bitů.
   Kontroluje signaturu, IHDR, chunk bounds/typy/CRC, PLTE/tRNS pořadí a rozsahy,
   souvislost IDAT, filtry, zlib stream/checksum/trailer/excess data a IEND.
   Neznámé kritické chunky a ostatní typy jsou odmítnuty.
3. Nearest-neighbor fit se zachováním poměru stran a centrováním. 320×320 se
   zobrazí jako 128×128 (16px černé okraje). Skládá se vždy celý 160×64 nebo menší
   cílový pás ze všech sedmi passů; sparse/neúplné Adam7 řádky se na TFT neposílají.
4. První průchod ověří celý obraz bez TFT. Pak pro každý pás proběhne kompletní
   dekódování a ověření traileru před jeho publikací. Průběžný yield probíhá
   mimo TFT transakce, bez volání radio API. Published MOT zůstává po celou dobu
   immutable; nedochází k jeho předčasnému acknowledge.
5. Úspěch uvolní published ownership přes dosavadní acknowledge a zachová cache.
   Neplatný první průchod přes discard uvolní nový objekt a ponechá předchozí
   pixely v GRAM. Není-li starý compressed obraz bezpečně cachován, neslibuje se
   jeho opětovné dekódování. Po chybě až během renderu UI obnoví poslechový view.
6. MOT porovnává délku a bytes duplicitního segmentu, odmítá konflikt a rozporné
   LAST hranice. Toto je malá policy adaptace, ne druhý assembler. Retransmise
   stejného segmentu může doplnit LAST; repeated-zero kompatibilita zůstává.
7. JPEG jádro zůstalo nezměněné: prokázaný referenční bugfix nebyl nalezen.
   Wrapper loguje scans, skutečně vykreslené MCU rows, progress callback counts,
   validation/render časy. Adam7 loguje stage/pass/row a počet kompletních dekódů.
   SLS/RAM before/after poskytuje internal 8bit/32bit free/largest, min8 a stack HWM.

PNG interlace=0 zůstává na PNGdec. Omezení jejího grayscale 2/4-bit a packed
palette alpha RGB565 převodu jsou preexistující a nejsou v této etapě opravena.
U Adam7 jsou stejné kombinace včetně transparentnosti pixelově ověřené.

## API a soubory této etapy

- Nové produkční: `src/DataBytes.h`, `src/DataCrc.h`, `src/PngAdam7.h`,
  `src/PngAdam7.cpp`, `src/PngAdam7Scale.h`.
- Upravené: `src/Slideshow.cpp/.h`, `src/Ui.ino`, `src/si4684.cpp`,
  `src/mot_assembly_policy.h`, `tests/test_mot_assembly.cpp`,
  `tests/test_jpeg.cpp`, `tests/README.md`.
- Nové testy: `tests/run_sls_tests.py`, `tests/test_png_adam7.cpp`,
  `tests/test_png_standard.cpp`, `tests/test_slideshow_render.cpp`,
  `tests/host_shims/` (pouze testovací doubles, nikdy firmware).
- Reporty: PORT_MATRIX.md, IMPLEMENTATION_REPORT.md, RESOURCE_REPORT.md,
  TEST_PLAN_HW.md.

`renderRamSlideshow` dostal volitelný poslední argument `bool* displayModified`;
výchozí nullptr zachovává existující volání. Ukazuje, zda render již změnil TFT.
`PngAdam7Decode` je headless caller-workspace API s řádkovým a yield callbackem.
`duplicateMatches` a `lastBoundaryValid` jsou malé hostově testované MOT policy.

V souborech Ui/si4684 se zachovaly dřívější necommitnuté LCD/SPI úpravy. Nebyl
vytvořen smíšený commit obsahující cizí předchozí práci. Zdrojové soubory jsou
upraveny přímo, ne pouze dodány jako diff. Specifikační patch EPG_20261023.patch
nebyl aplikován: patří k zatím neschválenému EPG milníku.

## Automatické ověření

Z kořene repozitáře:

```powershell
pio run
python tests/run_sls_tests.py
```

- Baseline i výsledné `FMDABRadio` sestavení: SUCCESS.
- Původní testy radio_scheduler, fm_features, dab_service_switch, mot_assembly,
  jpeg: PASS před portem i po portu.
- Adam7: 92 validních fixtures a 197 hostile vstupů. Všechny legální color/depth
  kombinace, filtry 0..4, 1×1/3×7/17×13/319×127/320×240/320×320, split/empty IDAT,
  tRNS, CRC a checksum, truncated/excess zlib, chunk order, palette overflow,
  invalid dimensions/header, přesně 51200 B a odmítnutí 51201 B, deterministické
  mutace. Porovnání všech pixelů s nezávislým Python encoderem.
- Ověřeno škálování stejným stripe callbackem jako firmware, canary bounds,
  posunuté workspace 0..7 B, nedostatečná kapacita, cancel/reuse a 100 opakování.
- PNGdec noninterlaced: devět fixtures, validační/renderovací průchod. Grayscale
  1/8, opaque packed paleta 1/2/4, paleta 8 s alpha, RGB8, gray-alpha8, RGBA8.
  Alpha rounding tolerance nejvýše jeden RGB565 krok na kanál odpovídá původnímu
  PNGdec převodníku; Adam7 porovnání je přesné, bez tolerance.
- Skutečný Slideshow.cpp s hostovým TFT spy: plné škálování/černé okraje,
  invalidní PNG bez jediného TFT zápisu, 100 reject/render cyklů s jednou velkou
  alokací, střídání Adam7/PNG/JPEG a vložená chyba mezi pásy → false + modified.
- MOT nové testy: shodná i konfliktní data/délka, null, overflow offset a LAST.
  Jde o policy testy, nikoli porovnání obou úplných radio driverů na RF trace.

Sanitizer pokus `python tests/run_sls_tests.py --sanitize` zde nešel linkovat:
Windows GCC instalace nemá libasan/libubsan. Není vykazován jako PASS. Runner
volbu zachovává pro vybavený host. Canary/mutační testy nejsou náhradou ASan.

## Zachované hranice a bezpečný fallback

GPIO, NVSPI adresy, EEPROM schema/scan, Controls, AF, scheduler, retry/recovery,
MOT arena, serial architektura a TFT init/reset se nemění. Žádná nová ISR/task.
EPG není zavedeno: žádná EPG menu položka, nefunkční stub ani zdánlivé enable flagy.
Při alokační chybě zůstává stávající vypnutí slideshow a rádio pokračuje.

**Na skutečném ESP32 nebylo testováno ani nahráno.** Yield není asynchronní image
decoder ani obsluha CTS; ověřit dobu decode a ztráty na 11A/11B podle TEST_PLAN_HW.
EPG integrace vyžaduje další souhlas a samostatné RAM/ownership testy.
