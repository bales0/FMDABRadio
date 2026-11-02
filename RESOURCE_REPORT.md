# Zdroje a měření – SLS P0

## Sestavení na stejném výchozím worktree

PlatformIO env FMDABRadio, espressif32 7.0.1, ESP32-WROOM bez PSRAM,
Arduino framework 3.20017, PNGdec 1.1.6. Baseline zahrnuje dřívější LCD/SPI změny.

| Metrika | Baseline | P0 výsledek | Rozdíl |
| --- | ---: | ---: | ---: |
| Statická RAM / 327680 B | 108888 B | 108888 B | 0 B |
| Flash / 1310720 B | 444369 B | 451381 B | +7012 B |
| Permanentní decoder arena | 76800 B | 76800 B | 0 B |
| Permanentní MOT arena | 51200 B | 51200 B | 0 B |
| PNG source line, stávající BSS | 4096 B | 4096 B | 0 B |
| Nová velká heap alokace na obrázek | 0 | 0 | 0 |
| EPG runtime buffers | žádné | žádné | 0 B |

Oprava účtování: PlatformIO RAM již obsahuje 51200 B MOT arénu, protože je
členem globální instance DAB. Pouze 76800 B decoder arena je samostatná heap
alokace. MOT arénu tedy nelze ke statické RAM přičíst podruhé.
Podpora Adam7 nevytváří další 320×240 ani 320×320 framebuffer.

## Overlay uvnitř 76800 B

| Část Adam7 cesty | Velikost / umístění |
| --- | --- |
| Inflate state + dictionary + 2 scanlines + input + palette/alpha + RGB row | target sizeof(Workspace) = 47200 B |
| Zarovnání | nejvýše 7 B uvnitř arény |
| Kapacita před pásem | 56320 B |
| Cílový RGB565 pás, max. 160×64 | 20480 B od offsetu 56320 |
| Celý existující decoderWorkspace | 76800 B |

47200 B je compile-time hodnota z Xtensa objektu PngAdam7.cpp.o:
`PngAdam7WorkspaceBytes()` literal `0x0000b860`. Host x64 má 47224 B, protože
struktury obsahují ukazatele. Tyto hodnoty nejsou měřením ESP32 heap za provozu.
Statický assert kontroluje state+alignment+stripe <=76800 na každém buildu.
PNGdec a JPEG používají stejný začátek arény pouze v jiné době; jejich instance
nejsou živé současně s Adam7 stavem.

## Čas a opakované použití

Adam7 má jeden kompletní validační průchod a ceil(cílová výška/64) renderovacích
průchodů, tedy na 160×128 nejvýše tři kompletní dekódy včetně validace. Je to
vědomá CPU/RAM výměna. Každý pás se vykreslí až po kompletním zlib traileru.
Yield se volá po chunku/Adam7 řádku a po uzavřeném TFT bitmap přenosu; nevydává
radio příkazy a neprovádí reentrantní SPI. Synchronous render ale stále zdrží
kooperativní aplikaci; target worst/median latency musí být změřena na HW.

Host Adam7 test vykonal 92 validních a 197 hostile fixtures, pixelové kontroly,
škálování, misalignment a stovky opakování; běžný průchod této sady trval přibližně
1,8–2,3 s na vývojovém PC (závisí na současném buildu). Není to čas jedné image
ani ESP32 benchmark. Celý renderer má navíc 100 reject/reopen cyklů a kontrolu,
že `heap_caps_malloc` byl zavolán právě jednou. Host doubles reportují heap
telemetrii nulou; tyto nuly se nepoužívají jako důkaz skutečného volného heap.

## Záměrně neměřené bez HW

- ESP32 free/min/largest internal 8bit a 32bit, worst stack a trend po 100 images.
- SPI/UI worst/median loop latency, latence tlačítek a CTS během decode.
- DSRV backlog/lost segment, complete SLS za TID na 11A/11B.
- RF session ≥60 minut a audio/AF/scan regression.
- EPG core/live RAM, EPG OFF/ON benchmark: EPG není součástí této etapy.

Runtime výstup `[SLS/RAM] before|after i8=free/largest i32=free/largest min8=...
stackHwm=...` byl přidán pro HW měření. Oblasti i8/i32 se mohou překrývat,
nesčítat je. JPEG/Adam7 logy oddělují validation/render časy. Stack HWM z ESP-IDF
zaznamenávat podle jednotek frameworku, ne převádět naslepo na počet slov.

Výstupy před/po obrazu samy neprokazují celou cestu IRQ→DSRV→TFT; rozsáhlá loss
telemetrie a EPG budget patří k dalšímu schvalovanému milníku. Pokud HW test
ukáže více CTS chyb nebo ztrát SLS při Adam7, nepokračovat do EPG live a nejprve
vyřešit omezené plánování decode; nekompenzovat to resetováním rádia.

## Navazující EPG L0 / LCD etapa 2026-11-02

Default build: statická RAM 110576 B, flash 452577 B. Proti bezprostřednímu
stavu před touto etapou +1696 B RAM a +952 B flash. LCD cache má na Xtensa
1696 B. EPG Core má 14424 B a pět bufferů celkem 49152 B (součet 63576 B);
Core není nikde instancován, ani při compile opt-in EPG_ENABLE=1.
Tento rozpočet je pro budoucí integraci, nikoli aktuální heap alokace.
MOT 51200 B je již v BSS; pouze decoder 76800 B se přičítá jako heap.
Podrobnosti a hranice měření: IMPLEMENTATION_REPORT_EPG_L0_LCD.md.

## Navazující L1 / textový buffer

Aktuální default build: RAM 112192 B, flash 457461 B (+1616/+4884 B proti L0).
Canvas objekt má 44 B BSS, obrazová data 260 B heap jednou při startu; převod
používá 320 B dočasného řádku na stacku. Zbytek přírůstku BSS patří omezenému
L1 inventory a jeho driver stavu. Programme Core stále není instancován.
Aktuální podrobnosti: IMPLEMENTATION_REPORT_EPG_L1_TEXT.md.

## Audio activation/recovery (2026-11-02)

Default build proti čistému 1f19644: RAM 112192 → 112240 B (+48 B), flash
457461 → 459501 B (+2040 B). Supervisor používá pevný stav, žádné nové heap
alokace ani image buffery. Arény, canvas a sběrnicové takty beze změny.
Host MOT časy jsou syntetické, heap/stack/RF za běhu na ESP32 neměřeny.
Podrobnosti: IMPLEMENTATION_REPORT_AUDIO_RECOVERY_MOT.md.

## Plynulý SLS/MOT karusel

Default po této navazující etapě: RAM 112344 B, flash 460785 B. Proti
rozpracované audio opravě +104 B / +1284 B. Nové položky jsou lease/token,
32B replay bitmapa, osmičlenná TID historie a malé metadata; data zůstávají
v jediné původní 51200B aréně. Nezavádí se heap alokace ani další image buffer.
Volitelná diagnostika má měřených dalších 88 B statické RAM; default je 0.
Podrobnosti, syntetické časování a HW limity: SLS_MOT_IMPLEMENTATION_REPORT_CZ.md.
