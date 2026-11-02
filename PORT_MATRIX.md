# Selektivní převzetí SLS/EPG – audit, etapa P0

Datum: 26. 10. 2026. Schválená etapa: audit, Adam7 a prokázané opravy JPEG/MOT.
EPG ani návrat Tech/Tune zatím nejsou schválenou implementační etapou.

## Reprodukovatelný základ

SHA ověřena proti `git ls-remote ... HEAD refs/heads/main` obou projektů:

- FMDABRadio: `a6e40d826acd02720b1b1e79621e49c35835c5b4`.
- SI4684-FMDAB-Receiver: `4a4b2d205a2f8c36b56378ec40dcc2dfa560414d`.

Lokální HEAD odpovídaly vzdálenému main. Referenční worktree byl čistý.
Cíl navíc obsahoval již existující necommitnuté LCD/SPI úpravy v Display,
RadioDAB, Ui, si4684.*, dab_service_switch_policy a odpovídajících testech,
README a FIX_LCD_SPI_REPORT. Tyto změny se zachovávají a patří do baseline,
nikoli do velikostního přírůstku tohoto portu. Žádný pull/merge/reset neproběhl.

## JPEG: porovnání funkci po funkci

Porovnány celé `JPEGdecoder.cpp/.h` pomocí `git diff --no-index`, nikoli jen
názvy funkcí. „Stejné“ znamená stejné dekódovací rozhodování v těchto SHA;
rozdíly DIAG_PRINTF/JPEG_DIAG_PRINTF a TFT_eSPI/JPEGDisplay nejsou bugfix.

| Funkce / oblast | Výsledek porovnání | Rozhodnutí a ověření |
| --- | --- | --- |
| MemoryFile read/seek/valid/position/close | stejné bounds a EOF | LEAVE; truncated fixtures |
| JPEGpreflight marker walk / SOI / EOI | stejné | LEAVE; chybějící EOI a marker testy |
| Preflight rozměry / scaleDivisor | lepší v cíli: 1..4, kontrola neplatného viewportu; reference 1..2 | LEAVE; 320×320 → 107×107 |
| Preflight SOF0 / SOF2 / APP0 | stejné | LEAVE; baseline/progressive fixtures |
| Preflight unsupported multiscan / components | stejné | LEAVE; multiscan a CMYK rejection |
| JPEGpreflightName | stejné výsledkové stavy | LEAVE |
| zigzag, pjExtend, pjClamp | stejné | LEAVE |
| nzSet / nzGet | stejná nonzero bitmapa | LEAVE; progressive equivalence |
| PJBitReader / pjBuildHuff / pjHuffDecode / pjReceive | stejné | LEAVE; poškozená DHT a entropy |
| PJDecoder / static decoder state | stejné bounded single-owner řešení | LEAVE; žádná nová instance |
| pjComputeBlockOffsets / pjGlobalBlockIdx | stejné | LEAVE; sampling 420/422/444 |
| pjRead8 / pjRead16 / pjSkip | stejné | LEAVE; truncated marker |
| pjParseDQT | stejné kontroly precision/index/length | LEAVE; invalid table tests |
| pjParseDHT | stejné délky/indexy/Huffman bounds | LEAVE; malformed DHT |
| pjParseSOF | stejné sampling 1..4, max. 10 bloků/MCU a table index | LEAVE |
| pjParseSOS | stejné komponenty a selektory tabulek | LEAVE |
| pjAcceptProgressiveScan | stejné pořadí DC/AC a refinement úrovní | LEAVE; invalid refinement |
| pjParseDRI | stejné | LEAVE; restart fixtures |
| pjDecodeDCFirst / pjDecodeDCRefine | stejné | LEAVE; progressive grayscale/YCbCr |
| pjDecodeACFirst / pjDecodeACRefine | stejné | LEAVE; přesná shoda s baseline |
| pjDecodeBaseline / pjDecodeBlock | stejné | LEAVE |
| pjRowBlockIndex | stejné | LEAVE |
| pjTakeEntropyMarker / pjConsumeRestart | stejné striktní pořadí markerů | LEAVE; chybný restart |
| pjScanFailure | jen jiné logovací rozhraní | LEAVE; nepřenášet constants.h |
| pjDecodeScan | stejné entropy a MCU bounds | LEAVE |
| pjIDCT / pjYCbCrToRGB565 | stejné | LEAVE; pixelové porovnání fixtures |
| pjOutputMCURow | lepší v cíli: scale 1..4 + nezávislý JPEGDisplay | LEAVE; ST7735 bounds |
| pjSkipToMarker / pjSkipEntropy | stejné | LEAVE |
| pjProcessFileForRow | stejné scan restarts a validační pravidla, jiné log makro | LEAVE |
| pjDecodeBaselinePass | stejný caller workspace; jiný TFT adapter | LEAVE |
| pjDecodeProgressivePass | stejný row workspace + nonzero bitmap, bez full-frame koeficientů | LEAVE |
| JPEGdecodePass | stejné jádro, lepší scale v cíli | LEAVE |
| JPEGvalidate | stejné úplné dekódování bez TFT | LEAVE; validace zůstává před renderem |
| JPEGdecoder | jen adaptér grafické knihovny a scale rozdíl | LEAVE |

