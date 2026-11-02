# SLS/MOT: plynulý karusel — 2026-11-02

## Základ a příčina

Ověřeno `git fetch origin main`: lokální HEAD i origin/main jsou
`1f196446e96488f6188840b5fb3134b55c892ee9`. Worktree již obsahoval schválenou
necommitnutou opravu audio startup/recovery; byla zachována a regresně testována.
Bez commit/push, flashování, připojení COM či zápisů do EEPROM.

Prokázané softwarové příčiny:

- Cache A propouštěla první body nového TID B pouze při segmentu 0. Při
  `LAST31 → header → body0..30` se LAST31 zahodil. Pokud vysílač pokračoval
  dalším TID místo opakování B, mohl se stejný problém opakovat bez dokončení
  dalších obrázků; nejde jen o pomalou SPI.
- Dokončené TID se blokovalo trvale, takže stejný ID s později změněným obsahem
  nebyl přijat. `publishedPending` navíc fungoval jako zámek celé arény až do ACK;
  ztracené/odložené potvrzení tedy mohlo blokovat příjem bez časové meze.
  Z předaných logů nelze určit, zda v konkrétním HW běhu opravdu chyběl ACK.
- Platná hlavička jiného TID mohla přepsat stav aktivního kolektoru.
- Konečné přiřazení `_dsrvPending = buffersRemaining > 0` mohlo ztratit novou
  STATUS/IRQ indikaci vzniklou při payload read. Závod nyní reprodukuje host test.

## Vlastnictví jedné arény

| Stav | Co je v aréně | Povolený přechod |
| --- | --- | --- |
| collecting | unikátní fragmenty jednoho SID/CID/TID/generace | libovolné pořadí; žádný idle reset |
| complete/published | hotový raw objekt a komprimovaný image view | oznámení UI; samo o sobě nic nezamyká |
| render-owned | stejné neměnné bajty pod číslovaným render lease | MOT parser nepíše; uvolnění jen odpovídajícím tokenem |
| reusable/cache | hotový objekt, který může UI znovu otevřít | nový TID jej převezme prvním validním body, včetně LAST31 |

`beginSlideshowRender` / `endSlideshowRender` obalují skutečný decode ve všech
výsledkových větvích. Starý token nesmí uvolnit novější lease. Změna tune
generation nemůže potvrdit jiný obrázek. `takeSlideshowUpdate` a ACK řeší pouze
notifikaci, nikoli dobu života zdroje dekodéru. Bez ACK nevzniká permanentní lock.
Nezavádí se destruktivní timeout ani nové vlákno.

Header jiného TID se uloží do malé metadata položky a nemaže bitmapu/cache.
Nový nezastaralý body může převzít i rozpracovaný kolektor libovolným segmentem;
starý TID se zapamatuje v osmičlenné historii, aby opožděné pakety nepřepnuly
nový kolektor zpět. Historie je vázána na SID/CID a tune generation, nikoli na čas.
Neplatné délky, konfliktní duplicity/LAST a cizí kontext nepoškodí platné fragmenty.
Skutečná změna kontextu ruší staré údaje; 30 s ovlivňuje pouze LCD RX indikaci.

### Recyklované TID a dedup

Raw segmenty zůstávají po kompletaci na místě; dekodér dostává pointer+offset
do stejné arény. Platí to i pro prefix před JPEG/PNG nebo padding za JPEG EOI.
Dosavadní délky segmentů lze znovu použít k přesnému porovnání přijatých bajtů.

Recyklaci dokončeného stejného TID otevírá **validní header + body0**. Shodné
segmenty cache nepřepisují a nepublikují. Pokud se liší až vnitřní segment,
bitmapa 32 B eviduje, které shodné segmenty už v tomto cyklu skutečně dorazily.
Pouze ty se bezpečně zkompaktují do nového kolektoru; nepřebírají se nepozorované
části starého obrazu. Test pokrývá stejné TID, BodySize, header i body0 se změnou
až uvnitř segmentu 7. Nový úplný obsah se deduplikuje stávající délkou+content
hashem; shodný obsah pod jiným TID nevytváří další redraw.

