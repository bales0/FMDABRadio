# Desktop regression tests

The complete SLS P0 suite (including the original five tests) runs with:

```powershell
pio run
python tests/run_sls_tests.py
```

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
