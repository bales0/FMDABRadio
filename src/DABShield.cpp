/*
 * ESP32/Si4684 adapter implementation.
 *
 * Invariant: the GPIO26 ISR only records an edge. All SPI transfers, command
 * state transitions, parsing and diagnostic output run from DAB::task().
 */
#include "DABShield.h"

#include <SPI.h>
#include <esp_heap_caps.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "Si468xROM.h"

const uint32_t dab_freq[] = {
    174928, 176640, 178352, 180064, 181936, 183648, 185360, 187072,
    188928, 190640, 192352, 194064, 195936, 197648, 199360, 201072,
    202928, 204640, 206352, 208064, 209936, 211648, 213360, 215072,
    216928, 218640, 220352, 222064, 223936, 225648, 227360, 229072,
    230784, 232496, 234208, 235776, 237488, 239200};

namespace {

struct PropertySetting {
  uint16_t property;
  uint16_t value;
};

constexpr uint32_t RADIO_SPI_HZ = 2000000UL;
constexpr uint32_t DAB_IMAGE_FLASH_ADDRESS = 0x086000UL;
constexpr uint32_t FM_IMAGE_FLASH_ADDRESS = 0x106000UL;
constexpr uint32_t RADIO_COMMAND_TIMEOUT_US = 1000000UL;
constexpr uint32_t RADIO_TUNE_TIMEOUT_MS = 5000UL;
constexpr uint16_t PATCH_CHUNK_SIZE = 480;
constexpr uint32_t SLS_COLLECTION_TIMEOUT_MS = 30000UL;
constexpr uint16_t DAB_XPAD_DLS = 1U << 0;
// The supplied dabreceiver enables DLS, MOT and the associated XPAD
// applications with this value. Keep DLS-only mode minimal when SLS is off.
constexpr uint16_t DAB_XPAD_RECEIVER_COMPAT = 0x0097;

uint16_t dabXpadValue(bool slideshowEnabled) {
  return slideshowEnabled ? DAB_XPAD_RECEIVER_COMPAT : DAB_XPAD_DLS;
}

volatile bool si4684IrqEdge = false;

void IRAM_ATTR onSi4684Intb() {
  si4684IrqEdge = true;
}

bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

const PropertySetting DAB_PROPERTIES[] = {
    {0x0800, 0x8001},  // PIN_CONFIG_ENABLE: INTBOUTEN + analog DAC.
    {0x0000, 0x2031},  // INT_CTL_ENABLE: DEVNT, DACQ, DSRV and STC.
    {0x0001, 0x0010},  // INT_CTL_REPEAT: repeat DSRV until acknowledged.
    {0x1710, 0xF83E},  // DAB_TUNE_FE_VARM.
    {0x1711, 0x01A4},  // DAB_TUNE_FE_VARB.
    {0x1712, 0x0001},  // DAB_TUNE_FE_CFG.
    {0x8100, 0x0003},  // DIGITAL_SERVICE_INT_SOURCE: packet + overflow.
    {0xB300, 0x0081},  // DAB_EVENT_INTERRUPT_SOURCE: list + reconfigure.
    {0xB400, DAB_XPAD_DLS},  // DAB_XPAD_ENABLE: value is selected at boot.
};

const PropertySetting FM_PROPERTIES[] = {
    {0x0800, 0x8001},  // PIN_CONFIG_ENABLE: INTBOUTEN + analog DAC.
    {0x0000, 0x000D},  // INT_CTL_ENABLE: RSQ, RDS and STC.
    {0x3100, 8750},    // FM_SEEK_BAND_BOTTOM: replaced from FM region.
    {0x3101, 10800},   // FM_SEEK_BAND_TOP: replaced from FM region.
    {0x3102, 10},      // FM_SEEK_FREQUENCY_SPACING: replaced from FM region.
    {0x3204, 10},      // FM_VALID_SNR_THRESHOLD.
    {0x3202, 17},      // FM_VALID_RSSI_THRESHOLD.
    {0x3200, 114},     // FM_VALID_MAX_TUNE_ERROR.
    {0x1710, 0xF83E},  // FM_TUNE_FE_VARM.
    {0x1711, 0x01A4},  // FM_TUNE_FE_VARB.
    {0x1712, 0x0001},  // FM_TUNE_FE_CFG.
    {0x3900, 0x0001},  // FM_AUDIO_DE_EMPHASIS: replaced from FM region.
    {0x3C00, 0x0001},  // FM_RDS_INTERRUPT_SOURCE: FIFO receive.
    {0x3C01, 0x0004},  // FM_RDS_INTERRUPT_FIFO_COUNT: low-latency batches.
    {0x3C02, 0x0001},  // FM_RDS_CONFIG: enable RDS.
};

constexpr uint8_t RDS_GROUP_0A = 0;
constexpr uint8_t RDS_GROUP_0B = 1;
constexpr uint8_t RDS_GROUP_1A = 2;
constexpr uint8_t RDS_GROUP_2A = 4;
constexpr uint8_t RDS_GROUP_2B = 5;
constexpr uint8_t RDS_GROUP_4A = 8;

}  // namespace

DAB::DAB()
    : ECC(0),
      EnsembleID(0),
      ServiceDataLength(0),
      ServiceDataCharset(0),
      error(0),
      freq_index(0xFF),
      numberofservices(0),
      ChipRevision(0),
      RomID(0),
      PartNo(0),
      VerMajor(0),
      VerMinor(0),
      VerBuild(0),
      freq(0),
      signalstrength(0),
      snr(0),
      quality(0),
      valid(false),
      bitrate(0),
      samplerate(0),
      type(SERVICE_NONE),
      mode(DUAL),
      dabplus(false),
      fmPilot(false),
      fmStereoBlend(0),
      pty(0),
      pi(0),
      rdsSync(false),
      tp(false),
      ta(false),
      Year(0),
      Months(0),
      Days(0),
      Hours(0),
      Minutes(0),
      _radio(_host),
      _diagnostics(nullptr),
      _callback(nullptr),
      _chipSelectPin(13),
      _interruptPin(26),
      _resetPin(14),
      _powerEnablePin(2),
      _gain0Pin(27),
      _gain1Pin(33),
      _band(0),
      _state(State::Off),
      _stateAfterTune(State::Ready),
      _operation(RadioOperation::None),
      _completedOperation(RadioOperation::None),
      _completedSuccess(false),
      _operationResultPending(false),
      _bandReadyPending(false),
      _commandPending(false),
      _stcPending(false),
      _rdsPending(false),
      _dsrvPending(false),
      _deviceEventPending(false),
      _volumePending(false),
      _slideshowPropertyPending(false),
      _gainAfterVolume(false),
      _desiredUserVolume(0),
      _volumeCommandUser(0),
      _volumeCommandSi(0),
      _currentSiVolume(0xFF),
      _volumeCommandGainDb(-6),
      _currentGainDb(-6),
      _propertyIndex(0),
      _stateDeadlineMs(0),
      _operationDeadlineMs(0),
      _patchOffset(0),
      _fmTuneTarget(0),
      _fmBandBottom(8750),
      _fmBandTop(10800),
      _fmSeekSpacing(10),
      _fmDeEmphasis(1),
      _dabTuneTarget(0),
      _serviceId(0),
      _componentId(0),
      _serviceStartRetries(0),
      _rdsPsSeenMask(0),
      _rdsPsStableMask(0),
      _lastTextAbState(0xFF),
      _dlsReceivedMask(0),
      _dlsLastSegment(0xFF),
      _dlsToggle(0xFF),
      _dlsCharset(0),
      _currentServiceIndex(0),
      _currentServiceStored(false),
      _slideshowTransportId(0),
      _slideshowHighestSegment(0),
      _slideshowTotalSegments(0),
      _slideshowExpectedLength(0),
      _slideshowReceivedBytes(0),
      _slideshowImageLength(0),
      _slideshowLastActivityMs(0),
      _slideshowServiceId(0),
      _slideshowComponentId(0),
      _slideshowImageHash(0),
      _slideshowEnabled(false),
      _slideshowCollecting(false),
      _slideshowAvailable(false),
      _slideshowUpdate(false),
      _irqCounter(0),
      _commandErrors(0),
      _dsrvOverflows(0),
      _dsrvPackets(0),
      _dlsPackets(0),
      _motPackets(0),
      _dsrvDiagnosticDivider(0),
      _lastAudioStatusMs(0),
      _lastMetadataStatusMs(0),
      _lastStatusDiagnosticMs(0) {
  memset(Ensemble, 0, sizeof(Ensemble));
  memset(ServiceData, 0, sizeof(ServiceData));
  ServiceDataLength = 0;
  memset(service, 0, sizeof(service));
  memset(ps, 0, sizeof(ps));
  memset(_workspace, 0, sizeof(_workspace));
  memset(_rdsText, 0, sizeof(_rdsText));
  memset(_rdsProgramService, 0, sizeof(_rdsProgramService));
  memset(_dlsSegments, 0, sizeof(_dlsSegments));
  memset(_dlsSegmentLengths, 0, sizeof(_dlsSegmentLengths));
  memset(_slideshowSegmentLengths, 0, sizeof(_slideshowSegmentLengths));
  memset(_slideshowSegmentBitmap, 0, sizeof(_slideshowSegmentBitmap));
  _rdsPsSeenMask = 0;
  _rdsPsStableMask = 0;

  _host.context = this;
  _host.writeCommand = writeCommand;
  _host.readReply = readReply;
  _host.timeUs = timeUs;
  _host.idle = idle;
  _host.setReset = setReset;
  _host.setPower = setPower;
  _radio.setHost(_host);
  _radio.setWorkspace(_workspace, sizeof(_workspace));
  _radio.setStatusCallback(statusCallback, this);
  _radio.setCtsPollIntervalUs(1000);
  _radio.setIdleStatusPollIntervalUs(50000);
}