Omezení: wire TID není neomezené pořadové číslo. Až osm zapamatovaných odstavených ID
se odmítá během jiného aktivního kolektoru; jejich pozdější legitimní návrat
nad cache vyžaduje header+body0. U stejného nedokončeného TID nejsou konfliktní
pakety naslepo prohlášeny za nový obraz. Libovolné úmyslné promíchání verzí bez
rozlišující on-air identity nelze spolehlivě rozpoznat. Nezavádí se domnělá
monotónnost TID ani časovač zahazující validní fragmenty.

## UI a DSRV

Manual ponechá cache do příchodu nového body. Po jejím převzetí nelze současně
znovu dekódovat A a skládat B ve stejné aréně: otevření zobrazí existující čekací
obrazovku a po kompletaci B jej vykreslí. Již viditelný snímek A zůstává během
sběru B v GRAM. Explicitní čekání na obrázek přežije i content dedup shodného B.
Auto, menu/modal, scan a Off mají explicitní notifikační přechody bez lease.
Neplatný formát odmítnutý před TFT zápisem ponechá poslední GRAM obraz; skutečné
selhání během renderu používá dosavadní bezpečný návrat na listening obrazovku.
Bez druhé image cache nelze slibovat obnovení A po částečném překreslení B.

Po spotřebování počáteční DSRV indikace se nová STATUS indikace z payload read
a ISR latch kombinují s `buffersRemaining`. Prázdná odpověď nenechá sticky bit
v nekonečné GET smyčce; zůstává 50ms idle polling fallback. ISR stále jen hlásí
hranu. Burst limit 4 a fairness RF/audio/EPG se nemění. Skutečně doložená SLS cesta
je PAD `src=1, DSCTy=60`; standard-data `src=0` se nově neroutuje bez potvrzení.

## Měření a ověření

Reprodukovatelné testy:

```powershell
pio run -e FMDABRadio
python tests/run_sls_tests.py --mot-baseline 28097fdd1ed8dbc6414f21884738896c6db230d5
python tests/run_audio_tests.py
python tests/run_epg_tests.py
python tests/run_ui_tests.py
python tests/run_mot_benchmark.py
```

Vše PASS. Skutečný driver ověřuje A→B→C, bitmapy a přesné bajty, reverse/LAST-first,
shodný obsah, recyklaci ID, prefix image view, lost ACK, lease a stale token,
45/90/180s mezery a millis wrap, kontext/off/on, poškozené pakety a STATUS/ISR
race. UI test používá skutečné UI funkce s reálným driverem a decoder double;
actual JPEG baseline/progressive, PNG/Adam7 a renderer mají samostatné regresní
testy. Zachována audio recovery, FM/DLS/scheduler a EPG L0/L1 ON/OFF.
Existující host warning `Aborted` v resultName zůstává výjimkou stejně jako dříve;
ostatní host warnings jsou errors. HW ani sanitizer se tímto během neověřovaly.

Benchmark používá matching historické `.cpp/.h/policy` v TEMP, fake SPI a clock,
minimální CTS 3 ms pro DSRV / 2 ms pro metadata. Nejde o RF/CPU benchmark.
Ve scénáři A→B se opakovaný LAST B vysílá až v t=1000 ms: starý HEAD dokončil
sekvenci od prvního přijatého bloku A za 900 ms, nový za **421 ms OFF / 423 ms ON**,
tedy před vynucenou retransmisí. Bajty a konečné hashe jsou shodné, oba TID hotové.
Samotný B (od jeho prvního DSRV bloku, nikoli od začátku A) se v tomto replay
zkrátil ze 647 ms na 170 ms v obou nových variantách. Rozdíl není RF benchmark.
U izolovaného 16309B objektu zůstalo 176 ms do complete a 181 ms aktivního drainu.
V modelu byl IRQ→GET ve stejném ticku, polling příklad čekal 25 ms, maximální
GET mezera běžně 8 ms. Již rozběhnutý UA dotaz přidal při dalším burstu 2 ms;
17 komponent se zpracovalo bez hladovění DSRV. Nejde o mez pro skutečný přijímač.