Prokazatelný chybějící JPEG bugfix v referenci nebyl nalezen. Zdrojové soubory
JPEGdecoder.cpp/.h zůstaly nezměněné. Statistiky scanů, renderovaných MCU řádků,
progress callbacků a časů jsou přidány pouze v integračním Slideshow.cpp.

Při cílovém viewportu 160×128 preflight připouští nejvýše 640×512 přes divisor 4;
skutečné přijetí ještě omezuje sampling a dostupná row/bitmap workspace. Není to
příslib podpory libovolného souboru 640×512. 320×320 420 baseline i progressive
jsou ověřené fixtures. Grayscale/YCbCr zůstávají, CMYK/YCCK není implementováno.

## Další algoritmy a náklady

| Oblast | Rozhodnutí | Důvod / rozsah |
| --- | --- | --- |
| Adam7 PNG parse, CRC, bounded inflate, unfilter, sample, RGB565 | PORT + ADAPT | v cíli chyběl; PngAdam7.*, byte view bez EPG; reference 320×240 rozšířena na testovaných 320×320 |
| Referenční sparse row → ILI9341 draw | NEPORTOVAT; ADAPT | PngAdam7Scale.h skládá dokončené 64řádkové pásy pro 160×128 |
| Shared JPEG/PNG arena / decoderBusy / placement-new | LEAVE | již správně existuje; Adam7 i pás mají společných 76800 B |
| PNGdec noninterlace | LEAVE | původní cesta a šířkový guard 2048 zůstávají, skutečné formáty/šířky dál omezuje PNGdec 1.1.6 |
| PNGdec grayscale 2/4-bit RGB565 | DEFER | v knihovně chybí konverzní větve; preexistující omezení, nepřenášet na Adam7 cestu |
| PNGdec packed palette alpha | DEFER | původní RGB565 konverze ignoruje tRNS pro packed 1/2/4-bit paletu; Adam7 ji správně skládá na černou |
| MOT packed arena / bitmap / bounds / ownership | LEAVE | ekvivalent, žádný druhý assembler |
| MOT header BodySize a transport identity | LEAVE | cíl už dekóduje core a kontroluje limit/exaktní dokončení |
| MOT duplicate payload validation | PORT + ADAPT | reference acceptSlideshowDuplicate porovnává délku/bytes; cílový duplicate je ignoroval před repeated-zero completion |
| MOT LAST boundary | PORT princip + ADAPT | odmítnout segment za známým koncem a rozporný LAST; stejná retransmise smí dodat dosud chybějící LAST |
| MOT / DSRV footer CRC | DEFER | nepředpokládat formát 2B footeru v Si4684 wrapperu bez potvrzeného trace; PNG CRC není MOT transport CRC |
| Chyba po prvních renderovaných pixelech | ADAPT | displayModified výstup a obnovení poslechového UI; invalidní první průchod TFT nemění |
| EPG Core / time / ingress / cache / Now-Next | DEFER | další schválení, nejprve host replay a RAM budget; žádný nový radio příkaz |
| SPI/component/UA discovery a ownership | DEFER | stávající DABService ukládá jen vybraný komponent; před live EPG vyžaduje úplnou mapu, ne automatický routing DSCTy60 |
| ClockRefresh LOCAL→UTC→LOCAL a confidence/cache | DEFER | samostatné testy časových skoků/DST, bez mechanického mazání cache |
| Receive health / loss diagnostics | DEFER mimo P0 | nepřebírat retry/recovery/scheduler; nyní pouze SLS decode timing/RAM a jednotlivé důvody MOT rejection |
| DSRV drain/watchdog/recovery | LEAVE | žádný scheduler port; fyzické chyby po tlaku na PCB software nevyřeší |
| Tech/Tune a tuning debounce | DEFER | konflikt s dřívějším přáním odstranit; vyžaduje výslovný nový souhlas |
| Station list preview→SELECT, dirty rows | LEAVE | již vhodné pro sedm tlačítek |
| Controls / AF deferred events / backlight | LEAVE | jiný cílový HW, ekvivalent existuje |
| Serial diff/push a nové fronty | DEFER | nejprve měření; stávající serial 4KiB se nepřepisuje |
| Unicode/EBU TextCodec / metadata generation filtering | LEAVE v P0 | žádný prokázaný chybějící algoritmus pro tuto etapu |
| Logos / decoder arbitration s EPG | DEFER | první EPG etapa má být textová a bez druhé image arény |
| NVS/direct storage, I2S/TPA6130A2, IR/rotary, ILI9341 | NEPORTOVAT | neslučitelný cílový HW/úložiště, nezahrnuto do etapy |

