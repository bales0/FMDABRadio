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

PlatformIO RAM není totéž co volný heap a neobsahuje dynamickou cenu obou arén.
Jejich součet zůstává 128000 B, nikoli nová rezerva přidaná k původnímu stavu.
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