void DAB::configurePins(uint8_t chipSelect, uint8_t interrupt, uint8_t reset,
                        uint8_t powerEnable) {
  _chipSelectPin = chipSelect;
  _interruptPin = interrupt;
  _resetPin = reset;
  _powerEnablePin = powerEnable;

  pinMode(_chipSelectPin, OUTPUT);
  digitalWrite(_chipSelectPin, HIGH);
  pinMode(_resetPin, OUTPUT);
  digitalWrite(_resetPin, LOW);
  pinMode(_powerEnablePin, OUTPUT);
  digitalWrite(_powerEnablePin, LOW);
  pinMode(_interruptPin, INPUT_PULLUP);

  detachInterrupt(digitalPinToInterrupt(_interruptPin));
  attachInterrupt(digitalPinToInterrupt(_interruptPin), onSi4684Intb, FALLING);
}

void DAB::configureAudioPins(uint8_t gain0, uint8_t gain1) {
  _gain0Pin = gain0;
  _gain1Pin = gain1;
  pinMode(_gain0Pin, OUTPUT);
  pinMode(_gain1Pin, OUTPUT);
  setTpaGain(-6);
}

void DAB::configureFmBand(uint16_t bottom10kHz, uint16_t top10kHz,
                         uint8_t spacing10kHz, uint8_t deEmphasis) {
  if (bottom10kHz < 7600) bottom10kHz = 7600;
  if (top10kHz > 10800) top10kHz = 10800;
  if (top10kHz < bottom10kHz) top10kHz = bottom10kHz;
  if (spacing10kHz != 5 && spacing10kHz != 10 && spacing10kHz != 20) {
    spacing10kHz = 10;
  }
  if (deEmphasis > 2) deEmphasis = 1;
  _fmBandBottom = bottom10kHz;
  _fmBandTop = top10kHz;
  _fmSeekSpacing = spacing10kHz;
  _fmDeEmphasis = deEmphasis;
}

void DAB::setDiagnostics(Stream* stream) {
  _diagnostics = stream;
}

void DAB::setCallback(void (*serviceDataCallback)(void)) {
  _callback = serviceDataCallback;
}

