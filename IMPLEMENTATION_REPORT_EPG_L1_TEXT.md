# EPG L1 a buffer běžícího textu — 2026-11-02

## Výsledek

Běžící text se skládá mimo LCD a odesílá bez samostatného vymazání. EPG L1
automaticky zjišťuje signalizaci SPI aplikací při poslechu DAB a vypisuje
diagnostiku do existujícího neblokujícího serial monitoru. Programy a TFT
indikátor EPG ještě nejsou zapojeny (L2/L3).

## Porovnání textu s referencí

`SI4684-FMDAB-Receiver` na revizi
`df0c8bc9eef4c4598742fd462216675d1e885d81` používá `FullLineSprite` 308×20,
posun 1 px / 50 ms, složení textu a pozadí v RAM a `pushSprite`.
Při 16bitové barvě jde o 12320 B pixelových dat; sprite sdílí i ostatní pole.

Cíl zachovává svůj posun 1 px / 35 ms, font, UTF-8 a diakritiku. Sdílené
glyph funkce nyní kreslí na `Adafruit_GFX` surface, tedy LCD i canvas.
Použit je `GFXcanvas1` 160×13: 260 B jednou alokovaných obrazových dat a
44 B statického objektu na Xtensa. Černé pozadí a popředí se rozvinou přes
320 B dočasný RGB565 řádek na zásobníku. Jedno adresní okno a jedna TFT
transakce přenesou 2080 pixelů. Žádný nový RGB framebuffer, opakované heap
alokace ani samostatné černé vymazání před textem. Canvas kryje y=53..65,
včetně posledního řádku dolních diakritických znamének; původní textový
počátek y=55 zůstává. Při selhání alokace funguje původní přímé vykreslování.

## Mapa skutečných datových cest a rozhodnutí L1

1. `GET_DIGITAL_SERVICE_LIST` vrací služby i více komponent. Dosavadní
   `DABService` ukládá jen jednu vybranou komponentu; pro discovery nestačí.
   Nové omezené inventory drží všechna SID + čtyřbajtová wire pole, nejvýše
   128 komponent. Celá délka/framing/kapacita se ověří před publikací.
   Při překročení limitu stav `incomplete`, nikdy falešná nepřítomnost.
2. Target vendor parser odděluje 16bitový COMP_ID od příznakových bytů.
   Do `DAB_GET_COMPONENT_INFO` se posílá skutečný COMP_ID, celé wire pole
   se ponechá pro TMId/DSCTy/CA. Nepřebírá se odlišná interpretace CID reference.
3. Kooperativní dotaz má 100ms deadline, nejvýše jeden rozpracovaný příkaz,
   odstup alespoň 250 ms. Používá stávající persistentní reply workspace.
   Odpověď se čte 28 B + nejvýše 255 B UA; parser kontroluje celou UA délku,
   počet, padding a SPI typ 0x007. UA parser převzat z uvedené reference,
   GPL-3.0, s uvedením původu. Nepoužívají se heuristiky podle názvu služby.
4. Každá změna tune/service/boot, vypnutí nebo reconfiguration invaliduje
   generaci. Stale odpovědi se nepublikují. Stejné inventory včetně komponent
   nepřeruší probíhající průchod jen kvůli změně popisku. Po dokončení se UA
   znovu ověřuje po 60 s; chybný seznam má také 60s odklad dalšího pokusu.
5. L1 čeká za DSRV, signalizací, hlasitostí, existujícími metadata příkazy a
   přepínáním streamu. Při čekajícím urgentním provozu nezačíná UA dotaz.
   Timeout/chyba komponenty ukončí její pokus; nesmí sama vyvolat reset rádia
   ani zvyšovat recovery řadu CTS. Host-starvation/genuine statistiky se měří dál.
6. Pasivní DSRV diagnostika přijme jen nepoškozený DSCTy=60, přesný aktivní
   SID/CID, potvrzené SPI UA, nešifrovanou komponentu a správný packet/PAD
   transport. CID=0 se pro tento důkaz nepřijímá. Ani to neprokazuje, že MOT
   obsahuje EPG: sdílená komponenta může nést SLS. Log proto říká pouze
   `passive MOT candidate ... content unverified` a vypíše první pozorování.
7. Žádné nové START/STOP služby, X-PAD konfigurace, drain scheduler, ingress
   do Core ani další EPG buffery. Stávající SLS routing a cache zůstávají.
   Packet EPG na samostatném neaktivním endpointu se tedy zatím nepřijímá.

## Spouštění a význam diagnostiky

`EPG_DISCOVERY_ENABLE=1` je výchozí. App povolí L1 jen v režimu DAB mimo
autoscan a band boot; samotné dotazy navíc vyžadují platný aktivní audio stream.
`EPG_DISCOVERY_ENABLE=0` zakáže discovery příkazy i při runtime žádosti o zapnutí
a umožní SLS baseline. Nejde o nové nastavení v EEPROM/menu.
`EPG_ENABLE=0` nadále vypíná programme Core; je to jiný přepínač.

Serial monitor:

- `[EPG/L1] pending ...`: inventory/průchod ještě není dokončen.
- `[EPG/L1] UA SID=... CID=... result=SPI transport=... CA=...`: ověřená
  signalizace konkrétní komponenty, nikoli dekódovaný program.
- `[EPG/L1] SPI-signalled ... SPI=N failed=0`: všechny odpovědi dokončeny,
  alespoň jedna komponenta inzeruje SPI.
- `[EPG/L1] not-signalled ...`: kompletní průchod bez SPI signalizace.
- `[EPG/L1] incomplete ...`: vadný/příliš velký seznam nebo selhané UA dotazy;
  nelze z toho vyvozovat nepřítomnost EPG.

## Ověření a limity

- `python tests/run_ui_tests.py`: PASS; skutečné render/transfer funkce,
  přesná bitová expanze, barva, stride, jedno okno, bez pre-clear, OOM fallback
  a zachování dílčích aktualizací. Glyph renderer zůstává sdílený produkční;
  host spy není fyzické vizuální ověření diakritiky nebo plynulosti.
- `python tests/run_epg_tests.py`: PASS; L0 regrese a L1 policy + skutečný driver
  s fake SPI, validní/vadná UA, plné inventory, stale/disabled, priority,
  command failure, CTS timeout bez recovery, sestavení discovery ON/OFF.
- `python tests/run_sls_tests.py`: PASS; Adam7/PNG/JPEG, scheduler, přepínání,
  skutečný MOT driver, activity/cache, sběrnicové limity a build adaptér.
- PlatformIO default: PASS, statická RAM 112192 B, flash 457461 B. Proti
  předchozí L0/LCD etapě +1616 B statické RAM, +4884 B flash, plus 260 B
  canvas heap. MOT arena je již v BSS; 76800 B decoder heap beze změny.

Na zařízení nebylo nahráno ani testováno. Pro L1 zbývá RF porovnání s discovery
OFF, CTS/DSRV/SLS/serialDrop a latence; pro text vizuální kontrola při SLS/audio
zátěži. Teprve po L1 ověření má následovat aktivní EPG acquisition/Now-Next (L2)
a zobrazení přítomnosti/programů na TFT (L3).