## RAM a původ

Podrobná čísla a chybějící HW měření jsou v RESOURCE_REPORT.md. Základní
rozhodnutí: 76800 B existující aréna, v ní 47200 B target Adam7 state (+ až 7 B
alignment) a 20480 B pás. Žádná další trvalá velká alokace. Cenou jsou 1 validační
a nejvýše 2 kompletní renderovací dekódy pro obraz vysoký 128 px.

Oba kořenové LICENSE jsou GNU GPL v3. Původ Adam7 a nezávislého test encoderu je
uveden v souborech včetně SHA. Zachována závislost PNGdec 1.1.6 (Apache-2.0;
vložený inflater má vlastní zlib notice); nebyla přibalena nová kopie inflateru
ani měněny licenční hlavičky knihovny. Malý table-free CRC32 helper je připraven
ke sdílení s případným budoucím EPG, bez duplikace CRC tabulky.

## Navazující schválená etapa EPG L0 / LCD (2026-11-02)

Aktuální stav nahrazuje dřívější odklad samotného headless core; podrobnosti
v IMPLEMENTATION_REPORT_EPG_L0_LCD.md. Reference nyní
`df0c8bc9eef4c4598742fd462216675d1e885d81`.

| Oblast | Rozhodnutí | Výsledek |
| --- | --- | --- |
| EPG core/time/cache/CRC | PORT | byte DRAM, OOM cleanup, bounded allocations, saturace statistik, oprava SCIdS |
| Replay fixtures/testy | PORT | historický demo replay a synthetic multi-TID, bez velkého RAW oracle |
| Live discovery/routing/scheduler/clock | DEFER | žádné tuner příkazy ani instance Core |
| EPG UI/serial/runtime ON | DEFER | EPG_ENABLE=1 je pouze compile opt-in |
| Logo decoder integration | DEFER | pouze komprimovaný buffer v neaktivním core API |
| LCD dirty flags | LEAVE + rozšířit | cache jednotlivých polí, statické texty se nepřekreslují |
| Menu/list ovládání | LEAVE | vlastní dílčí rendering, žádná Tech/Tune stránka |

Navazující souhlas: L1 discovery je nyní PORT/ADAPT (UA parser reference,
vlastní omezené inventory a kooperativní integrace target driveru). Live
acquisition/ingress do Core, Now/Next a EPG TFT/serial UI zůstávají DEFER.
Reference textový sprite princip je ADAPT na jednobitový 260 B canvas.
Aktuální mapa datových cest a omezení: IMPLEMENTATION_REPORT_EPG_L1_TEXT.md.
