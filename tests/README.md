# Desktop regression tests

The complete SLS P0 suite (including the original five tests) runs with:

```powershell
pio run
python tests/run_sls_tests.py
```

The suite also compiles the real `src/si4684.cpp` against `radio_shims` and feeds
binary DSRV frames with a deterministic clock. It verifies a missing interior
MOT segment after 45/90/180 seconds, repeated carousels, millis wrap, context
invalidation, published ownership, bounds, SPI CS ordering and framing. Bus
configuration tests reject overclock overrides and check the build-local
Adafruit initR default-frequency adaptation. These are not electrical tests.

Optional verification of the original 30-second loss on the actual old driver:

```powershell
python tests/run_sls_tests.py --mot-baseline 28097fdd1ed8dbc6414f21884738896c6db230d5
```

The baseline driver is read with `git show` into TEMP; the working tree is not
checked out or modified. The expected specific failed assertion confirms that
the old driver discards the 608-byte partial object after the first 45s pause.
The radio-driver integration build demotes only the pre-existing `Aborted`
enum switch warning from an error; all other host warnings stay errors.

LCD activity regression also checks 29999/30000ms, timestamp zero and millis
wrap. Invalid/foreign packets cannot refresh reception; valid headers and
duplicates can. Expiry preserves the 608-byte partial object and its 95% stored
progress, while completed cached images stay available independently of time.

The runner compiles the exact PNGdec 1.1.6 inflater from PlatformIO libdeps.
Generated independent Adam7 fixtures and all executables are temporary and do
not modify tracked assets. It covers 92 valid and 197 hostile Adam7 inputs,
160x128 stripe scaling, workspace canaries/alignment, nine supported standard
PNG conversions and the real Slideshow.cpp with host-only TFT/heap doubles.
The render integration tests 100 reject/reopen cycles, shared format overlay,
validation before any TFT write and render-failure signaling. Host heap doubles
are not measurements of ESP32 memory or SPI/CTS timing.

Optional `python tests/run_sls_tests.py --sanitize` requires a host compiler
with working ASan/UBSan runtimes. The current Windows GCC installation lacks
libasan/libubsan, so this configuration was not executed successfully here.
Known pre-existing PNGdec RGB565 limitations (grayscale 2/4-bit and packed
palette alpha) are documented in PORT_MATRIX.md, not claimed as passing modes.

Run from the repository root with a C++11 compiler:

```powershell
g++ -std=c++11 -Wall -Wextra -pedantic tests/test_radio_scheduler.cpp -o tests/test_radio_scheduler.exe
./tests/test_radio_scheduler.exe
g++ -std=c++11 -Wall -Wextra -pedantic tests/test_fm_features.cpp -o tests/test_fm_features.exe
./tests/test_fm_features.exe
g++ -std=c++11 -Wall -Wextra -pedantic tests/test_dab_service_switch.cpp -o tests/test_dab_service_switch.exe
./tests/test_dab_service_switch.exe
g++ -std=c++11 -Wall -Wextra -pedantic tests/test_mot_assembly.cpp -o tests/test_mot_assembly.exe
./tests/test_mot_assembly.exe
g++ -std=c++11 -Wall -Wextra -pedantic tests/test_jpeg.cpp src/JPEGdecoder.cpp -o tests/test_jpeg.exe
./tests/test_jpeg.exe
```

`test_fm_features` covers regional stepping, AF candidate validation, AF list
deduplication, RDS clock decoding/confirmation and local-date rollover.
`test_dab_service_switch` covers same/cross-mux STOP dispatch, STOP OK/already
inactive/error classification, data-to-audio STOP progression, retry 1..3 and
exhaustion, request supersession during STOP/settle, wrap-safe settle delays,
request-id rollover and audio-PAD SLS context rules. These are host policy tests,
not a hardware simulation of the complete radio driver.
`test_mot_assembly` covers segment bounds, duplicate/repeated-zero completion,
LAST handling, object changes and the packed 50 KiB arena boundary.
`test_radio_scheduler` also locks down CTS host-starvation classification and
the delayed/bounded DAB audio-info retry schedule.

The JPEG fixtures are deterministic assets copied with the progressive decoder
from `SI4684-FMDAB-Receiver` revision `5b88340d3ef32208cc7bff7d05de6a89a5e844dc`.

## EPG L0 and LCD field repaint (2026-11-02)

