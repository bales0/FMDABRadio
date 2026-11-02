# HW akceptace – první etapa SLS/Adam7

Stav: připravený plán, nikoli provedený test. EPG zůstává mimo tuto etapu.

## Podmínky

1. Nejprve odstranit mechanickou závadu: dříve tlak na PCB vyvolával power-up
   timeout / command-not-found. Nestabilní spoj není použitelný pro srovnání
   algoritmů. Při aktivní závadě nezapisovat eFuse ani nemaskovat chyby retry.
2. Uchovat referenční build včetně původních LCD/SPI úprav, stejnou anténu,
   napájení, pozici, mux a signál. Zachytit cold start log včetně alloc metrik.
3. Nový firmware sestavit `pio run`; flashnout až jako samostatný uživatelský
   krok. Serial monitor 115200. Žádné automatické nahrání zde neproběhlo.

## Formáty / ownership

- Zobrazit stávající SOF0/SOF2 včetně 320×320, grayscale/420/422/444/restarts;
  potvrdit stejné barvy, celé okraje a stejné škálování.
- Noninterlaced PNG: stávající RGB/RGBA, indexed a monochrome fixtures. Nevydávat
  preexistující PNGdec grayscale2/4 nebo packed palette tRNS problém za regresi
  Adam7; tyto kombinace běžného PNG jsou zaznamenaná omezení.
- Adam7: typy0/2/3/4/6 v podporovaných hloubkách, 1×1, liché17×13, 319×127,
  320×240 a 320×320. Poslední má mít 128×128 centrovaný obraz, alpha na černé.
- Správné CRC vadného entropy/filter/zlib souboru: žádný neúplný pass nesmí na
  TFT. Chyba PNG CRC/truncated file má zachovat předchozí obraz, uvolnit nový
  published objekt a umožnit další validní image. Opakovaný SELECT zobrazí
  cachovaný validní obraz; nezahájí nové sestavování téhož TID.
- Střídat JPEG → PNG → Adam7 → JPEG alespoň 100×. Záměrně po validním objektu
  přijmout chybný nový TID a pak další správný. Během MOT přeladit a ověřit,
  že staré SID/CID segmenty nevytvoří nový obraz.
- MOT trace: reorder, shodný duplicate, konfliktní duplicate/délka, LAST před
  již vyšším segmentem, segment za LAST, chybějící LAST/repeated segment0,
  ztracený segment a nové TID. Conflict se musí odmítnout bez publikace směsi.

## Ovládání a SPI

- DAB 11A a 11B: cold boot, stejný/jiný mux, dlouhá session ≥60 minut.
- Station list CH± musí zůstat preview; SELECT commit. Menu/list nesmí přemalovat
  automatický SLS. Manual/Auto/Off SLS musí fungovat, VOL± musí zůstat funkční.
- DAB↔FM, scan complete/cancel, FM AF off/on a návrat do menu. Tech/Tune se
  nesmí nově objevit. EEPROM seznam a hlasitost se nesmějí ztratit.
- Logickým analyzátorem změřit současné CS, TFT8MHz/radio2MHz a dobu bloků draw.
  Adam7 pump nevydává radio příkazy; SPI nesmí být reentrantní.

## UART a očekávané důkazy

Existující `STATUS` a `DEBUG` zachytit před/po session; `HELP` vypisuje příkazy
konkrétního buildu. Nepoužívat neimplementované EPG STATUS/DEBUG či replay CLI.

- `[SLS] shared decoder=76800 ... Adam7=47200 stripe=20480 ...`: jednorázová
  inicializace sdílené arény. Neočekávat novou alokaci při každém SELECT.
- `[SLS/Adam7] OK ... stage=ok ... runs=... validate=... render=...`:
  nejvýše3 runs pro cílovou výšku128; rejected má uvést stage/pass/row.
- `[SLS/JPEG] scans=... renderedMCUrows=... pumps=... validate=... render=...`:
  callback count není počet unikátních JPEG řádků; progressive znovu čte scany.
- `[SLS/RAM] before/after`: archivovat i8 free/largest, i32 free/largest, min8,
  stackHwm. Free/largest nesmí po každém stejném obrazu monotónně klesat.
- `[SLS][WARN] conflicting duplicate` / `conflicting LAST`: odmítnutí packetu,
  nikoli decoder failure. PNG rejection je jiný diagnostický stav.
- Existující `[DIAG]`, `[DAB/CTS]` a DSRV/MOT/OVF/serialDrop: srovnat baseline
  vůči portu. Čítače samy neprokazují lokalizaci všech ztracených bloků.

Při stejném signálu nesmí přibýt timeouty nebo ztráty SLS či vstupu v souvislosti
s renderem. Měřit zvlášť UI/SLS OFF, manual bez renderu a aktivní Adam7. Yield
sám nezaručuje obsluhu kooperativního CTS. Pokud se to zhorší, zastavit EPG
integraci a vrátit měření/důkazy; nepřidávat automatický TFT/radio reset.

EPG OFF/ON, multi-SID discovery, clock/DST a L0 replay budou testy další fáze
teprve po novém souhlasu. V tomto buildu žádné EPG příkazy ani buffers nejsou.