bool DAB::allocateSlideshowArena() {
  if (_slideshowEnabled) return true;
  resetSlideshowAssembler(true);
  diagnostic("[SLS] fixed RAM arena ready: %lu bytes, free=%u largest=%u",
             static_cast<unsigned long>(DAB_SLS_ARENA_BYTES),
             ESP.getFreeHeap(),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  return true;
}

void DAB::releaseSlideshowArena() {
  resetSlideshowAssembler(true);
  diagnostic("[SLS] disabled; fixed RAM arena retained, free=%u largest=%u",
             ESP.getFreeHeap(),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

bool DAB::setSlideshowEnabled(bool enabled) {
  if (enabled && !allocateSlideshowArena()) {
    _slideshowEnabled = false;
    return false;
  }
  const bool changed = enabled != _slideshowEnabled;
  _slideshowEnabled = enabled;
  if (!enabled) releaseSlideshowArena();
  if (changed && _band == 0 && _state == State::Ready) {
    _slideshowPropertyPending = true;
  }
  return true;
}

bool DAB::slideshowEnabled() const {
  return _slideshowEnabled;
}

bool DAB::slideshowAvailable() const {
  return _slideshowAvailable && _slideshowImageLength > 0;
}

bool DAB::takeSlideshowUpdate() {
  const bool value = _slideshowUpdate;
  _slideshowUpdate = false;
  return value;
}

void DAB::discardSlideshow() {
  _slideshowAvailable = false;
  _slideshowUpdate = false;
  _slideshowImageLength = 0;
}

const uint8_t* DAB::slideshowData() const {
  return slideshowAvailable() ? _slideshowArena : nullptr;
}

uint32_t DAB::slideshowLength() const {
  return slideshowAvailable() ? _slideshowImageLength : 0;
}

bool DAB::urgentDataPending() const {
  return _dsrvPending || _rdsPending || _deviceEventPending ||
         (_interruptPin != 0xFF && digitalRead(_interruptPin) == LOW);
}

bool DAB::beginAsync(uint8_t requestedBand) {
  if (_commandPending || (requestedBand > 1)) {
    return false;
  }

  _band = requestedBand;
  _operation = RadioOperation::BandBoot;
  _operationResultPending = false;
  _bandReadyPending = false;
  _stcPending = false;
  _rdsPending = false;
  _dsrvPending = false;
  _deviceEventPending = false;
  _slideshowPropertyPending = false;
  _currentSiVolume = 0xFF;
  _lastAudioStatusMs = 0;
  _lastMetadataStatusMs = 0;
  _lastStatusDiagnosticMs = 0;
  setTpaGain(-6);
  _patchOffset = 0;
  _propertyIndex = 0;
  error = 0;
  freq = 0;
  signalstrength = 0;
  snr = 0;
  quality = 0;
  valid = false;
  bitrate = 0;
  samplerate = 0;
  type = SERVICE_NONE;
  mode = DUAL;
  dabplus = false;
  pty = 0;
  pi = 0;
  ECC = 0;
  numberofservices = 0;
  memset(ServiceData, 0, sizeof(ServiceData));
  ServiceDataLength = 0;
  ServiceDataCharset = 0;
  memset(ps, 0, sizeof(ps));
  rdsSync = false;
  tp = false;
  ta = false;
  fmPilot = false;
  fmStereoBlend = 0;
  resetDlsAssembler();
  resetSlideshowAssembler(true);

  // RSTB is asserted before power changes, as required by the A10 reset rules.
  digitalWrite(_resetPin, LOW);
  digitalWrite(_powerEnablePin, HIGH);
  _state = State::ResetHold;
  _stateDeadlineMs = millis() + 100;
  diagnostic("[RADIO] boot requested: %s, NVSPI image @ 0x%06lX",
             _band == 0 ? "DAB" : "FM",
             static_cast<unsigned long>(_band == 0 ? DAB_IMAGE_FLASH_ADDRESS
                                                   : FM_IMAGE_FLASH_ADDRESS));
  return true;
}

bool DAB::requestFmTune(uint16_t frequency10kHz) {
  if (!ready() || _band != 1 || _commandPending) {
    return false;
  }
  _operation = RadioOperation::FmTune;
  _fmTuneTarget = frequency10kHz;
  _stcPending = false;
  _operationDeadlineMs = millis() + RADIO_TUNE_TIMEOUT_MS;
  memset(ps, 0, sizeof(ps));
  memset(ServiceData, 0, sizeof(ServiceData));
  ServiceDataLength = 0;
  memset(_rdsText, 0, sizeof(_rdsText));
  memset(_rdsProgramService, 0, sizeof(_rdsProgramService));
  _rdsPsSeenMask = 0;
  _rdsPsStableMask = 0;
  _lastTextAbState = 0xFF;
  rdsSync = false;
  tp = false;
  ta = false;
  fmPilot = false;
  fmStereoBlend = 0;
  diagnostic("[RADIO] FM tune start: %u.%02u MHz", frequency10kHz / 100,
             frequency10kHz % 100);
  return startCore(_radio.startFmTune(frequency10kHz), State::FmTuneCommand);
}

bool DAB::requestFmSeek(bool up, bool wrap) {
  if (!ready() || _band != 1 || _commandPending) {
    return false;
  }
  _operation = RadioOperation::FmSeek;
  _stcPending = false;
  _operationDeadlineMs = millis() + 12000;
  memset(ps, 0, sizeof(ps));
  memset(ServiceData, 0, sizeof(ServiceData));
  ServiceDataLength = 0;
  memset(_rdsText, 0, sizeof(_rdsText));
  memset(_rdsProgramService, 0, sizeof(_rdsProgramService));
  _rdsPsSeenMask = 0;
  _rdsPsStableMask = 0;
  _lastTextAbState = 0xFF;
  rdsSync = false;
  tp = false;
  ta = false;
  fmPilot = false;
  fmStereoBlend = 0;
  diagnostic("[RADIO] FM seek start: direction=%s wrap=%u", up ? "up" : "down",
             wrap ? 1U : 0U);
  return startCore(_radio.startFmSeek(up, wrap), State::FmSeekCommand);
}

bool DAB::requestDabTune(uint8_t frequencyIndex) {
  if (!ready() || _band != 0 || _commandPending || frequencyIndex >= DAB_FREQS) {
    return false;
  }
  _operation = RadioOperation::DabTune;
  _dabTuneTarget = frequencyIndex;
  _stateAfterTune = State::Ready;
  _stcPending = false;
  _operationDeadlineMs = millis() + RADIO_TUNE_TIMEOUT_MS;
  numberofservices = 0;
  valid = false;
  diagnostic("[RADIO] DAB tune start: index=%u frequency=%lu kHz", frequencyIndex,
             static_cast<unsigned long>(freq_khz(frequencyIndex)));
  return startCore(_radio.startDabTune(frequencyIndex), State::DabTuneCommand);
}

bool DAB::requestDabService(uint8_t frequencyIndex, uint32_t serviceId,
                            uint32_t componentId) {
  if (!ready() || _band != 0 || _commandPending || frequencyIndex >= DAB_FREQS) {
    return false;
  }
  _operation = RadioOperation::DabService;
  _dabTuneTarget = frequencyIndex;
  _serviceId = serviceId;
  _componentId = componentId;
  _serviceStartRetries = 0;
  _operationDeadlineMs = millis() + RADIO_TUNE_TIMEOUT_MS;
  _stcPending = false;
  memset(ServiceData, 0, sizeof(ServiceData));
  ServiceDataLength = 0;
  ServiceDataCharset = 0;
  resetDlsAssembler();
  resetSlideshowAssembler(true);
  diagnostic("[RADIO] DAB service request: index=%u SID=0x%08lX CID=0x%08lX",
             frequencyIndex, static_cast<unsigned long>(serviceId),
             static_cast<unsigned long>(componentId));

  if (freq_index == frequencyIndex && valid) {
    startDabServiceCommand();
    return _commandPending;
  }

  _stateAfterTune = State::DabServiceCommand;
  return startCore(_radio.startDabTune(frequencyIndex), State::DabTuneCommand);
}

void DAB::requestVolume(uint8_t volume) {
  if (volume <= 75) {
    _desiredUserVolume = volume;
    _volumePending = true;
  }
}

void DAB::task() {
  if (si4684IrqEdge) {
    noInterrupts();
    si4684IrqEdge = false;
    interrupts();
    ++_irqCounter;
    _radio.notifyInterrupt();
  }

  const si468x::Result serviceResult = _radio.service();
  if (_commandPending && !_radio.busy()) {
    _commandPending = false;
    commandCompleted();
  } else if (!_commandPending && serviceResult == si468x::Result::TransportError) {
    ++_commandErrors;
  }

  advanceState();

  if (_state == State::Ready && !_commandPending) {
    if (_volumePending) {
      serviceVolume();
    } else if (_dsrvPending || _rdsPending || _deviceEventPending) {
      processPendingEvents();
    } else if (_slideshowPropertyPending && _band == 0) {
      _slideshowPropertyPending = false;
      startProperty(0xB400, dabXpadValue(_slideshowEnabled),
                    State::SlideshowProperty);
    } else {
      processPendingEvents();
    }
  }

  if (_slideshowCollecting && _slideshowLastActivityMs != 0 &&
      static_cast<uint32_t>(millis() - _slideshowLastActivityMs) >
          SLS_COLLECTION_TIMEOUT_MS) {
    diagnostic("[SLS][WARN] collection timeout: object=%06lX bytes=%lu",
               static_cast<unsigned long>(_slideshowTransportId),
               static_cast<unsigned long>(_slideshowReceivedBytes));
    resetSlideshowAssembler(true);
  }
}

bool DAB::ready() const {
  return _state == State::Ready;
}

bool DAB::busy() const {
  return _state != State::Ready && _state != State::Off && _state != State::Failed;
}

uint8_t DAB::band() const {
  return _band;
}

bool DAB::takeBandReady() {
  const bool value = _bandReadyPending;
  _bandReadyPending = false;
  return value;
}

bool DAB::takeOperationResult(RadioOperation& operation, bool& success) {
  if (!_operationResultPending) {
    return false;
  }
  operation = _completedOperation;
  success = _completedSuccess;
  _operationResultPending = false;
  return true;
}

const char* DAB::stateName() const {
  switch (_state) {
    case State::Off: return "off";
    case State::ResetHold: return "reset-hold";
    case State::ResetRelease: return "reset-release";
    case State::PowerUp: return "power-up";
    case State::LoadInitPatch: return "load-init-patch";
    case State::LoadPatch: return "load-patch";
    case State::PatchDelay: return "patch-delay";
    case State::LoadInitFlash: return "load-init-flash";
    case State::ConfigureFlash: return "configure-flash";
    case State::FlashLoad: return "flash-load";
    case State::Boot: return "boot";
    case State::ConfigureBand: return "configure-band";
    case State::Identify: return "identify";
    case State::Ready: return "ready";
    case State::FmTuneCommand: return "fm-tune-command";
    case State::FmTuneWaitStc: return "fm-tune-stc";
    case State::FmSeekCommand: return "fm-seek-command";
    case State::FmSeekWaitStc: return "fm-seek-stc";
    case State::DabTuneCommand: return "dab-tune-command";
    case State::DabTuneWaitStc: return "dab-tune-stc";
    case State::DabServiceCommand: return "dab-service";
    case State::DabServiceRetry: return "dab-service-retry";
    case State::VolumeCommand: return "volume";
    case State::SlideshowProperty: return "slideshow-property";
    case State::Failed: return "failed";
  }
  return "unknown";
}

bool DAB::status() {
  if (!ready() || _commandPending) {
    return false;
  }
  if (_band == 0) {
    updateDabStatus(false);
  } else {
    updateFmStatus(false);
  }
  return error == 0;
}

bool DAB::status(uint32_t serviceId, uint32_t componentId) {
  _serviceId = serviceId;
  _componentId = componentId;
  return status();
}

bool DAB::time(DABTime* value) {
  if (!value || !ready() || _band != 0 || _commandPending) {
    return true;
  }
  si468x::DabTimeInfo info;
  const si468x::Result result = _radio.dabGetTime(0, info);
  if (result != si468x::Result::Ok) {
    fail(result, "DAB_GET_TIME");
    return true;
  }
  value->Year = info.year;
  value->Months = info.month;
  value->Days = info.day;
  value->Hours = info.hour;
  value->Minutes = info.minute;
  value->Seconds = info.second;
  return false;
}

void DAB::mono(bool enable) {
  if (ready() && !_commandPending) {
    const si468x::Result result = _radio.setProperty(0x0302, enable ? 1 : 0);
    if (result != si468x::Result::Ok) fail(result, "AUDIO_OUTPUT_CONFIG");
  }
}

void DAB::mute(bool left, bool right) {
  if (ready() && !_commandPending) {
    const si468x::Result result = _radio.setMute(left, right);
    if (result != si468x::Result::Ok) fail(result, "AUDIO_MUTE");
  }
}

uint32_t DAB::freq_khz(uint8_t index) const {
  return index < DAB_FREQS ? dab_freq[index] : 0;
}

uint32_t DAB::irqCount() const { return _irqCounter; }
uint32_t DAB::commandErrorCount() const { return _commandErrors; }
uint32_t DAB::dsrvOverflowCount() const { return _dsrvOverflows; }
uint32_t DAB::dsrvPacketCount() const { return _dsrvPackets; }
uint32_t DAB::dlsPacketCount() const { return _dlsPackets; }
uint32_t DAB::motPacketCount() const { return _motPackets; }

bool DAB::writeCommand(void* context, uint8_t command, const uint8_t* args,
                       uint16_t length) {
  DAB* self = static_cast<DAB*>(context);
  SPI.beginTransaction(SPISettings(RADIO_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(self->_chipSelectPin, LOW);
  SPI.transfer(command);
  for (uint16_t i = 0; i < length; ++i) {
    SPI.transfer(args[i]);
  }
  digitalWrite(self->_chipSelectPin, HIGH);
  SPI.endTransaction();
  return true;
}

bool DAB::readReply(void* context, uint8_t* destination, uint16_t length) {
  if (!destination || length < 4) return false;
  DAB* self = static_cast<DAB*>(context);
  SPI.beginTransaction(SPISettings(RADIO_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(self->_chipSelectPin, LOW);
  SPI.transfer(0x00);  // SPI framing byte; logical STATUS0 follows it.
  for (uint16_t i = 0; i < length; ++i) {
    destination[i] = SPI.transfer(0x00);
  }
  digitalWrite(self->_chipSelectPin, HIGH);
  SPI.endTransaction();
  return true;
}

uint32_t DAB::timeUs(void*) {
  return micros();
}

void DAB::idle(void*) {
  yield();
}

void DAB::setReset(void* context, bool asserted) {
  DAB* self = static_cast<DAB*>(context);
  digitalWrite(self->_resetPin, asserted ? LOW : HIGH);
}

void DAB::setPower(void* context, bool enabled) {
  DAB* self = static_cast<DAB*>(context);
  digitalWrite(self->_powerEnablePin, enabled ? HIGH : LOW);
}

void DAB::statusCallback(void* context, const si468x::Status& status) {
  DAB* self = static_cast<DAB*>(context);
  self->_stcPending |= status.stcInt();
  self->_rdsPending |= status.rdsInt();
  self->_dsrvPending |= status.dsrvInt();
  self->_deviceEventPending |= status.deviceEventInt();
  if (status.commandError() || status.fatal()) {
    ++self->_commandErrors;
  }
}

size_t DAB::patchReader(void*, uint32_t offset, uint8_t* destination,
                        size_t length) {
  const size_t patchSize = sizeof(rom_patch_016);
  if (!destination || offset >= patchSize) return 0;
  if (length > patchSize - offset) length = patchSize - offset;
  for (size_t i = 0; i < length; ++i) {
    destination[i] = pgm_read_byte(rom_patch_016 + offset + i);
  }
  return length;
}

void DAB::serviceListHeader(void* context,
                            const si468x::DabServiceListHeader& header) {
  DAB* self = static_cast<DAB*>(context);
  self->numberofservices =
      header.numberOfServices < DAB_MAX_SERVICES ? header.numberOfServices
                                                 : DAB_MAX_SERVICES;
  self->_currentServiceIndex = 0;
  self->_currentServiceStored = false;
}

void DAB::serviceListService(void* context,
                             const si468x::DabServiceEntry& entry) {
  DAB* self = static_cast<DAB*>(context);
  const uint8_t index = self->_currentServiceIndex;
  if (index < DAB_MAX_SERVICES) {
    DABService& target = self->service[index];
    memset(&target, 0, sizeof(target));
    target.Freq = self->freq_index;
    target.ServiceID = entry.serviceId;
    memcpy(target.Label, entry.label, 16);
    target.Label[16] = 0;
    target.Charset = entry.labelCharset;
    target.Type = SERVICE_NONE;
  }
  self->_currentServiceStored = false;
  ++self->_currentServiceIndex;
}

void DAB::serviceListComponent(void* context,
                               const si468x::DabComponentEntry& entry) {
  DAB* self = static_cast<DAB*>(context);
  if (entry.serviceIndex >= DAB_MAX_SERVICES) return;
  DABService& target = self->service[entry.serviceIndex];
  if (!self->_currentServiceStored || entry.primary) {
    target.CompID = entry.componentId;
    target.Type = entry.transportModeId == 0 ? SERVICE_AUDIO : SERVICE_DATA;
    self->_currentServiceStored = true;
  }
}

bool DAB::startCore(si468x::Result result, State state) {
  if (result != si468x::Result::Pending) {
    fail(result, stateName());
    return false;
  }
  _state = state;
  _commandPending = true;
  return true;
}

bool DAB::startRaw(si468x::Command command, const uint8_t* args, uint16_t length,
                   State state, uint32_t timeoutUsValue) {
  return startCore(_radio.startCommand(command, args, length, nullptr, 0,
                                       timeoutUsValue),
                   state);
}

bool DAB::startProperty(uint16_t property, uint16_t value, State state) {
  _workspace[0] = 0;
  si468x::writeLe16(_workspace + 1, property);
  si468x::writeLe16(_workspace + 3, value);
  return startRaw(si468x::Command::SET_PROPERTY, _workspace, 5, state);
}

void DAB::advanceState() {
  const uint32_t now = millis();
  switch (_state) {
    case State::ResetHold:
      if (deadlineReached(now, _stateDeadlineMs)) {
        digitalWrite(_resetPin, HIGH);
        _state = State::ResetRelease;
        _stateDeadlineMs = now + 100;
      }
      break;

    case State::ResetRelease:
      if (deadlineReached(now, _stateDeadlineMs)) {
        uint8_t args[15] = {0};
        args[1] = 0x17;  // 19.2 MHz crystal, transfer size 7.
        args[2] = 0x48;
        si468x::writeLe32(args + 3, 19200000UL);
        args[7] = 0x1F;
        args[8] = 0x10;
        args[12] = 0x18;
        startRaw(si468x::Command::POWER_UP, args, sizeof(args), State::PowerUp);
      }
      break;

    case State::PatchDelay:
      if (deadlineReached(now, _stateDeadlineMs)) {
        const uint8_t args[1] = {0};
        startRaw(si468x::Command::LOAD_INIT, args, sizeof(args),
                 State::LoadInitFlash);
      }
      break;

    case State::FmTuneWaitStc:
    case State::FmSeekWaitStc:
      if (_stcPending) {
        _stcPending = false;
        updateFmStatus(true);
        if (_operation != RadioOperation::None) {
          finishOperation(error == 0);
        }
      } else if (deadlineReached(now, _operationDeadlineMs)) {
        fail(si468x::Result::Timeout, "FM STC");
      }
      break;

    case State::DabTuneWaitStc:
      if (_stcPending) {
        _stcPending = false;
        updateDabStatus(true);
        if (_operation == RadioOperation::None) {
          break;
        } else if (error != 0) {
          finishOperation(false);
        } else if (_stateAfterTune == State::DabServiceCommand) {
          startDabServiceCommand();
        } else {
          finishOperation(true);
        }
      } else if (deadlineReached(now, _operationDeadlineMs)) {
        fail(si468x::Result::Timeout, "DAB STC");
      }
      break;

    case State::DabServiceRetry:
      if (deadlineReached(now, _operationDeadlineMs)) {
        fail(si468x::Result::Timeout, "START_DIGITAL_SERVICE retries");
      } else if (deadlineReached(now, _stateDeadlineMs)) {
        startDabServiceCommand();
      }
      break;

    default:
      break;
  }
}

void DAB::commandCompleted() {
  const si468x::Result result = _radio.lastResult();
  if (result != si468x::Result::Ok) {
    // Right after DAB STC the service database can need a few more tens of
    // milliseconds. This is a recoverable receiver state, not a UI error.
    if (_state == State::DabServiceCommand &&
        !deadlineReached(millis(), _operationDeadlineMs)) {
      ++_serviceStartRetries;
      if (_serviceStartRetries == 1 || (_serviceStartRetries % 20) == 0) {
        diagnostic("[RADIO][WARN] DAB service not ready (%s), retry=%u",
                   resultName(result), _serviceStartRetries);
      }
      _state = State::DabServiceRetry;
      _stateDeadlineMs = millis() + 25;
      return;
    }
    fail(result, stateName());
    return;
  }

  switch (_state) {
    case State::PowerUp: {
      const uint8_t args[1] = {0};
      startRaw(si468x::Command::LOAD_INIT, args, sizeof(args),
               State::LoadInitPatch);
      break;
    }

    case State::LoadInitPatch:
    case State::LoadPatch: {
      const uint32_t patchSize = sizeof(rom_patch_016);
      if (_patchOffset >= patchSize) {
        _state = State::PatchDelay;
        _stateDeadlineMs = millis() + 4;
        break;
      }
      uint16_t count = static_cast<uint16_t>(patchSize - _patchOffset);
      if (count > PATCH_CHUNK_SIZE) count = PATCH_CHUNK_SIZE;
      _workspace[0] = _workspace[1] = _workspace[2] = 0;
      patchReader(this, _patchOffset, _workspace + 3, count);
      _patchOffset += count;
      startRaw(si468x::Command::HOST_LOAD, _workspace, count + 3,
               State::LoadPatch);
      break;
    }

    case State::LoadInitFlash: {
      // Keep the known-working NVSPI setup and layout already programmed on
      // the receiver board; no large firmware image is linked into the ESP32.
      const uint8_t args[7] = {0x10, 0x00, 0x00, 0x01, 0x00, 0x10, 0x27};
      startRaw(si468x::Command::FLASH_LOAD, args, sizeof(args),
               State::ConfigureFlash);
      break;
    }

    case State::ConfigureFlash: {
      uint8_t args[11] = {0};
      const uint32_t address =
          _band == 0 ? DAB_IMAGE_FLASH_ADDRESS : FM_IMAGE_FLASH_ADDRESS;
      si468x::writeLe32(args + 3, address);
      startRaw(si468x::Command::FLASH_LOAD, args, sizeof(args), State::FlashLoad);
      break;
    }

    case State::FlashLoad: {
      const uint8_t args[1] = {0};
      startRaw(si468x::Command::BOOT, args, sizeof(args), State::Boot);
      break;
    }

    case State::Boot:
      _propertyIndex = 0;
      _state = State::ConfigureBand;
      configureNextProperty();
      break;

    case State::ConfigureBand:
      configureNextProperty();
      break;

    case State::FmTuneCommand:
      _state = State::FmTuneWaitStc;
      break;

    case State::FmSeekCommand:
      _state = State::FmSeekWaitStc;
      break;

    case State::DabTuneCommand:
      _state = State::DabTuneWaitStc;
      break;

    case State::DabServiceCommand:
      freq_index = _dabTuneTarget;
      valid = true;
      finishOperation(true);
      break;

    case State::VolumeCommand:
      _currentSiVolume = _volumeCommandSi;
      if (_gainAfterVolume) {
        setTpaGain(_volumeCommandGainDb);
      }
      error = 0;
      _state = State::Ready;
      diagnostic("[AUDIO] V%u -> %s%d dB, Si=%u, TPA=%+d dB",
                 _volumeCommandUser, _volumeCommandUser == 0 ? "mute " : "",
                 _volumeCommandUser == 0 ? 0 :
                     static_cast<int>(_volumeCommandUser) - 69,
                 _volumeCommandSi, _currentGainDb);
      if (_desiredUserVolume != _volumeCommandUser) {
        _volumePending = true;
      }
      break;

    case State::SlideshowProperty:
      error = 0;
      _state = State::Ready;
      verifySlideshowProperty();
      break;

    default:
      break;
  }
}

void DAB::startDabServiceCommand() {
  uint8_t args[11] = {0};
  si468x::writeLe32(args + 3, _serviceId);
  si468x::writeLe32(args + 7, _componentId);
  startRaw(si468x::Command::START_DIGITAL_SERVICE, args, sizeof(args),
           State::DabServiceCommand);
}

void DAB::serviceVolume() {
  const uint8_t user = _desiredUserVolume;
  uint8_t siVolume = 0;
  int8_t gainDb = -6;

  if (user >= 1 && user <= 6) {
    siVolume = user;
  } else if (user <= 69 && user >= 7) {
    siVolume = user - 6;
    gainDb = 0;
  } else if (user <= 72 && user >= 70) {
    siVolume = user - 9;
    gainDb = 3;
  } else if (user >= 73) {
    siVolume = user - 12;
    gainDb = 6;
  }

  _volumePending = false;
  _volumeCommandUser = user;
  _volumeCommandSi = siVolume;
  _volumeCommandGainDb = gainDb;
  _gainAfterVolume = gainDb > _currentGainDb;

  // Downward gain changes happen before reducing Si4684 attenuation. Upward
  // changes happen only after the compensated Si4684 volume is confirmed.
  if (gainDb < _currentGainDb) {
    setTpaGain(gainDb);
  }
  if (_currentSiVolume == siVolume) {
    if (gainDb != _currentGainDb) setTpaGain(gainDb);
    diagnostic("[AUDIO] V%u -> %s%d dB, Si=%u, TPA=%+d dB",
               user, user == 0 ? "mute " : "",
               user == 0 ? 0 : static_cast<int>(user) - 69,
               siVolume, _currentGainDb);
    return;
  }
  startProperty(0x0300, siVolume, State::VolumeCommand);
}

void DAB::setTpaGain(int8_t gainDb) {
  bool gain0 = false;
  bool gain1 = false;
  switch (gainDb) {
    case 0:
      gain0 = true;
      break;
    case 3:
      gain1 = true;
      break;
    case 6:
      gain0 = true;
      gain1 = true;
      break;
    default:
      gainDb = -6;
      break;
  }
  digitalWrite(_gain0Pin, gain0 ? HIGH : LOW);
  digitalWrite(_gain1Pin, gain1 ? HIGH : LOW);
  _currentGainDb = gainDb;
}

void DAB::finishOperation(bool success) {
  const RadioOperation completed = _operation;
  _completedOperation = completed;
  _completedSuccess = success;
  _operationResultPending = completed != RadioOperation::None;
  _operation = RadioOperation::None;
  // A failed tune/status operation is recoverable. Only a failed image boot
  // leaves the driver unavailable until another band boot is requested.
  _state = (!success && completed == RadioOperation::BandBoot)
               ? State::Failed
               : State::Ready;
  diagnostic("[RADIO] operation %u %s", static_cast<unsigned>(completed),
             success ? "complete" : "failed");
}

void DAB::fail(si468x::Result result, const char* where) {
  error = static_cast<uint8_t>(0x80 | (static_cast<int8_t>(result) < 0
                                          ? -static_cast<int8_t>(result)
                                          : static_cast<int8_t>(result)));
  ++_commandErrors;
  diagnostic("[RADIO][ERROR] %s: %s, device=0x%02X state=%s", where,
             resultName(result), _radio.lastDeviceError(), stateName());
  _commandPending = false;
  finishOperation(false);
}

void DAB::configureNextProperty() {
  if (_band == 0 && _propertyIndex == 0) {
    _workspace[0] = DAB_FREQS;
    _workspace[1] = _workspace[2] = 0;
    for (uint8_t i = 0; i < DAB_FREQS; ++i) {
      si468x::writeLe32(_workspace + 3 + i * 4, dab_freq[i]);
    }
    ++_propertyIndex;
    startRaw(si468x::Command::DAB_SET_FREQ_LIST, _workspace,
             3 + DAB_FREQS * 4, State::ConfigureBand);
    return;
  }

  const ::PropertySetting* settings =
      _band == 0 ? DAB_PROPERTIES : FM_PROPERTIES;
  const uint8_t count =
      _band == 0 ? sizeof(DAB_PROPERTIES) / sizeof(DAB_PROPERTIES[0])
                 : sizeof(FM_PROPERTIES) / sizeof(FM_PROPERTIES[0]);
  const uint8_t settingIndex =
      _band == 0 ? static_cast<uint8_t>(_propertyIndex - 1) : _propertyIndex;

  if (settingIndex < count) {
    ++_propertyIndex;
    const uint16_t property = settings[settingIndex].property;
    uint16_t value = settings[settingIndex].value;
    if (_band == 0 && property == 0xB400) {
      value = dabXpadValue(_slideshowEnabled);
    } else if (_band == 1) {
      switch (property) {
        case 0x3100: value = _fmBandBottom; break;
        case 0x3101: value = _fmBandTop; break;
        case 0x3102: value = _fmSeekSpacing; break;
        case 0x3900: value = _fmDeEmphasis; break;
        default: break;
      }
    }
    startProperty(property, value, State::ConfigureBand);
    return;
  }

  if (_band == 1) {
    diagnostic("[RADIO] FM region config: %u.%02u-%u.%02u MHz step=%u kHz de-emphasis=%u us",
               _fmBandBottom / 100, _fmBandBottom % 100,
               _fmBandTop / 100, _fmBandTop % 100,
               static_cast<unsigned>(_fmSeekSpacing) * 10U,
               _fmDeEmphasis == 0 ? 75U : (_fmDeEmphasis == 1 ? 50U : 0U));
  }
  _state = State::Identify;
  identify();
}

void DAB::identify() {
  si468x::PartInfo part;
  si468x::SystemState system;
  si468x::FunctionInfo function;
  si468x::Result result = _radio.getPartInfo(part);
  if (result == si468x::Result::Ok) result = _radio.getSystemState(system);
  if (result == si468x::Result::Ok) result = _radio.getFunctionInfo(function);
  if (result != si468x::Result::Ok) {
    fail(result, "radio identification");
    return;
  }

  ChipRevision = part.chipRevision;
  RomID = part.romId;
  PartNo = part.partNumber;
  VerMajor = function.major;
  VerMinor = function.minor;
  VerBuild = function.build;
  if (_band == 0) verifySlideshowProperty();
  error = 0;
  _state = State::Ready;
  _bandReadyPending = true;
  _completedOperation = RadioOperation::BandBoot;
  _completedSuccess = true;
  _operationResultPending = true;
  _operation = RadioOperation::None;
  diagnostic("[RADIO] ready: part=Si%u rev=%u ROM=%u image=%u FW=%u.%u.%u IRQ=%lu",
             PartNo, ChipRevision, RomID, static_cast<unsigned>(system.image),
             VerMajor, VerMinor, VerBuild,
             static_cast<unsigned long>(_irqCounter));
}

void DAB::verifySlideshowProperty() {
  const uint16_t expected = dabXpadValue(_slideshowEnabled);
  uint16_t actual = 0;
  const si468x::Result result = _radio.getProperty(0xB400, actual);
  if (result != si468x::Result::Ok) {
    ++_commandErrors;
    diagnostic("[SLS][WARN] DAB_XPAD_ENABLE readback failed: %s",
               resultName(result));
    return;
  }
  diagnostic("[SLS] DAB_XPAD_ENABLE requested=0x%04X readback=0x%04X (%s)",
             expected, actual, _slideshowEnabled ? "DLS+MOT" : "DLS only");
  if (actual != expected) {
    ++_commandErrors;
    diagnostic("[SLS][WARN] DAB_XPAD_ENABLE mismatch");
  }
}

void DAB::processPendingEvents() {
  if (_dsrvPending && _band == 0) {
    processDsrv();
  } else if (_rdsPending && _band == 1) {
    processRds();
  } else if (_deviceEventPending && _band == 0) {
    processDabEvent();
  }
}

void DAB::processRds() {
  _rdsPending = false;
  bool changed = false;
  for (uint8_t count = 0; count < 8; ++count) {
    si468x::FmRdsGroup group;
    const si468x::Result result =
        _radio.fmRdsStatus(group, false, false, true);
    if (result != si468x::Result::Ok) {
      fail(result, "FM_RDS_STATUS");
      return;
    }
    rdsSync = group.sync;
    if (group.piValid) pi = group.pi;
    if (group.tpPtyValid) {
      pty = group.pty;
      tp = group.tp;
    }
    changed |= decodeRdsGroup(group) != 0;
    if (group.fifoUsed <= 1) break;
  }
  if (changed && _callback) _callback();
}

void DAB::resetDlsAssembler() {
  memset(_dlsSegments, 0, sizeof(_dlsSegments));
  memset(_dlsSegmentLengths, 0, sizeof(_dlsSegmentLengths));
  _dlsReceivedMask = 0;
  _dlsLastSegment = 0xFF;
  _dlsToggle = 0xFF;
  _dlsCharset = 0;
}

void DAB::publishDlsSegments(uint8_t firstSegment, uint8_t lastSegment,
                             bool complete) {
  if (firstSegment > lastSegment || lastSegment >= 8) return;
  char assembled[DAB_MAX_SERVICEDATA_LEN] = {0};
  uint16_t outputLength = 0;
  for (uint8_t index = firstSegment; index <= lastSegment; ++index) {
    uint8_t copyLength = _dlsSegmentLengths[index];
    if (copyLength > DAB_MAX_SERVICEDATA_LEN - 1 - outputLength) {
      copyLength = DAB_MAX_SERVICEDATA_LEN - 1 - outputLength;
    }
    memcpy(assembled + outputLength, _dlsSegments[index], copyLength);
    outputLength += copyLength;
  }
  if (outputLength == 0) return;
  if (ServiceDataLength == outputLength &&
      ServiceDataCharset == _dlsCharset &&
      memcmp(ServiceData, assembled, outputLength) == 0) {
    return;
  }
  memset(ServiceData, 0, sizeof(ServiceData));
  memcpy(ServiceData, assembled, outputLength);
  ServiceDataLength = outputLength;
  ServiceDataCharset = _dlsCharset;
  diagnostic("[DLS] %s: range=%u-%u bytes=%u charset=%u",
             complete ? "complete" : "partial", firstSegment, lastSegment,
             outputLength, ServiceDataCharset);
  if (_callback) _callback();
}

void DAB::processDsrv() {
  _dsrvPending = false;
  si468x::DsrvHeader header;
  // Match the supplied receiver: acknowledge one DSRV buffer, inspect its
  // 24-byte header, then re-read the preserved reply at its exact length.
  // This avoids both READ_OFFSET assumptions and over-reading short packets.
  si468x::Result result =
      _radio.getDigitalServiceDataHeader(header, false, true);
  if (result != si468x::Result::Ok) {
    fail(result, "GET_DIGITAL_SERVICE_DATA header");
    return;
  }
  ++_dsrvPackets;
  if (header.overflow() || header.serviceOverflow()) {
    ++_dsrvOverflows;
    diagnostic("[RADIO][WARN] DSRV overflow: remaining=%u total=%lu",
               header.buffersRemaining,
               static_cast<unsigned long>(_dsrvOverflows));
  }

  if (header.physicalError() || header.payloadReceivedWithErrors()) {
    diagnostic("[RADIO][WARN] damaged DSRV packet dropped: source=%u bytes=%u",
               header.dataSource, header.byteCount);
    _dsrvPending = header.buffersRemaining > 0;
    return;
  }

  if (header.byteCount == 0 || header.byteCount > sizeof(_workspace) - 24) {
    if (header.byteCount > sizeof(_workspace) - 24) {
      ++_commandErrors;
      diagnostic("[RADIO][WARN] oversized DSRV packet dropped: %u bytes",
                 header.byteCount);
    }
    _dsrvPending = header.buffersRemaining > 0;
    return;
  }

  const uint16_t replyLength = static_cast<uint16_t>(24 + header.byteCount);
  result = _radio.readCurrentReply(_workspace, replyLength);
  if (result != si468x::Result::Ok) {
    fail(result, "GET_DIGITAL_SERVICE_DATA payload");
    return;
  }
  const si468x::Result parseResult =
      si468x::Si468x::parseDsrvHeader(_workspace, replyLength, header);
  if (parseResult != si468x::Result::Ok) {
    fail(parseResult, "parse DSRV payload header");
    return;
  }

  const uint8_t* payload = _workspace + 24;
  // Recognized DLS and MOT packets have their own detailed logs below. A
  // sampled raw header still makes an unknown/misaligned stream diagnosable
  // without letting Serial output itself cause a DSRV overflow.
  if (header.isDlsSource() || (++_dsrvDiagnosticDivider & 0x0F) == 1) {
    diagnostic("[DSRV] src=%u type=%u bytes=%u remaining=%u SID=%08lX CID=%08lX head=%02X %02X %02X",
               header.dataSource, header.dscType, header.byteCount,
               header.buffersRemaining,
               static_cast<unsigned long>(header.serviceId),
               static_cast<unsigned long>(header.componentId), payload[0],
               header.byteCount > 1 ? payload[1] : 0,
               header.byteCount > 2 ? payload[2] : 0);
  }

  if (header.isDlsSource() && header.byteCount >= 2) {
    ++_dlsPackets;
    const uint8_t prefix = payload[0];
    const uint8_t field2 = payload[1];
    const bool command = (prefix & 0x10) != 0;
    if (command) {
      if ((prefix & 0x0F) == 1) {
        // A Clear command announces a new label cycle. Keep the last good
        // label visible until at least one fragment of its replacement arrives.
        resetDlsAssembler();
        diagnostic("[DLS] clear command; keeping last good text (%u bytes)",
                   ServiceDataLength);
      }
    } else if (header.byteCount > 18) {
      // Some Si468x DAB images forward a complete dynamic label in one DSRV
      // block. In that form bytes 0..1 are an envelope and all remaining
      // bytes are text; the low nibble of byte 0 is not a segment length.
      uint8_t charset = field2 >> 4;
      if (charset == 4) charset = 6;
      const uint8_t* text = payload + 2;
      uint16_t textLength = min<uint16_t>(
          header.byteCount - 2, DAB_MAX_SERVICEDATA_LEN - 1);
      if (charset == 6) {
        // UCS-2BE must remain aligned to complete two-byte code units.
        textLength &= static_cast<uint16_t>(~1U);
        while (textLength >= 2 && text[textLength - 2] == 0 &&
               (text[textLength - 1] == 0 ||
                text[textLength - 1] == ' ' ||
                text[textLength - 1] == '\r' ||
                text[textLength - 1] == '\n')) {
          textLength -= 2;
        }
      } else {
        while (textLength > 0 &&
               (text[textLength - 1] == 0 ||
                text[textLength - 1] == ' ' ||
                text[textLength - 1] == '\r' ||
                text[textLength - 1] == '\n')) {
          --textLength;
        }
      }
      if (textLength > 0 &&
          (ServiceDataLength != textLength ||
           ServiceDataCharset != charset ||
           memcmp(ServiceData, text, textLength) != 0)) {
        memset(ServiceData, 0, sizeof(ServiceData));
        memcpy(ServiceData, text, textLength);
        ServiceDataLength = textLength;
        ServiceDataCharset = charset;
        resetDlsAssembler();
        _dlsToggle = prefix >> 7;
        _dlsCharset = charset;
        diagnostic("[DLS] full block: bytes=%u charset=%u toggle=%u",
                   textLength, charset, _dlsToggle);
        if (_callback) _callback();
      }
    } else {
      const bool first = (prefix & 0x40) != 0;
      const bool last = (prefix & 0x20) != 0;
      const uint8_t toggle = prefix >> 7;
      // In the DLS field byte the high nibble is the character encoding;
      // the segment number is carried by the low three bits.
      const uint8_t segment = first ? 0 : (field2 & 0x07);
      const uint8_t textLength = (prefix & 0x0F) + 1;

      if (header.byteCount < static_cast<uint16_t>(2 + textLength)) {
        ++_commandErrors;
        diagnostic("[DLS][WARN] short segment: bytes=%u expected=%u",
                   header.byteCount, 2U + textLength);
      } else {
        if (_dlsToggle != toggle) {
          memset(_dlsSegments, 0, sizeof(_dlsSegments));
          memset(_dlsSegmentLengths, 0, sizeof(_dlsSegmentLengths));
          _dlsReceivedMask = 0;
          _dlsLastSegment = 0xFF;
          _dlsToggle = toggle;
        }
        if (first) {
          _dlsCharset = field2 >> 4;
          // The supplied dabreceiver treats encoding 4 as the UCS-2 form
          // represented by value 6 in the ETSI conversion tables.
          if (_dlsCharset == 4) _dlsCharset = 6;
        }
        memcpy(_dlsSegments[segment], payload + 2, textLength);
        _dlsSegmentLengths[segment] = textLength;
        _dlsReceivedMask |= static_cast<uint8_t>(1U << segment);
        if (last) _dlsLastSegment = segment;
        diagnostic("[DLS] segment=%u first=%u last=%u toggle=%u bytes=%u charset=%u mask=%02X",
                   segment, first ? 1U : 0U, last ? 1U : 0U, toggle,
                   textLength, _dlsCharset, _dlsReceivedMask);

        bool complete = false;
        if (_dlsLastSegment < 8) {
          const uint8_t requiredMask = static_cast<uint8_t>(
              (1U << (_dlsLastSegment + 1)) - 1U);
          complete = (_dlsReceivedMask & requiredMask) == requiredMask;
        }

        if (complete) {
          publishDlsSegments(0, _dlsLastSegment, true);
        } else if ((_dlsReceivedMask & 0x01) != 0) {
          // Publish the longest contiguous prefix immediately. Some ensembles
          // repeat segments slowly or omit a reliable LAST indication.
          uint8_t contiguousLast = 0;
          while (contiguousLast < 7 &&
                 (_dlsReceivedMask & (1U << (contiguousLast + 1))) != 0) {
            ++contiguousLast;
          }
          publishDlsSegments(0, contiguousLast, false);
        } else {
          // If reception began in the middle of a carousel, a fragment is more
          // useful than a permanently empty information area.
          publishDlsSegments(segment, segment, false);
        }
      }
    }
  } else if (header.isPad()) {
    processMotPacket(header, payload, header.byteCount);
  }
  _dsrvPending = header.buffersRemaining > 0;
}

void DAB::resetSlideshowAssembler(bool clearImage) {
  memset(_slideshowSegmentLengths, 0, sizeof(_slideshowSegmentLengths));
  memset(_slideshowSegmentBitmap, 0, sizeof(_slideshowSegmentBitmap));
  _slideshowTransportId = 0;
  _slideshowHighestSegment = 0;
  _slideshowTotalSegments = 0;
  _slideshowExpectedLength = 0;
  _slideshowReceivedBytes = 0;
  _slideshowLastActivityMs = 0;
  _slideshowServiceId = 0;
  _slideshowComponentId = 0;
  _slideshowCollecting = false;
  if (clearImage) {
    _slideshowImageLength = 0;
    _slideshowAvailable = false;
    _slideshowUpdate = false;
  }
}

bool DAB::allSlideshowSegmentsReceived(uint16_t count) const {
  if (count == 0 || count > SLS_MAX_SEGMENTS) return false;
  for (uint16_t segment = 0; segment < count; ++segment) {
    if ((_slideshowSegmentBitmap[segment >> 3] &
         (1U << (segment & 7))) == 0) {
      return false;
    }
  }
  return true;
}

void DAB::processMotPacket(const si468x::DsrvHeader& header,
                           const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length == 0 || header.dscType != 60) return;
  // DAB DSCTy 60 carries MOT directory/header chunks as packet 0x73 and
  // object-body chunks as packet 0x74. The two-byte CRC follows the declared
  // chunk and is deliberately not copied into the image arena.
  const uint8_t packetType = payload[0];
  if (packetType != 0x73 && packetType != 0x74) return;
  ++_motPackets;
  diagnostic("[SLS] packet type=%02X bytes=%u SID=%08lX CID=%08lX head=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             packetType, length,
             static_cast<unsigned long>(header.serviceId),
             static_cast<unsigned long>(header.componentId),
             payload[0], length > 1 ? payload[1] : 0,
             length > 2 ? payload[2] : 0, length > 3 ? payload[3] : 0,
             length > 4 ? payload[4] : 0, length > 5 ? payload[5] : 0,
             length > 6 ? payload[6] : 0, length > 7 ? payload[7] : 0,
             length > 8 ? payload[8] : 0, length > 9 ? payload[9] : 0,
             length > 10 ? payload[10] : 0, length > 11 ? payload[11] : 0);
  if (!_slideshowEnabled) {
    diagnostic("[SLS][WARN] MOT ignored: slideshow mode is Off");
    return;
  }
  if (length < 11) {
    ++_commandErrors;
    diagnostic("[SLS][WARN] MOT ignored: packet is shorter than 11 bytes");
    return;
  }
  const uint16_t segmentField = static_cast<uint16_t>(payload[2] << 8) |
                                payload[3];
  const bool last = (segmentField & 0x8000U) != 0;
  const uint16_t segment = segmentField & 0x7FFFU;
  const uint32_t objectId = (static_cast<uint32_t>(payload[4]) << 16) |
                            (static_cast<uint32_t>(payload[5]) << 8) |
                            payload[6];
  // Bits 7..5 of byte 7 carry data-group flags. The chunk length itself is
  // the remaining 13-bit big-endian value (E1 F5 -> 01F5 -> 501 bytes).
  const uint16_t dataLength =
      static_cast<uint16_t>((payload[7] & 0x1FU) << 8) | payload[8];
  const uint32_t requiredLength = 9UL + dataLength + 2UL;
  if (requiredLength > length) {
    ++_commandErrors;
    diagnostic("[SLS][WARN] short MOT packet type=%02X bytes=%u expected=%lu",
               packetType, length, static_cast<unsigned long>(requiredLength));
    return;
  }
  if (packetType == 0x73) {
    diagnostic("[SLS] header object=%06lX segment=%u%s bytes=%u",
               static_cast<unsigned long>(objectId), segment,
               last ? " last" : "", dataLength);
    return;
  }

  if (segment >= SLS_MAX_SEGMENTS || dataLength == 0 ||
      dataLength > SLS_SEGMENT_SLOT_BYTES) {
    diagnostic("[SLS][WARN] segment dropped: index=%u length=%u",
               segment, dataLength);
    return;
  }

  if (!_slideshowCollecting) {
    // Keep the completed image intact in the single arena until the UI has
    // decoded it. Otherwise the next carousel packet clears manual readiness.
    if (_slideshowAvailable) return;
    resetSlideshowAssembler(true);
    _slideshowCollecting = true;
    _slideshowTransportId = objectId;
    _slideshowServiceId = header.serviceId;
    _slideshowComponentId = header.componentId;
  } else if (_slideshowTransportId != objectId) {
    if (segment != 0) {
      diagnostic("[SLS] object %06lX segment=%u skipped; collecting %06lX",
                 static_cast<unsigned long>(objectId), segment,
                 static_cast<unsigned long>(_slideshowTransportId));
      return;
    }
    // Tuning can begin in the middle of an object. A segment 0 belonging to
    // another Object ID is a clean boundary and is preferable to waiting for
    // missing leading segments of the incomplete object until timeout.
    diagnostic("[SLS] switching object %06lX -> %06lX at segment 0",
               static_cast<unsigned long>(_slideshowTransportId),
               static_cast<unsigned long>(objectId));
    resetSlideshowAssembler(true);
    _slideshowCollecting = true;
    _slideshowTransportId = objectId;
    _slideshowServiceId = header.serviceId;
    _slideshowComponentId = header.componentId;
  }

  if (last) _slideshowTotalSegments = segment + 1;

  const uint8_t mask = static_cast<uint8_t>(1U << (segment & 7));
  uint8_t& bitmapByte = _slideshowSegmentBitmap[segment >> 3];
  if ((bitmapByte & mask) != 0) {
    if (segment == 0 && _slideshowTotalSegments == 0 &&
        _slideshowHighestSegment > 0 &&
        allSlideshowSegmentsReceived(_slideshowHighestSegment + 1)) {
      _slideshowTotalSegments = _slideshowHighestSegment + 1;
      finishSlideshowObject();
    }
    return;
  }

  if (_slideshowReceivedBytes + dataLength > DAB_SLS_ARENA_BYTES) {
    diagnostic("[SLS][WARN] object %06lX exceeds RAM arena",
               static_cast<unsigned long>(objectId));
    resetSlideshowAssembler(true);
    return;
  }
  memcpy(_slideshowArena + segment * SLS_SEGMENT_SLOT_BYTES,
          payload + 9, dataLength);
  _slideshowSegmentLengths[segment] = dataLength;
  bitmapByte |= mask;
  _slideshowReceivedBytes += dataLength;
  if (segment > _slideshowHighestSegment) _slideshowHighestSegment = segment;
  _slideshowLastActivityMs = millis();

  diagnostic("[SLS] body object=%06lX segment=%u%s bytes=%u total=%lu",
             static_cast<unsigned long>(objectId), segment,
             last ? " last" : "", dataLength,
             static_cast<unsigned long>(_slideshowReceivedBytes));

  const uint16_t count = _slideshowTotalSegments != 0
                             ? _slideshowTotalSegments
                             : _slideshowHighestSegment + 1;
  if (_slideshowTotalSegments != 0 &&
      allSlideshowSegmentsReceived(count)) {
    finishSlideshowObject();
  }
}

void DAB::finishSlideshowObject() {
  const uint16_t count = _slideshowTotalSegments != 0
                             ? _slideshowTotalSegments
                             : _slideshowHighestSegment + 1;
  if (!allSlideshowSegmentsReceived(count)) return;

  uint32_t outputLength = 0;
  for (uint16_t segment = 0; segment < count; ++segment) {
    const uint16_t length = _slideshowSegmentLengths[segment];
    if (length == 0 || outputLength + length > DAB_SLS_ARENA_BYTES) {
      diagnostic("[SLS][WARN] assembly bounds failure at segment %u", segment);
      resetSlideshowAssembler(true);
      return;
    }
    memmove(_slideshowArena + outputLength,
            _slideshowArena + segment * SLS_SEGMENT_SLOT_BYTES, length);
    outputLength += length;
  }
  uint32_t imageOffset = outputLength;
  bool jpeg = false;
  bool png = false;
  for (uint32_t offset = 0; offset < outputLength; ++offset) {
    if (offset + 3 <= outputLength && _slideshowArena[offset] == 0xFF &&
        _slideshowArena[offset + 1] == 0xD8 &&
        _slideshowArena[offset + 2] == 0xFF) {
      imageOffset = offset;
      jpeg = true;
      break;
    }
    if (offset + 8 <= outputLength && _slideshowArena[offset] == 0x89 &&
        _slideshowArena[offset + 1] == 0x50 &&
        _slideshowArena[offset + 2] == 0x4E &&
        _slideshowArena[offset + 3] == 0x47 &&
        _slideshowArena[offset + 4] == 0x0D &&
        _slideshowArena[offset + 5] == 0x0A &&
        _slideshowArena[offset + 6] == 0x1A &&
        _slideshowArena[offset + 7] == 0x0A) {
      imageOffset = offset;
      png = true;
      break;
    }
  }
  if (!jpeg && !png) {
    diagnostic("[SLS][WARN] assembled object is not JPEG or PNG");
    resetSlideshowAssembler(true);
    return;
  }
  if (imageOffset != 0) {
    memmove(_slideshowArena, _slideshowArena + imageOffset,
            outputLength - imageOffset);
    outputLength -= imageOffset;
  }
  if (jpeg && outputLength >= 2) {
    // Ignore MOT padding after the JPEG end marker.
    for (uint32_t offset = outputLength - 1; offset > 0; --offset) {
      if (_slideshowArena[offset - 1] == 0xFF &&
          _slideshowArena[offset] == 0xD9) {
        outputLength = offset + 1;
        break;
      }
    }
  }

  uint32_t hash = 2166136261UL;
  for (uint32_t index = 0; index < outputLength; ++index) {
    hash = (hash ^ _slideshowArena[index]) * 16777619UL;
  }
  const bool changed = !_slideshowAvailable ||
                       outputLength != _slideshowImageLength ||
                       hash != _slideshowImageHash;
  _slideshowImageLength = outputLength;
  _slideshowImageHash = hash;
  _slideshowAvailable = true;
  _slideshowUpdate |= changed;
  diagnostic("[SLS] %s ready: %lu bytes, %u segments, imageOffset=%lu free=%u largest=%u%s",
             jpeg ? "JPEG" : "PNG", static_cast<unsigned long>(outputLength),
             count, static_cast<unsigned long>(imageOffset), ESP.getFreeHeap(),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             changed ? "" : " (unchanged)");
  resetSlideshowAssembler(false);
}

void DAB::processDabEvent() {
  _deviceEventPending = false;
  si468x::DabEventStatus event;
  const si468x::Result result = _radio.dabGetEventStatus(event, true);
  if (result != si468x::Result::Ok) {
    fail(result, "DAB_GET_EVENT_STATUS");
    return;
  }
  if (event.serviceListAvailable || event.serviceListInterrupt) {
    refreshDabServiceList();
  }
  if (event.reconfiguration || event.reconfigurationWarning) {
    diagnostic("[RADIO] DAB reconfiguration event: warning=%u active=%u",
               event.reconfigurationWarning ? 1U : 0U,
               event.reconfiguration ? 1U : 0U);
  }
}

bool DAB::refreshDabServiceList() {
  si468x::DabServiceListSink sink;
  sink.context = this;
  sink.onHeader = serviceListHeader;
  sink.onService = serviceListService;
  sink.onComponent = serviceListComponent;
  si468x::DabServiceListParser parser;
  parser.setSink(sink);
  const si468x::Result result = _radio.readDabServiceList(parser, 256);
  if (result != si468x::Result::Ok) {
    diagnostic("[RADIO][WARN] service list: %s", resultName(result));
    return false;
  }
  diagnostic("[RADIO] service list: %u services", numberofservices);
  return true;
}

void DAB::updateFmStatus(bool acknowledgeStc) {
  si468x::FmRsqStatus value;
  const si468x::Result result =
      _radio.fmRsqStatus(value, false, false, false, acknowledgeStc);
  if (result != si468x::Result::Ok) {
    fail(result, "FM_RSQ_STATUS");
    return;
  }
  freq = value.frequency10kHz;
  signalstrength = value.rssi;
  snr = value.snr;
  valid = value.valid;
  const uint32_t now = millis();
  if (acknowledgeStc || _lastAudioStatusMs == 0 ||
      now - _lastAudioStatusMs >= 1000) {
    si468x::FmAcfStatus audio;
    if (_radio.fmAcfStatus(audio) == si468x::Result::Ok) {
      fmPilot = audio.pilot;
      fmStereoBlend = audio.stereoBlendPercent;
    }
    _lastAudioStatusMs = now;
  }
  error = 0;
  if (_lastStatusDiagnosticMs == 0 ||
      now - _lastStatusDiagnosticMs >= 5000) {
    diagnostic("[RADIO] FM status: %u.%02u MHz valid=%u RSSI=%d SNR=%d pilot=%u blend=%u%%",
               freq / 100, freq % 100, valid ? 1U : 0U, signalstrength, snr,
               fmPilot ? 1U : 0U, fmStereoBlend);
    _lastStatusDiagnosticMs = now;
  }
}

void DAB::updateDabStatus(bool acknowledgeStc) {
  si468x::DabDigradStatus grade;
  si468x::Result result =
      _radio.dabDigradStatus(grade, false, false, acknowledgeStc);
  if (result != si468x::Result::Ok) {
    fail(result, "DAB_DIGRAD_STATUS");
    return;
  }

  freq_index = grade.tuneIndex;
  signalstrength = grade.rssi;
  snr = static_cast<int8_t>(grade.cnr);
  quality = grade.ficQuality;
  valid = grade.valid && grade.acquired;
  error = 0;

  const uint32_t now = millis();
  const bool refreshMetadata = acknowledgeStc ||
      _lastMetadataStatusMs == 0 || now - _lastMetadataStatusMs >= 5000;
  if (valid && refreshMetadata) {
    si468x::DabEnsembleInfo ensemble;
    if (_radio.dabGetEnsembleInfo(ensemble) == si468x::Result::Ok) {
      EnsembleID = ensemble.ensembleId;
      ECC = ensemble.ecc;
      memcpy(Ensemble, ensemble.label, sizeof(Ensemble));
      Ensemble[16] = 0;
    }
  }

  if (refreshMetadata && _serviceId != 0 && _componentId != 0) {
    si468x::DabAudioInfo audio;
    if (_radio.dabGetAudioInfo(audio) == si468x::Result::Ok) {
      bitrate = audio.bitRateKbps;
      samplerate = audio.sampleRateHz;
      mode = static_cast<AudioMode>(audio.audioMode & 0x03);
    }
    si468x::DabServiceInfo serviceInfo;
    if (_radio.dabGetServiceInfo(_serviceId, serviceInfo) == si468x::Result::Ok) {
      pty = serviceInfo.pty;
      type = serviceInfo.dataService ? SERVICE_DATA : SERVICE_AUDIO;
    }
    si468x::DabSubchannelInfo subchannel;
    if (_radio.dabGetSubchannelInfo(_serviceId, _componentId, subchannel) ==
        si468x::Result::Ok) {
      dabplus = subchannel.serviceMode == 4;
      bitrate = subchannel.bitRateKbps;
    }
  }
  if (refreshMetadata) _lastMetadataStatusMs = now;

  if (_lastStatusDiagnosticMs == 0 ||
      now - _lastStatusDiagnosticMs >= 5000) {
    diagnostic("[RADIO] DAB status: index=%u valid=%u RSSI=%d CNR=%d FIC=%u DSRV=%lu DLS=%lu MOT=%lu OVF=%lu",
               freq_index, valid ? 1U : 0U, signalstrength, snr, quality,
               static_cast<unsigned long>(_dsrvPackets),
               static_cast<unsigned long>(_dlsPackets),
               static_cast<unsigned long>(_motPackets),
               static_cast<unsigned long>(_dsrvOverflows));
    _lastStatusDiagnosticMs = now;
  }
}

uint16_t DAB::decodeRdsGroup(const si468x::FmRdsGroup& group) {
  if (!group.blockUsable(1) || !group.blockUsable(3)) return 0;
  const uint16_t blockB = group.block[1];
  const uint16_t blockC = group.block[2];
  const uint16_t blockD = group.block[3];
  const uint8_t rdsGroup = blockB >> 11;
  uint16_t changed = 0;

  switch (rdsGroup) {
    case RDS_GROUP_0A:
    case RDS_GROUP_0B: {
      ta = (blockB & 0x0010) != 0;
      const uint8_t offset = blockB & 0x03;
      const uint8_t segmentBit = static_cast<uint8_t>(1U << offset);
      const uint8_t first = blockD >> 8;
      const uint8_t second = blockD & 0xFF;
      const bool seen = (_rdsPsSeenMask & segmentBit) != 0;
      const bool repeated =
          seen && _rdsProgramService[0][offset * 2] == first &&
          _rdsProgramService[0][offset * 2 + 1] == second;

      _rdsProgramService[0][offset * 2] = first;
      _rdsProgramService[0][offset * 2 + 1] = second;
      _rdsPsSeenMask |= segmentBit;
      if (repeated) {
        _rdsPsStableMask |= segmentBit;
      } else {
        _rdsPsStableMask &= static_cast<uint8_t>(~segmentBit);
      }

      // Segmenty PS nemuseji prijit v poradi 0..3. Nazev prijmeme, jakmile
      // se kazda ze ctyr dvojic alespon jednou beze zmeny zopakuje.
      if (_rdsPsStableMask == 0x0F &&
          memcmp(ps, _rdsProgramService[0], 8) != 0) {
        memcpy(ps, _rdsProgramService[0], 8);
        ps[8] = 0;
        changed |= 0x0001;
        diagnostic("[RDS] stable PS detected: %.8s", ps);
      }
      break;
    }

    case RDS_GROUP_1A:
      if (group.blockUsable(2) && (blockC & 0x7000) == 0) {
        ECC = blockC & 0xFF;
      }
      break;

    case RDS_GROUP_2A:
    case RDS_GROUP_2B: {
      if (rdsGroup == RDS_GROUP_2A && !group.blockUsable(2)) break;
      const uint8_t textAbState =
          static_cast<uint8_t>(((blockB & 0x0010) ? 0x80 : 0) + rdsGroup);
      if (textAbState != _lastTextAbState) {
        memset(_rdsText, 0, sizeof(_rdsText));
      }
      _lastTextAbState = textAbState;
      const uint8_t offset =
          (blockB & 0x0F) * (rdsGroup == RDS_GROUP_2A ? 4 : 2);
      const uint8_t byteCount = rdsGroup == RDS_GROUP_2A ? 4 : 2;
      memcpy(_rdsText[1] + offset, _rdsText[0] + offset, byteCount);
      if (rdsGroup == RDS_GROUP_2A) {
        _rdsText[0][offset] = blockC >> 8;
        _rdsText[0][offset + 1] = blockC & 0xFF;
        _rdsText[0][offset + 2] = blockD >> 8;
        _rdsText[0][offset + 3] = blockD & 0xFF;
      } else {
        _rdsText[0][offset] = blockD >> 8;
        _rdsText[0][offset + 1] = blockD & 0xFF;
      }
      for (uint8_t i = 0; i < byteCount; ++i) {
        if (_rdsText[0][offset + i] == 0x0D) _rdsText[0][offset + i] = 0;
      }

      if (memcmp(_rdsText[0], _rdsText[1], 64) == 0 &&
          memcmp(ServiceData, _rdsText[0], 64) != 0) {
        memcpy(ServiceData, _rdsText[0], 64);
        ServiceData[64] = 0;
        ServiceDataLength = 64;
        while (ServiceDataLength > 0 &&
               (ServiceData[ServiceDataLength - 1] == 0 ||
                ServiceData[ServiceDataLength - 1] == ' ')) {
          --ServiceDataLength;
        }
        ServiceDataCharset = 0;
        diagnostic("[RDS] RadioText complete: bytes=%u AB=%u type=%s",
                   ServiceDataLength, textAbState >> 7,
                   rdsGroup == RDS_GROUP_2A ? "2A" : "2B");
        changed |= 0x0004;
      }
      break;
    }

    case RDS_GROUP_4A: {
      if (!group.blockUsable(2)) break;
      uint32_t mjd = blockB & 0x03;
      mjd = (mjd << 15) + ((blockC >> 1) & 0x7FFF);
      long j = mjd + 2400001 + 68569;
      const long c = 4 * j / 146097;
      j = j - (146097 * c + 3) / 4;
      const long y = 4000 * (j + 1) / 1461001;
      j = j - 1461 * y / 4 + 31;
      const long m = 80 * j / 2447;
      Days = j - 2447 * m / 80;
      j = m / 11;
      Months = m + 2 - 12 * j;
      Year = 100 * (c - 49) + y + j;
      Hours = ((blockD >> 12) & 0x0F) + ((blockC << 4) & 0x10);
      Minutes = (blockD >> 6) & 0x3F;
      changed |= 0x0010;
      break;
    }
  }
  return changed;
}

void DAB::diagnostic(const char* format, ...) {
  if (!_diagnostics) return;
  char buffer[192];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  _diagnostics->println(buffer);
}

const char* DAB::resultName(si468x::Result result) {
  switch (result) {
    case si468x::Result::Ok: return "ok";
    case si468x::Result::Pending: return "pending";
    case si468x::Result::Busy: return "busy";
    case si468x::Result::InvalidArgument: return "invalid-argument";
    case si468x::Result::NoTransport: return "no-transport";
    case si468x::Result::NoTimer: return "no-timer";
    case si468x::Result::Timeout: return "timeout";
    case si468x::Result::TransportError: return "transport-error";
    case si468x::Result::DeviceError: return "device-error";
    case si468x::Result::BufferTooSmall: return "buffer-too-small";
    case si468x::Result::Unsupported: return "unsupported";
    case si468x::Result::MalformedReply: return "malformed-reply";
    case si468x::Result::EndOfData: return "end-of-data";
  }
  return "unknown";
}
