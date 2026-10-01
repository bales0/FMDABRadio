# FMDABRadio – consolidated Si4684 radio core

This tree removes the historical `DABShield`/`Si4684Radio` naming and keeps one
radio implementation in `src/si4684.cpp` + `src/si4684.h`.

## Source of radio behaviour

The DAB/FM scheduler, Si468x command-engine contract, anti-burst policy, service
lifecycle, DSRV/DLS/MOT handling, RDS handling and recovery model follow the
current `bales0/SI4684-FMDAB-Receiver` implementation.

The follow-up feature pass was compared with reference commit
`c4c2a7d08f51a86003f33121e548a02d0cd333fd` (2026-10-01). Platform-neutral
AF, RDS clock, PTY, DAB service-switch and service-context policies were adapted
to this board rather than copying the reference application's hardware/UI layer.

The following files are byte-identical to the current reference project at the
point this package was prepared:

- `src/dab_scheduler_policy.h` – Git blob `7e12bdc75be77680cfa165fb5a776707b80038a1`
- `src/vendor/si468x/Si468x.h` – Git blob `c96f94467e04ecba3f66557b8bbae4c0d34ea1f0`
- `src/FmRegion.h` – Git blob `da5020fd6093dbb66e792c5f1066eb36740e03d8`

`src/si4684.cpp/.h` is the FMDABRadio board adapter variant. It intentionally
keeps the reference scheduler/runtime model while changing the hardware-facing
parts required by this PCB.

## Intentional FMDABRadio differences

- Si4684 and ST7735 share SPI on SCK18/MISO19/MOSI23.
- Si4684 CS is GPIO13.
- INTB is permanently connected to GPIO26; FALLING ISR only records the edge.
- Si4684 reset is GPIO14 and PWREN is GPIO2.
- DAB/FM application images are loaded from the Si4684 external NVSPI flash,
  not embedded in ESP32 flash:
  - DAB `0x086000`
  - FM `0x106000`
- The existing AT24C256 layout, 7-button UI, ST7735 UI, backlight and headphone
  amplifier/gain switching are retained.
- Serial remains a diagnostic monitor; the reference serial-control protocol
  is represented by a smaller non-blocking command set documented in
  `SERIAL_PROTOCOL.md`; it does not emulate the reference GUI monitor byte for
  byte.

## Adapted reference functions

- Regional manual FM tuning/seek and manual DAB multiplex selection use a
  dedicated screen driven by the seven existing buttons.
- RDS group 0A AF decoding and bounded automatic AF probing verify PI and
  RSSI/SNR improvement before accepting a candidate.
- RDS group 4A clock samples require chronological confirmation; the broadcast
  half-hour offset is applied with date rollover.
- RDS/RBDS PTY names, signal-unit selection, compact themes and English/Czech
  settings labels fit the 160x128 UI.
- FM scan PI/RSSI/SNR metadata lives in a versioned EEPROM extension. Legacy
  station tables retain their original byte layout.
- DAB data association prefers the active audio SID, accepts an ensemble-wide
  fallback only when unambiguous, waits after audio start and filters MOT by
  the confirmed audio/data service context.

## Removed parallel radio implementations

The project does not contain:

- `DABShield.cpp/.h`
- `Si4684Radio.cpp/.h`
- `RadioSchedulerPolicy.h`

There is one radio state machine and one Si468x transport path.

## Display

`RadioDAB.ino` contains one `tft.initR(INITR_BLACKTAB)` call at startup only.
Radio boot/recovery does not reinitialise the TFT.

## DAB labels / text

DAB scan stores the 16-byte raw service label plus its charset. UCS-2BE labels
are not trimmed as C strings before EEPROM storage. `TextCodec.cpp` uses the
same EBU Latin mapping used by the reference radio core and supports EBU Latin,
UCS-2BE and UTF-8.

## MOT slideshow

The current radio core uses a fixed 50 KiB MOT arena with packed segment
storage and accepts segments up to 2048 bytes. The ST7735 JPEG/PNG renderer and
scaling remain FMDABRadio-specific.

## Build verification

A PlatformIO executable is not installed in the artifact environment, so this
package has been checked structurally but has not been compiled here. Run the
normal `FMDABRadio` PlatformIO environment before flashing hardware.