```powershell
python tests/run_epg_tests.py
python tests/run_ui_tests.py
```

Both runners compile into TEMP with C++11 warnings as errors and use no COM,
network, EEPROM or hardware. EPG tests enable the headless module only: replay,
cache pressure, multi-TID, hostile input, OOM, allocation counts, bearer identity,
clock confidence/offset and wrap. Fixture provenance is in fixtures/epg/README.md.
The full large RAW corpus/oracle is not part of this run.

LCD tests extract the actual production render functions and observe TFT writes:
100 unchanged redraw requests write nothing; individual changes touch their own
fields, pixel scrolling only the DLS strip, and invalidation reconstructs the view.
This is not a physical display flicker or timing measurement.

`measure_epg_target.cpp` is a compile-only Xtensa layout probe, never firmware.
See IMPLEMENTATION_REPORT_EPG_L0_LCD.md for target RAM and integration limits.

The EPG runner now also tests L1 discovery against the actual si4684.cpp with
host SPI: all-component inventory, UA padding/length, exact command CID,
stale replies, disabled/ON/OFF builds, priorities and timeout without recovery.
No real tuner/COM is accessed. The existing Aborted enum switch warning is
demoted only for this driver harness, as in the MOT integration harness.
LCD tests additionally execute the production mono-to-RGB transfer and check
all 2080 pixels, row stride, one window, no pre-clear and allocation fallback.
Current stage: IMPLEMENTATION_REPORT_EPG_L1_TEXT.md.

## Audio activation and MOT A/B replay

```powershell
python tests/run_audio_tests.py
python tests/run_mot_benchmark.py
```

Audio tests compile actual si4684.cpp with scripted SPI/time, plus extracted
production app boot/deferred/result functions. They cover cold boot/image/XPAD,
RF rechecks, 8+4 bounded STARTs, fresh-CID correction, one metadata recovery,
cancel/generations, STOP paths and PAD-MOT after accepted/verified audio.
The benchmark builds matching historical headers/drivers and current discovery
ON/OFF in TEMP. It reports synthetic clock times and exact byte/hash checks,
LAST-first/reverse/duplicates/corruption, multiple TIDs, 45/90/180s carousel gaps,
IRQ/polling and a 421ms no-task model. It does not measure real RF/SPI speed,
JPEG decoding time, hardware FIFO capacity or audible output. No COM/flash.
See IMPLEMENTATION_REPORT_AUDIO_RECOVERY_MOT.md for budgets, results and HW plan.

## Continuous SLS carousel

Current lifecycle: SLS_MOT_IMPLEMENTATION_REPORT_CZ.md. The radio harness now
tests an explicit render lease, any-first-segment TID transitions, exact cached
replay/probe/compaction for recycled TIDs, late old identities, and IRQ/STATUS
arriving during payload read. Notification/ACK does not pin the arena.
The UI runner also compiles actual slideshow UI functions with the real driver
for Manual/Auto/Off, modal/scan, partial reopen, same-content waiting, failures
and immutable decoder input. Diagnostics are enabled only for the MOT host
harness; the default firmware flag is 0 and emits no new periodic output.
Historical replay now copies matching policy headers as well as driver/header.

## Live EPG L2/L3 and BAND/menu

`run_epg_tests.py` also links the actual driver, live runtime and Core. It feeds
DSRV packet and UA-confirmed PAD SI/PI groups, checks station identity/Now/Next,
unchanged partial SLS, full-object freshness, CRC/stale/foreign rejection,
packet START, bounded STOP quarantine and paired UTC/local time. Five runtime
allocation failures leave no Core and preserve SLS; default Core logo/OOM tests
remain. Live ON + discovery OFF is compiled and checked independently.

`run_ui_tests.py` extracts production EpgUi/menu/navigation functions and tests
160x128 drawing, bounded strip painting, cached EPG badge, UTF-8, modal return,
candidate/commit/cancel/busy/rollback and CH/volume precedence. It also compiles
the real Controls state machine for BAND release/800ms/bounce/boot-held/wrap,
SELECT800 and SCAN1000. No production demo data, COM or automatic flash.
See IMPLEMENTATION_REPORT_BAND_EPG_LIVE_CZ.md and TEST_PLAN_EPG_LIVE_HW_CZ.md;
host SPI/TFT tests do not establish physical RF, heap or CTS/FIFO performance.
