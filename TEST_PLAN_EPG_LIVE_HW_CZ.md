# HW protokol EPG L2/L3 — 3. 11. 2026

## Stav

Protokol je připraven, **měření na desce zde nebylo provedeno**. Automatický flash
ani otevření COM se neprováděly. Nejprve po schváleném nahrání ověřit start audia
a SLS; poté následující matici. Nikdy nevydávat žlutou SPI signalizaci za přijatý PI.

## Záznam

Vyžádat a uložit úplný serial log s host timestampy od bootu, ne pouze výběr řádků.
Přiložit revizi/rozpracovaný diff, build EPG ON/OFF, typ ESP32 bez/s PSRAM,
verzi Si4684 FW, kanál/mux, audio SID/CID, anténu a popis kvality příjmu.
Použít stávající neblokující SerialMonitor při 115200. Nespouštět současně dva
procesy nad COM a neměnit radio/TFT/I2C clock pro test.

V logu musí být `[SLS/RAM]`, `[EPG/RAM] before/after`, `[EPG/L1]`, `[EPG/L2]`,
`[DIAG]` a `[DAB/CTS]`, pokud se objeví. Zapisovat:

- UA answered/total/confirmed, SID/CID/wire, packet/PAD a případný CA;
- selected bearer a počet skutečně zobrazených pořadů, directory/SI/PI/CRC,
  route/reject/busy, last valid update a důvod barvy;
- maxBacklog, celkové DSRV overflow a jejich přírůstek za test;
- hostStarved/genuine CTS, max service gap, reset/recovery/audio výsledky;
- INTERNAL|8BIT free/largest/min, INTERNAL|32BIT|EXEC free/largest, stack HWM;
- serialDrop/queue/droppedKeys, slyšitelné audio výpadky a chování SLS.

## Matice

| Test | Postup | Očekávané kritérium |
|---|---|---|
| Cold boot | 3× zapnout DAB s uloženou stanicí, pak 3× FM | Audio začne bez přeladění tam/zpět; žádná nová CTS recovery |
| Bez SPI | Naladit mux bez potvrzené SPI UA, BAND short/long | Šedé EPG, unavailable; žádné falešné pořady ani další data START |
| SPI bez PI | Mux s potvrzenou UA; logovat nejméně dva karusely | Žlutá i při samotné UA, ne zelená po directory/fragmentech |
| Packet SPI | Volný data slot, potvrzený packet-MOT endpoint | Jediný správný START SID/CID; directory→SI/PI→správný bearer, skutečný NOW/NEXT |
| PAD SPI | Potvrzená UA audio komponenty a UATYPE=7 | SI/PI přichází z PAD; UATYPE SLS/DLS neplní EPG; žádné míchání zdrojů |
| Obsazený SLS slot | Samostatná SLS data služba + packet EPG | Audio a SLS pokračují; EPG je případně pouze žluté, žádný SLS STOP kvůli EPG |
| Zelená | Přijmout validní PI pro právě poslouchaný SID/EID/ECC a trusted time | Zelená až po dekódování; titul a časy souhlasí s vysíláním |
| Cizí stanice | Přepnout audio na jiný SID/ensemble | Starý detail se zavře a zelená se invaliduje; starý titul se nepřenese |
| Cache/update | Zůstat naladěn přes aktualizaci PI, potom skutečně přerušit příjem | Během aktualizace použitelné cache zelená; jediný fragment neobnoví stale PI |
| Čas | UTC/local pár, poté ztráta RF/časového zdroje >5 min | Offset odpovídá vysílání, bez zdroje nevznikne NOW; list označuje UTC |
| SLS souběh | Manual/Auto/Off, JPEG baseline/progressive a PNG/Adam7 | EPG nepřepíše MOT arénu, Auto nepřekryje EPG, návrat na SLS funguje |
| OOM | EPG OFF vs ON se skutečně alokovaným 76800B dekodérem; zachytit before/after | Brána respektuje free/largest, OOM se bezpečně degraduje, nikdy zelená bez PI |
| BAND | Short release, 799/800 ms, bounce, držení při bootu | Long bez předchozího short, žádné přepnutí FM/DAB |
| Menu | CH± na Radio mode, VOL± kandidát, long SELECT cancel, short commit | Pouhé procházení nic neladí/neukládá; jeden potvrzený boot, původních 10 voleb funkčních |
| Busy/failure | Potvrdit při busy; pokud přirozeně selže boot, uložit log | Background busy ukáže čekání; po idle nejvýše jeden boot. Cancel/3s timeout bez pozdního bootu; chyba ponechá původní uložený režim |
| Modály | EPG Now→list→detail, menu a zpět, CH autorepeat, volume | CH v EPG neladí, volume funguje, menu vrátí platný pohled, pruhy bez full bliknutí |
| FM info | Short BAND RDS, long BAND tech, bez RDS i bez RF | Živá PS/RT/PTY/PI jen s platným RDS; EEPROM název se za live PS nevydává |