Syntetická 421ms pauza bez Dab.task prodloužila GET mezeru na 426 ms a drain
na 602 ms. Host FIFO je neomezená: nelze z toho vyvozovat skutečné overflow,
audio výpadek ani procentuální RF zrychlení. Adaptivní drain se proto nezavádí.

Volitelný `FMDAB_MOT_DIAGNOSTICS=1` přidává malé čítače/getter bez UART spamu:
DSRV bajty/backlog, unikátní/duplicitní segmenty, render/replay/context/stale/bad
odmítnutí, overflow, collector/complete/notify/render časy a max task gap.
`waitForZero` zůstává 0 pro odstraněný cache filtr nových TID; recyklace ID má
samostatné konzervativní pravidlo výše. Čas firstAt značí zahájení kolektoru,
nikoli RF timestamp před případně odloženou hlavičkou. HW heap/stack lze dál
sledovat stávajícími SLS/RAM výpisy, bez nových monitor příkazů.

## RAM, soubory a zachované části

Před touto etapou včetně audio opravy: RAM 112240 B, flash 459501 B.
Finální default: **RAM 112344 B (+104 B), flash 460785 B (+1284 B)**.
Diagnostická target varianta byla také sestavena, má o 88 B více statické RAM.
Výchozí build je opět bez diagnostiky. Žádná nová heap alokace v MOT cestě,
žádný druhý plný buffer. Arény zůstávají 51200/76800 B; MOT aréna je už v BSS.
Runtime free/min/largest/stack na ESP32 zatím neměřeny.
Xtensa disassembly finálního default objektu ukazuje vlastní stack frames:
MOT parser 96 B, seed replay 64 B, finish 64 B a lease/start helpery po 32 B.
To není součet celé call chain ani provozní stack high-water mark.

Tato etapa mění `si4684.cpp/.h`, `mot_assembly_policy.h`, SLS lifecycle části
`Ui.ino`, MOT/policy/UI/benchmark testy a host SPI shim. Doplňuje tuto zprávu
a testovací dokumentaci. Rozpracované audio soubory a ostatní dirty změny nebyly
resetovány. EEPROM, Radio SPI 10 MHz, TFT/I²C takty, firmware/GPIO, decoder
implementace, EPG parser, FM a samotná audio activation politika beze změny.
Porovnání s pracovním snapshotem před MOT úpravou potvrdilo beze změn také
12 přímo souvisejících audio/tune/recovery funkcí; jejich host regrese prošla.

## Samostatná P2 a praktický HW postup

Resumable renderer vyžaduje rozdělit dekódování do kroků plánovaných z hlavního
loopu, vždy mimo TFT transakci, s lease zachovaným přes všechny kroky. Transport
mezi kroky nesmí rekurzivně volat UI/dekodér ani měnit zdroj. Pro příchozí MOT
je nutná měřená omezená fronta nebo přiznané čekání na retransmisi. Nyní není
Dab.task přidaný do yield/ISR/decoder callbacku a dekodéry zůstávají nedotčené.
Segmenty dodané host parseru při render-owned stavu se odmítnou a musí se opakovat;
zero-loss během výhradního lease bez fronty není slíbeno.

Na HW (dosud neprovedeno):

1. Několik minut stejná stanice bez přeladění, alespoň tři různé TID/obrázky,
   včetně začátku LAST31. Kontrolovat stávající ready/render/DSRV/MOT/OVF/CTS logy.
2. Manual bez otevření → další objekt → SELECT během částečného B; Auto a návrat
   z menu/seznamu. Ověřit MOT RX/procenta/OK a nezměněné rozložení LCD.
3. Stejný obsah pod jiným TID, recyklované TID, opakované SELECT a decode reject.
   Žádný trvalý lock po prvním obrázku ani zbytečný redraw stejného obsahu.
4. Pauza signálu >30 s, návrat chybějícího segmentu, SLS Off/On a změna stanice.
   Oddělit skutečnou ztrátu dat od čekání na karusel; sledovat audio a CTS.
5. Stejné podmínky s discovery ON/OFF, PC timestampování existujících logů,
   SLS/RAM free/min/largest/stack a několik desítek obrázků. Oddělit příjem bez
   renderu od auto renderu. Firmware nebyl při práci nahrán do zařízení.
