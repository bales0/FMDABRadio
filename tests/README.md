# Desktop regression tests

Run from the repository root with a C++11 compiler:

```powershell
g++ -std=c++11 -Wall -Wextra -pedantic tests/test_radio_scheduler.cpp -o tests/test_radio_scheduler.exe
./tests/test_radio_scheduler.exe
g++ -std=c++11 -Wall -Wextra -pedantic tests/test_fm_features.cpp -o tests/test_fm_features.exe
./tests/test_fm_features.exe
g++ -std=c++11 -Wall -Wextra -pedantic tests/test_dab_service_switch.cpp -o tests/test_dab_service_switch.exe
./tests/test_dab_service_switch.exe
g++ -std=c++11 -Wall -Wextra -pedantic tests/test_jpeg.cpp src/JPEGdecoder.cpp -o tests/test_jpeg.exe
./tests/test_jpeg.exe
```

`test_fm_features` covers regional stepping, AF candidate validation, AF list
deduplication, RDS clock decoding/confirmation and local-date rollover.
`test_dab_service_switch` covers request supersession, wrap-safe settle delays,
bounded retry state and audio-PAD SLS context rules.

The JPEG fixtures are deterministic assets copied with the progressive decoder
from `SI4684-FMDAB-Receiver` revision `5b88340d3ef32208cc7bff7d05de6a89a5e844dc`.