## Zátěž 11A / 11B

Na každém dostupném muxu nejméně 10 minut se stejnou anténou a srovnatelným RF.
Každých 20–30 s otevřít/zavřít EPG, list/detail, menu a SLS; měnit hlasitost.
Zahrnout dlouhý progressive JPEG/Adam7 decode a potvrzení nové stanice. Pro každý
mux provést nejméně 3× scan, tune a FM↔DAB přes menu. Neprovádět testy najednou
tak, aby se nedalo rozlišit, který úkon vyvolal chybu.

Pokud 11A/11B nejsou dostupné, zaznamenat nedostupnost; jiný mux je doplňkový
test a nenahrazuje ověření dříve problémového multiplexu. Porovnat EPG OFF se
zapnutým SLS, potom EPG ON při stejném obsahu a pořadí úkonů.

Kritéria pro přijetí:

1. Žádný audio výpadek nebo opakovaný CTS/reset způsobený background EPG.
2. Žádný nekonečný START/STOP/UA cyklus a žádný rostoucí serial backlog.
3. UA inventory se při stabilizaci dokončí i při průběžném DSRV; audio a DSRV
   mají přednost, burst zůstává 4.
4. Bez systematického přírůstku overflow proti srovnatelnému OFF běhu. Pokud
   přírůstky vznikají při image decode i s EPG OFF, oddělit tento existující limit
   od nové EPG cesty a doložit timestampy.
5. Heap min/largest a stack HWM zůstávají stabilní po opakovaném přepínání,
   bez postupné ztráty paměti; OOM nemění audio/SLS na radio failure.
6. Fyzicky pozorovaný přechod šedá→žlutá→zelená musí být podložen UA, directory,
   SI/PI, přesným selected bearer a důvěryhodným časem, ne pouze barvou LCD.

## Postup při chybě

Zachovat celý log včetně bootu a nejméně minuty před/po problému. Poznamenat
poslední tlačítko, aktuální obrazovku a zda audio skutečně vypadlo. Při žluté
ověřit zvlášť potvrzenou UA, volný data slot/owner, directory, SI, PI, bearer,
clock a OOM. Při zelené s nesprávným pořadem porovnat SID/EID/ECC/SCIdS a UTC.

Při CTS/overflow porovnat OFF se stejným SLS obrázkem; nepřidávat takt sběrnice,
neprodlužovat naslepo timeouty a neaktivovat druhou data službu. Při selhání STOP
musí následovat konečná karanténa; cizí pakety nesmějí projít do SLS/EPG.
Výsledek zapsat jako PASS/FAIL/NEOVĚŘENO pro každý řádek a přiložit úplné časované
logy s maxBacklog, CTS, overflow, free/largest/min RAM a OOM.


Navazující oprava: ověřit i prázdné Now/Next oproti prázdnému seznamu. V logu musí
odpovídat `BAND short -> EPG Now/Next` a `BAND long -> EPG list`; po uvolnění long
nesmí přijít short. V menu volit VOL±, potvrdit SELECT během příjmu MOT a zachytit
čekání `...` i jediný následný boot. Vyzkoušet zrušení pending dlouhým SELECT,
odchodem z položky a timeout bez pozdního přepnutí. Jednořádkový text na y=44..56
musí plynule běžet; RSSI a barevné EPG na y=96 se navzájem nesmějí mazat.


### Aktuálně platná mapa po upřesnění uživatele

Předchozí řádky o long BAND / VOL změně hodnot jsou historické; pro nové ověření
platí: krátký SELECT vybírá další položku menu, CH± upravují hodnotu, dlouhý
SELECT potvrzuje odchodem a teprve tehdy zahájí změnu FM/DAB. VOL± zkoušet jako
hlasitost i uvnitř menu. Mezi CH změnou režimu a odchodem nesmí nastat ladění ani
zápis MODE do EEPROM. Vyzkoušet potvrzení při odchodu z jiné položky, čekání na
radio idle a jedno band-ready. Dlouhý BAND při 800 ms, po delším držení i po
uvolnění musí nechat Text/EPG/FM RDS pohled beze změny; krátký BAND nadále
otevře/zavře DAB Now/Next nebo FM RDS. EPG seznam/detail otevírat SELECT.


### Poslední oprava menu — závazná mapa pro HW test

CH± posouvají výběr položek, VOL± mění hodnoty. Na každé položce ověřit krátký
SELECT: nesmí změnit hodnotu ani výběr a nesmí potvrdit přepnutí FM/DAB.
Dlouhý SELECT uloží změny a zavře menu; změněné FM/DAB se spustí až poté.
Mimo menu zachovat krátký SELECT a běžnou hlasitost VOL±. Rezervace dlouhého
BAND zůstává platná. Tato mapa nahrazuje předchozí odstavec o SELECT navigaci.
