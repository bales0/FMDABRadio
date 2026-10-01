# FMDABRadio serial control

The optional line-oriented monitor runs at 115200 baud. Enable it in Settings
(`Serial control`). Commands are ASCII, case-insensitive and terminated by LF.
The parser is non-blocking. Replies beginning with `!` are command results;
lines beginning with `$` contain data.

| Command | Meaning |
|---|---|
| `HELP` | Print the compact command list. |
| `STATUS` | Current band, tuner state, frequency/service and signal data. |
| `LIST` | Current DAB service list or stored station list. FM entries include stored PI/RSSI/SNR metadata. |
| `MODE=FM` / `MODE=DAB` | Start a cooperative band transition. |
| `TUNE=n` | FM frequency in 10 kHz units (for example `10170`) or DAB index `0..37`. |
| `SEEK=UP` / `SEEK=DOWN` | Regional FM seek with wrap. |
| `SERVICE=n` | Start zero-based audio service from the current DAB multiplex list. |
| `VOLUME=0..75` | Set and persist volume. |
| `AF=0` / `AF=1` | Disable/enable automatic FM alternative-frequency probing. |
| `VIEW=TEXT`, `VIEW=SLS` | Select the main or DAB slideshow view. |
| `DEBUG` | Print tuner counters and current free heap. |

Commands that require an idle tuner return `!ERR ...` while another cooperative
operation is active. Diagnostic log output remains available on the same port.
