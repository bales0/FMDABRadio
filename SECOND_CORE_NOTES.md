# Radio-core boundary

Keep these files/app areas from FMDABRadio:
- UI (`RadioDAB.ino`, `Ui.ino`, `Display.ino`)
- controls (`Controls.*`)
- external EEPROM (`eeprom.ino`, `SparkFun_External_EEPROM.*`)
- backlight
- ST7735 slideshow renderer
- headphone amplifier gain/volume behaviour

Treat these as the authoritative radio layer:
- `si4684.cpp/.h`
- `dab_scheduler_policy.h`
- `FmRegion.h`
- `vendor/si468x/Si468x.h`

Do not reintroduce another DAB/FM service-list, DSRV, DLS, MOT, RDS or periodic
status parser in the UI layer.
