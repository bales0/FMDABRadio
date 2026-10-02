/*
 * ESP32 cooperative Si4684 radio backend implementation.
 *
 * Invariant: the GPIO26 ISR only records an edge. All SPI transfers, command
 * state transitions, parsing and diagnostic output run from DAB::task().
 */
#include "si4684.h"
#include "mot_assembly_policy.h"

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

bool dabLabelHasContent(const char* label) {
  if (label == nullptr) return false;
  for (uint8_t index = 0; index < 16U; ++index) {
    const uint8_t value = static_cast<uint8_t>(label[index]);
    if (value != 0U && value != static_cast<uint8_t>(' ')) return true;
  }
  return false;
}

}  // namespace

DAB::DAB()
    : ECC(0),
      EnsembleID(0),
      ServiceDataLength(0),
      ServiceDataCharset(0),
      ActiveCharset(0),
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
      Seconds(0),
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
      _stateAfterServiceStop(State::Ready),
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
      _generation(1),
      _commandGeneration(1),
      _signalSampleGeneration(0),
      _timeSampleGeneration(0),
      _fmClockGeneration(0),
      _dabServiceListGeneration(0),
      _stateDeadlineMs(0),
      _operationDeadlineMs(0),
      _patchOffset(0),
      _fmTuneTarget(0),
      _fmBandBottom(8750),
      _fmBandTop(10800),
      _fmSeekSpacing(10),
      _fmDeEmphasis(1),
      _dabTuneTarget(0),
      _dabTuneBusyRetries(0),
      _dabTuneRetryNotBeforeMs(0),
      _serviceId(0),
      _componentId(0),
      _activeServiceId(0),
      _activeComponentId(0),
      _activeServiceValid(false),
      _dataServiceId(0),
      _dataComponentId(0),
      _activeDataServiceId(0),
      _activeDataComponentId(0),
      _activeDataServiceValid(false),
      _dataServicePending(false),
      _dataServiceStopPending(false),
      _serviceTransitionPending(false),
      _dataServiceRetryCount(0),
      _dataServiceRetryNotBeforeMs(0),
      _serviceStartRetries(0),
      _statusAcknowledgesStc(false),
      _dabSignalRefreshPending(false),
      _dabServiceListRefreshPending(false),
      _dabEnsembleRefreshPending(false),
      _dabTimeRefreshPending(false),
      _dabAudioRefreshPending(false),
      _dabAudioInfoValid(false),
      _dabAudioInfoRetryCount(0),
      _dabAudioInfoNotBeforeMs(0),
      _dabServiceInfoRefreshPending(false),
      _dabSubchannelRefreshPending(false),
      _dsrvBurstCount(0),
      _fmRsqNextDueMs(0),
      _fmAcfNextDueMs(0),
      _fmRdsNextDueMs(0),
      _fmRdsConsecutiveErrors(0),
      _dabSignalNextDueMs(0),
      _dabEnsembleNextDueMs(0),
      _dabTimeNextDueMs(0),
      _dabAudioNextDueMs(0),
      _dabServiceInfoNextDueMs(0),
      _dabSubchannelNextDueMs(0),
      _lastLowPriorityCommandMs(0),
      _consecutiveCtsTimeouts(0),
      _lastRecoveryMs(0),
      _rdsPsSeenMask(0),
      _rdsPsStableMask(0),
      _lastTextAbState(0xFF),
      _dlsReceivedMask(0),
      _dlsLastSegment(0xFF),
      _dlsToggle(0xFF),
      _dlsCharset(0),
      _currentServiceIndex(0),
      _currentServiceStored(false),
      _matchingDataCandidateCount(0),
      _fallbackDataCandidateCount(0),
      _matchingDataServiceId(0),
      _matchingDataComponentId(0),
      _fallbackDataServiceId(0),
      _fallbackDataComponentId(0),
      _slideshowTransportId(0),
      _slideshowCompletedTransportId(0),
      _slideshowHighestSegment(0),
      _slideshowTotalSegments(0),
      _slideshowExpectedLength(0),
      _slideshowReceivedBytes(0),
      _slideshowImageLength(0),
      _slideshowLastImageLength(0),
      _slideshowLastActivityMs(0),
      _slideshowServiceId(0),
      _slideshowComponentId(0),
      _slideshowImageHash(0),
      _slideshowEnabled(false),
      _slideshowCollecting(false),
      _slideshowAvailable(false),
      _slideshowUpdate(false),
      _slideshowPublishedPending(false),
      _slideshowCompletedTransportValid(false),
      _irqCounter(0),
      _commandErrors(0),
      _dsrvOverflows(0),
      _dsrvPackets(0),
      _dlsPackets(0),
      _motPackets(0),
      _dsrvDiagnosticDivider(0),
      _lastAudioStatusMs(0),
      _lastMetadataStatusMs(0),
      _lastStatusDiagnosticMs(0),
      _lastMotSegmentLogMs(0),
      _diagDabHostStarvationCount(0),
      _diagDabHostStarvationMaxGapUs(0),
      _diagDabGenuineCtsTimeoutCount(0),
      _diagDabTuneBusyCount(0),
      _diagDabAudioNotAvailableCount(0),
      _diagLastCtsReportMs(0),
      _diagReportedHostStarvationCount(0),
      _diagReportedGenuineCtsCount(0) {
  memset(Ensemble, 0, sizeof(Ensemble));
  memset(ServiceData, 0, sizeof(ServiceData));
  memset(ActiveLabel, 0, sizeof(ActiveLabel));
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
  _radio.setCtsPollIntervalUs(2000);
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

  // configurePins() is called once during setup.  Calling detachInterrupt()
  // before Arduino has installed the GPIO ISR service produces an ESP-IDF
  // startup error on a cold boot.  Install the permanent Si4684 INTB handler
  // directly; GPIO26 is dedicated to INTB in this hardware.
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
  if (enabled && _band == 0 && _activeServiceValid &&
      _dataServiceId != 0U && !_activeDataServiceValid) {
    _dataServicePending = true;
  }
  if (!enabled) {
    releaseSlideshowArena();
    _dataServicePending = false;
    _dataServiceStopPending = _activeDataServiceValid;
  }
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

bool DAB::slideshowCollecting() const {
  return _slideshowCollecting;
}

uint8_t DAB::slideshowProgress() const {
  if (slideshowAvailable()) return 100;
  if (!_slideshowCollecting) return 0;
  uint32_t percent = 0;
  if (_slideshowExpectedLength != 0U) {
    percent = (_slideshowReceivedBytes * 100UL) / _slideshowExpectedLength;
  } else if (_slideshowTotalSegments != 0U) {
    uint16_t received = 0;
    for (uint16_t segment = 0; segment < _slideshowTotalSegments; ++segment) {
      if ((_slideshowSegmentBitmap[segment >> 3] &
           (1U << (segment & 7U))) != 0U) ++received;
    }
    percent = (static_cast<uint32_t>(received) * 100UL) /
              _slideshowTotalSegments;
  }
  return static_cast<uint8_t>(percent > 99U ? 99U : percent);
}

bool DAB::takeSlideshowUpdate() {
  const bool value = _slideshowUpdate;
  _slideshowUpdate = false;
  return value;
}

void DAB::acknowledgeSlideshow() {
  _slideshowUpdate = false;
  _slideshowPublishedPending = false;
}

void DAB::discardSlideshow() {
  _slideshowAvailable = false;
  _slideshowUpdate = false;
  _slideshowPublishedPending = false;
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
  _generation = dab_scheduler::nextGeneration(_generation);
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
  _consecutiveCtsTimeouts = 0;
  _dsrvBurstCount = 0;
  _fmRdsConsecutiveErrors = 0;
  _activeServiceValid = false;
  _activeDataServiceValid = false;
  _dataServicePending = false;
  _dataServiceStopPending = false;
  _serviceTransitionPending = false;
  _dataServiceId = 0;
  _dataComponentId = 0;
  dab_scheduler::resetRetry(_dataServiceRetryCount,
                            _dataServiceRetryNotBeforeMs);
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
  memset(ActiveLabel, 0, sizeof(ActiveLabel));
  ActiveCharset = 0;
  memset(ps, 0, sizeof(ps));
  rdsSync = false;
  tp = false;
  ta = false;
  fmClockValid = false;
  fmLocalOffsetHalfHours = 0;
  _fmAfList.clear();
  _fmClockValidator.reset();
  fmPilot = false;
  fmStereoBlend = 0;
  resetDlsAssembler();
  resetSlideshowAssembler(true);
  resetPeriodicDeadlines(millis());

  // Preserve the original FMDABRadio hardware power/reset sequence exactly.
  // This board proved reliable with: PWREN HIGH -> 100 ms -> RST LOW ->
  // 100 ms -> RST HIGH -> 100 ms -> POWER_UP.  Holding RST low across the
  // power ramp did not create the same qualified reset edge on this hardware
  // and could leave the external-NVSPI boot in the bootloader image.
  digitalWrite(_powerEnablePin, HIGH);
  digitalWrite(_resetPin, HIGH);
  _state = State::PowerSettle;
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
  pi = 0;
  pty = 0;
  ECC = 0;
  rdsSync = false;
  tp = false;
  ta = false;
  fmClockValid = false;
  _fmAfList.clear();
  _fmClockValidator.reset();
  fmPilot = false;
  fmStereoBlend = 0;
  diagnostic("[AF] candidate PI invalidated after tune");
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
  pi = 0;
  pty = 0;
  ECC = 0;
  rdsSync = false;
  tp = false;
  ta = false;
  fmClockValid = false;
  _fmAfList.clear();
  _fmClockValidator.reset();
  fmPilot = false;
  fmStereoBlend = 0;
  diagnostic("[AF] candidate PI invalidated after seek");
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
  _dabTuneBusyRetries = 0;
  _dabTuneRetryNotBeforeMs = 0;
  resetDabAudioInfo();
  numberofservices = 0;
  valid = false;
  signalstrength = 0;
  snr = 0;
  quality = 0;
  bitrate = 0;
  samplerate = 0;
  type = SERVICE_NONE;
  mode = DUAL;
  dabplus = false;
  pty = 0;
  resetDlsAssembler();
  resetSlideshowAssembler(true);
  _slideshowCompletedTransportValid = false;
  _slideshowLastImageLength = 0U;
  _slideshowImageHash = 0U;
  diagnostic("[RADIO] DAB tune start: index=%u frequency=%lu kHz", frequencyIndex,
             static_cast<unsigned long>(freq_khz(frequencyIndex)));
  _dabSwitch.requestSwitch();
  _serviceTransitionPending = true;
  _dataServicePending = false;
  _dataServiceId = 0;
  _dataComponentId = 0;
  if (_activeDataServiceValid) {
    _stateAfterServiceStop = State::DabTuneCommand;
    startDabStopDataCommand();
    return _commandPending;
  }
  if (_activeServiceValid) {
    _stateAfterServiceStop = State::DabTuneCommand;
    startDabStopCommand();
    return _commandPending;
  }
  _serviceTransitionPending = false;
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
  // Older EEPROM records may contain the two service-list information bytes
  // in bits 16..31. The Si4684 DSRV header exposes the actual 16-bit COMP_ID.
  componentId &= 0xFFFFU;
  _componentId = componentId;
  _serviceStartRetries = 0;
  _operationDeadlineMs = millis() + RADIO_TUNE_TIMEOUT_MS;
  _dabTuneBusyRetries = 0;
  _dabTuneRetryNotBeforeMs = 0;
  resetDabAudioInfo();
  _stcPending = false;
  memset(ServiceData, 0, sizeof(ServiceData));
  ServiceDataLength = 0;
  ServiceDataCharset = 0;
  memset(ActiveLabel, 0, sizeof(ActiveLabel));
  ActiveCharset = 0;
  bitrate = 0;
  samplerate = 0;
  type = SERVICE_NONE;
  mode = DUAL;
  dabplus = false;
  pty = 0;
  resetDlsAssembler();
  resetSlideshowAssembler(true);
  _slideshowCompletedTransportValid = false;
  _slideshowLastImageLength = 0U;
  _slideshowImageHash = 0U;
  diagnostic("[RADIO] DAB service request: index=%u SID=0x%08lX CID=0x%08lX",
             frequencyIndex, static_cast<unsigned long>(serviceId),
             static_cast<unsigned long>(componentId));
  _dabSwitch.requestSwitch();

  _serviceTransitionPending = true;
  _dataServicePending = false;
  _dataServiceId = 0;
  _dataComponentId = 0;
  const State afterStop =
      (freq_index == frequencyIndex && valid)
          ? State::DabServiceCommand : State::DabTuneCommand;
  _stateAfterTune = State::DabServiceCommand;
  if (_activeDataServiceValid) {
    _stateAfterServiceStop = afterStop;
    startDabStopDataCommand();
    return _commandPending;
  }

  if (_activeServiceValid) {
    _stateAfterServiceStop = afterStop;
    startDabStopCommand();
    return _commandPending;
  }

  if (freq_index == frequencyIndex && valid) {
    _serviceTransitionPending = false;
    startDabServiceCommand();
    return _commandPending;
  }

  _serviceTransitionPending = false;
  return startCore(_radio.startDabTune(frequencyIndex), State::DabTuneCommand);
}

bool DAB::requestDabServiceListRefresh() {
  if (_band != 0 || _state == State::Off || _state == State::Failed) {
    return false;
  }
  _dabServiceListRefreshPending = true;
  return true;
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
    const uint32_t now = millis();
    updatePeriodicRequests(now);
    scheduleReadyWork(now);
  }

  if (_slideshowCollecting && _slideshowLastActivityMs != 0 &&
      static_cast<uint32_t>(millis() - _slideshowLastActivityMs) >
          SLS_COLLECTION_TIMEOUT_MS) {
    diagnostic("[SLS][WARN] collection timeout: object=%06lX bytes=%lu",
               static_cast<unsigned long>(_slideshowTransportId),
               static_cast<unsigned long>(_slideshowReceivedBytes));
    resetSlideshowAssembler(false);
  }

  const uint32_t now = millis();
  const bool ctsCountersChanged =
      _diagReportedHostStarvationCount != _diagDabHostStarvationCount ||
      _diagReportedGenuineCtsCount != _diagDabGenuineCtsTimeoutCount;
  if (ctsCountersChanged &&
      (_diagLastCtsReportMs == 0U ||
       static_cast<uint32_t>(now - _diagLastCtsReportMs) >= 5000U)) {
    _diagLastCtsReportMs = now;
    _diagReportedHostStarvationCount = _diagDabHostStarvationCount;
    _diagReportedGenuineCtsCount = _diagDabGenuineCtsTimeoutCount;
    diagnostic("[DAB/CTS] hostStarved=%lu maxGap=%luus genuine=%lu consecutive=%u",
               static_cast<unsigned long>(_diagDabHostStarvationCount),
               static_cast<unsigned long>(_diagDabHostStarvationMaxGapUs),
               static_cast<unsigned long>(_diagDabGenuineCtsTimeoutCount),
               _consecutiveCtsTimeouts);
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
    case State::PowerSettle: return "power-settle";
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
    case State::IdentifyPart: return "identify-part";
    case State::IdentifySystem: return "identify-system";
    case State::IdentifyFunction: return "identify-function";
    case State::VerifySlideshowRead: return "verify-slideshow";
    case State::Ready: return "ready";
    case State::FmTuneCommand: return "fm-tune-command";
    case State::FmTuneWaitStc: return "fm-tune-stc";
    case State::FmSeekCommand: return "fm-seek-command";
    case State::FmSeekWaitStc: return "fm-seek-stc";
    case State::DabTuneCommand: return "dab-tune-command";
    case State::DabTuneRetry: return "dab-tune-retry";
    case State::DabTuneWaitStc: return "dab-tune-stc";
    case State::DabStopDataServiceCommand: return "dab-stop-data";
    case State::DabStopServiceCommand: return "dab-stop-service";
    case State::DabStopRetry: return "dab-stop-retry";
    case State::DabServiceSettle: return "dab-service-settle";
    case State::DabServiceCommand: return "dab-service";
    case State::DabServiceRetry: return "dab-service-retry";
    case State::DabDataServiceCommand: return "dab-data-service";
    case State::VolumeCommand: return "volume";
    case State::AudioConfigCommand: return "audio-config";
    case State::SlideshowProperty: return "slideshow-property";
    case State::FmRsqCommand: return "fm-rsq";
    case State::FmAcfCommand: return "fm-acf";
    case State::FmRdsCommand: return "fm-rds";
    case State::DabSignalCommand: return "dab-signal";
    case State::DabEventCommand: return "dab-event";
    case State::DabServiceListCommand: return "dab-service-list";
    case State::DabEnsembleCommand: return "dab-ensemble";
    case State::DabTimeCommand: return "dab-time";
    case State::DabAudioCommand: return "dab-audio";
    case State::DabServiceInfoCommand: return "dab-service-info";
    case State::DabSubchannelCommand: return "dab-subchannel";
    case State::DsrvCommand: return "dsrv";
    case State::Failed: return "failed";
  }
  return "unknown";
}

bool DAB::status() {
  // RF/status traffic is owned by the cooperative scheduler. UI callers only
  // consume the most recent cached sample.
  return ready() && error == 0;
}

bool DAB::status(uint32_t serviceId, uint32_t componentId) {
  _serviceId = serviceId;
  _componentId = componentId & 0xFFFFU;
  return status();
}

bool DAB::time(DABTime* value) {
  if (!value || _band != 0 || _timeSampleGeneration == 0) return true;
  value->Year = Year;
  value->Months = Months;
  value->Days = Days;
  value->Hours = Hours;
  value->Minutes = Minutes;
  value->Seconds = Seconds;
  return false;
}

void DAB::mono(bool enable) {
  if (ready() && !_commandPending) {
    startProperty(0x0302, enable ? 1 : 0, State::AudioConfigCommand);
  }
}

void DAB::mute(bool left, bool right) {
  if (ready() && !_commandPending) {
    startProperty(0x0301, static_cast<uint16_t>((left ? 1U : 0U) |
                                                (right ? 2U : 0U)),
                  State::AudioConfigCommand);
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
uint32_t DAB::signalSampleGeneration() const {
  return _signalSampleGeneration;
}
uint32_t DAB::timeSampleGeneration() const { return _timeSampleGeneration; }
uint32_t DAB::fmClockGeneration() const { return _fmClockGeneration; }
uint32_t DAB::dabServiceListGeneration() const {
  return _dabServiceListGeneration;
}
const fm_features::AfList& DAB::fmAfList() const { return _fmAfList; }

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
  // ERR/FATAL can remain asserted across several CTS safety polls for one
  // failed command. Count the completed Result once in commandCompleted(),
  // rather than once per repeated STATUS byte.
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
  self->_dataServiceId = 0;
  self->_dataComponentId = 0;
  self->_matchingDataCandidateCount = 0;
  self->_fallbackDataCandidateCount = 0;
  self->_matchingDataServiceId = 0;
  self->_matchingDataComponentId = 0;
  self->_fallbackDataServiceId = 0;
  self->_fallbackDataComponentId = 0;
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
    target.Charset = entry.labelCharset == 0x04U ? 0x06U : entry.labelCharset;
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
  if (self->_dataServiceId == 0 && entry.transportModeId == 3U &&
      strstr(target.Label, "tpeg") == nullptr &&
      strstr(target.Label, "TPEG") == nullptr) {
    self->_dataServiceId = target.ServiceID;
    self->_dataComponentId = entry.componentId;
  }
  if (entry.transportModeId == 3U && strstr(target.Label, "tpeg") == nullptr &&
      strstr(target.Label, "TPEG") == nullptr) {
    if (self->_fallbackDataCandidateCount != 0xFFU)
      ++self->_fallbackDataCandidateCount;
    self->_fallbackDataServiceId = target.ServiceID;
    self->_fallbackDataComponentId = entry.componentId;
    if (target.ServiceID == self->_serviceId) {
      if (self->_matchingDataCandidateCount != 0xFFU)
        ++self->_matchingDataCandidateCount;
      self->_matchingDataServiceId = target.ServiceID;
      self->_matchingDataComponentId = entry.componentId;
    }
  }
}

bool DAB::startCore(si468x::Result result, State state) {
  if (result != si468x::Result::Pending) {
    fail(result, stateName());
    return false;
  }
  _state = state;
  _commandGeneration = _generation;
  _commandPending = true;
  return true;
}

bool DAB::startRaw(si468x::Command command, const uint8_t* args, uint16_t length,
                   State state, uint32_t timeoutUsValue,
                   uint16_t replyLength) {
  if (replyLength > sizeof(_workspace)) return false;
  if (replyLength != 0) memset(_workspace, 0, replyLength);
  return startCore(_radio.startCommand(command, args, length,
                                       replyLength != 0 ? _workspace : nullptr,
                                       replyLength,
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
    case State::PowerSettle:
      if (deadlineReached(now, _stateDeadlineMs)) {
        digitalWrite(_resetPin, LOW);
        _state = State::ResetHold;
        _stateDeadlineMs = now + 100;
      }
      break;

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
        startFmStatus(true);
      } else if (deadlineReached(now, _operationDeadlineMs)) {
        fail(si468x::Result::Timeout, "FM STC");
      }
      break;

    case State::DabTuneWaitStc:
      if (_stcPending) {
        _stcPending = false;
        startDabStatus(true);
      } else if (deadlineReached(now, _operationDeadlineMs)) {
        fail(si468x::Result::Timeout, "DAB STC");
      }
      break;

    case State::DabTuneRetry:
      if (deadlineReached(now, _dabTuneRetryNotBeforeMs)) {
        startCore(_radio.startDabTune(_dabTuneTarget), State::DabTuneCommand);
      }
      break;

    case State::DabServiceRetry:
      if (deadlineReached(now, _operationDeadlineMs)) {
        fail(si468x::Result::Timeout, "START_DIGITAL_SERVICE retries");
      } else if (deadlineReached(now, _stateDeadlineMs)) {
        startDabServiceCommand();
      }
      break;

    case State::DabStopRetry:
      if (deadlineReached(now, _dabSwitch.notBeforeMs)) {
        if (_dabSwitch.resume == dab_switch::Resume::StopData)
          startDabStopDataCommand();
        else if (_dabSwitch.resume == dab_switch::Resume::StopAudio)
          startDabStopCommand();
        else
          _state = State::Ready;
      }
      break;

    case State::DabServiceSettle:
      if (deadlineReached(now, _stateDeadlineMs)) {
        if (_stateAfterServiceStop == State::DabTuneCommand) {
          startCore(_radio.startDabTune(_dabTuneTarget), State::DabTuneCommand);
        } else if (_stateAfterServiceStop == State::DabServiceCommand) {
          startDabServiceCommand();
        } else {
          _state = State::Ready;
        }
      }
      break;

    default:
      break;
  }
}

void DAB::commandCompleted() {
  const si468x::Result result = _radio.lastResult();
  if (!dab_scheduler::generationMatches(_commandGeneration, _generation)) {
    diagnostic("[RADIO][ASYNC] stale completion ignored state=%s generation=%lu current=%lu",
               stateName(), static_cast<unsigned long>(_commandGeneration),
               static_cast<unsigned long>(_generation));
    _state = State::Ready;
    return;
  }
  const bool backgroundCommand =
      _state == State::FmRsqCommand || _state == State::FmAcfCommand ||
      _state == State::FmRdsCommand || _state == State::DabSignalCommand ||
      _state == State::DabEventCommand ||
      _state == State::DabServiceListCommand ||
      _state == State::DabEnsembleCommand ||
      _state == State::DabTimeCommand || _state == State::DabAudioCommand ||
      _state == State::DabServiceInfoCommand ||
      _state == State::DabSubchannelCommand ||
      _state == State::DsrvCommand;
  const bool dabMetadataCommand =
      _state == State::DabServiceListCommand ||
      _state == State::DabEnsembleCommand ||
      _state == State::DabTimeCommand || _state == State::DabAudioCommand ||
      _state == State::DabServiceInfoCommand ||
      _state == State::DabSubchannelCommand;
  const dab_scheduler::CtsTimeoutClass ctsClass =
      dab_scheduler::classifyCtsTimeout(
          result == si468x::Result::Timeout,
          _band == 0U ? _radio.lastServiceGapUs() : 0U);
  if (ctsClass == dab_scheduler::CtsTimeoutClass::HostStarved) {
    ++_diagDabHostStarvationCount;
    if (_radio.lastServiceGapUs() > _diagDabHostStarvationMaxGapUs)
      _diagDabHostStarvationMaxGapUs = _radio.lastServiceGapUs();
    _consecutiveCtsTimeouts = dab_scheduler::nextConsecutiveCtsTimeouts(
        ctsClass, _consecutiveCtsTimeouts);
  } else if (ctsClass == dab_scheduler::CtsTimeoutClass::Genuine) {
    ++_diagDabGenuineCtsTimeoutCount;
    _consecutiveCtsTimeouts = dab_scheduler::nextConsecutiveCtsTimeouts(
        ctsClass, _consecutiveCtsTimeouts);
  } else {
    _consecutiveCtsTimeouts = dab_scheduler::nextConsecutiveCtsTimeouts(
        ctsClass, _consecutiveCtsTimeouts);
  }
  if (dab_scheduler::ctsRecoveryRequired(_consecutiveCtsTimeouts) &&
      (_lastRecoveryMs == 0U ||
       static_cast<uint32_t>(millis() - _lastRecoveryMs) >= 30000UL)) {
    _lastRecoveryMs = millis();
    diagnostic("[RADIO][RECOVERY] transport stalled; resetting Si4684 only");

    // Assert the physical Si4684 reset first. Once RSTB is LOW the device can
    // no longer complete the outstanding command, so clearing the host-side
    // command state with abortCommand() is safe and cannot desynchronise the
    // host from a still-running receiver. The TFT has a separate reset path and
    // is deliberately untouched.
    digitalWrite(_resetPin, LOW);
    (void)_radio.abortCommand();
    _commandPending = false;
    _state = State::Ready;
    beginAsync(_band);
    return;
  }
  if (result != si468x::Result::Ok) {
    const uint8_t deviceReason = _radio.lastDeviceError();
    if (_state == State::DabTuneCommand &&
        result == si468x::Result::DeviceError && deviceReason == 0x18U) {
      ++_diagDabTuneBusyCount;
      if (_dabTuneBusyRetries < dab_scheduler::DAB_TUNE_BUSY_MAX_RETRIES) {
        ++_dabTuneBusyRetries;
        _dabTuneRetryNotBeforeMs = millis() +
            dab_scheduler::DAB_TUNE_BUSY_BACKOFF_MS * _dabTuneBusyRetries;
        _state = State::DabTuneRetry;
        diagnostic("[DAB/TUNE] busy index=%u retry=%u/%u",
                   _dabTuneTarget, _dabTuneBusyRetries,
                   dab_scheduler::DAB_TUNE_BUSY_MAX_RETRIES);
      } else {
        ++_commandErrors;
        diagnostic("[DAB/TUNE] busy retries exhausted index=%u; request failed",
                   _dabTuneTarget);
        finishOperation(false);
      }
      return;
    }
    if (_state == State::DabAudioCommand &&
        result == si468x::Result::DeviceError && deviceReason == 0x03U) {
      ++_diagDabAudioNotAvailableCount;
      _dabAudioInfoValid = false;
      _state = State::Ready;
      const uint32_t now = millis();
      if (_dabAudioInfoRetryCount <
          dab_scheduler::DAB_AUDIO_INFO_FAST_RETRIES) {
        ++_dabAudioInfoRetryCount;
        const uint32_t delayMs = dab_scheduler::audioInfoRetryDelayMs(
            _dabAudioInfoRetryCount);
        _dabAudioInfoNotBeforeMs = now + delayMs;
        _dabAudioRefreshPending = true;
        diagnostic("[DAB/AUDIO] NOT_AVAILABLE retry=%u next=%lums",
                   _dabAudioInfoRetryCount,
                   static_cast<unsigned long>(delayMs));
      } else {
        _dabAudioInfoRetryCount = 0;
        _dabAudioRefreshPending = false;
        _dabAudioInfoNotBeforeMs =
            now + dab_scheduler::DAB_AUDIO_INFO_SLOW_RETRY_MS;
        _dabAudioNextDueMs = _dabAudioInfoNotBeforeMs;
        diagnostic("[DAB/AUDIO] NOT_AVAILABLE deferred to normal interval");
      }
      return;
    }
    if (_state == State::DabStopDataServiceCommand) {
      ++_commandErrors;
      const bool alreadyStopped = result == si468x::Result::DeviceError &&
                                  deviceReason == 0x03U;
      if (alreadyStopped) {
        diagnostic("[DAB/SWITCH] data STOP already inactive; continuing");
        _activeDataServiceValid = false;
        _dataServiceStopPending = false;
        _dabSwitch.stopRetries = 0U;
        if (_serviceTransitionPending && _activeServiceValid) {
          startDabStopCommand();
        } else if (_serviceTransitionPending) {
          _serviceTransitionPending = false;
          _state = State::DabServiceSettle;
          _stateDeadlineMs = millis() + dab_switch::STOP_SETTLE_MS;
        } else {
          _state = State::Ready;
        }
      } else if (_dabSwitch.backoff(
                     dab_switch::Resume::StopData,
                     _dabSwitch.stopRetries,
                     dab_switch::MAX_STOP_RETRIES, millis())) {
        _state = State::DabStopRetry;
        diagnostic("[DAB/SWITCH] data STOP retry=%u/%u result=%s",
                   _dabSwitch.stopRetries, dab_switch::MAX_STOP_RETRIES,
                   resultName(result));
      } else {
        diagnostic("[DAB/SWITCH] data STOP retries exhausted; new stream not started");
        finishOperation(false);
      }
      return;
    }
    if (_state == State::DabStopServiceCommand) {
      ++_commandErrors;
      const bool alreadyStopped = result == si468x::Result::DeviceError &&
                                  deviceReason == 0x03U;
      if (alreadyStopped) {
        diagnostic("[DAB/SWITCH] audio STOP already inactive; continuing");
        _activeServiceValid = false;
        resetDabAudioInfo();
        _dabSwitch.stopRetries = 0U;
        _serviceTransitionPending = false;
        _state = State::DabServiceSettle;
        _stateDeadlineMs = millis() + dab_switch::STOP_SETTLE_MS;
      } else if (_dabSwitch.backoff(
                     dab_switch::Resume::StopAudio,
                     _dabSwitch.stopRetries,
                     dab_switch::MAX_STOP_RETRIES, millis())) {
        _state = State::DabStopRetry;
        diagnostic("[DAB/SWITCH] audio STOP retry=%u/%u result=%s",
                   _dabSwitch.stopRetries, dab_switch::MAX_STOP_RETRIES,
                   resultName(result));
      } else {
        diagnostic("[DAB/SWITCH] audio STOP retries exhausted; new stream not started");
        finishOperation(false);
      }
      return;
    }
    if (_state == State::DabDataServiceCommand) {
      ++_commandErrors;
      _state = State::Ready;
      if (dab_scheduler::scheduleRetry(
              _dataServiceRetryCount, _dataServiceRetryNotBeforeMs,
              millis(), dab_switch::MAX_DATA_RETRIES,
              dab_switch::RETRY_BACKOFF_MS)) {
        _dataServicePending = true;
        diagnostic("[RADIO][WARN] data service start retry=%u: %s",
                   _dataServiceRetryCount, resultName(result));
      } else {
        _dataServicePending = false;
        diagnostic("[RADIO][WARN] data service abandoned after bounded retries");
      }
      return;
    }
    // Right after DAB STC the service database can need a few more tens of
    // milliseconds. This is a recoverable receiver state, not a UI error.
    if (_state == State::DabServiceCommand && _serviceStartRetries < 8U &&
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
    if (backgroundCommand) {
      ++_commandErrors;
      if (_state == State::FmRdsCommand) {
        _rdsPending = false;
        if (_fmRdsConsecutiveErrors < 5U) ++_fmRdsConsecutiveErrors;
        const uint32_t backoff = 250U << (_fmRdsConsecutiveErrors - 1U);
        _fmRdsNextDueMs = millis() + (backoff < 5000U ? backoff : 5000U);
      }
      if (dabMetadataCommand) {
        // A receiver can lose the ensemble between 1 Hz DIGRAD samples. Any
        // NOT_AVAILABLE-style metadata failure invalidates the cached lock and
        // forces DIGRAD to re-confirm it before another metadata command runs.
        valid = false;
        _dabSignalRefreshPending = true;
      }
      diagnostic("[RADIO][WARN] background %s: %s device=0x%02X",
                 stateName(), resultName(result), _radio.lastDeviceError());
      _state = State::Ready;
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

    case State::IdentifyPart: {
      ChipRevision = _workspace[4];
      RomID = _workspace[5];
      PartNo = si468x::readLe16(_workspace + 8);
      const uint8_t zero = 0;
      startRaw(si468x::Command::GET_SYS_STATE, &zero, 1,
               State::IdentifySystem, RADIO_COMMAND_TIMEOUT_US, 6);
      break;
    }

    case State::IdentifySystem: {
      const uint8_t reported = _workspace[4];
      const uint8_t expected = _band == 0
          ? static_cast<uint8_t>(si468x::Image::DAB)
          : static_cast<uint8_t>(si468x::Image::FMHD);

      // Do not enter READY unless the requested application image is really
      // active.  image=0 means bootloader/unknown here; accepting it previously
      // produced a false READY state followed by endless DAB_DIGRAD_STATUS
      // ERR_CMD replies and no INTB activity.
      if (reported != expected) {
        diagnostic("[RADIO][ERROR] wrong image=%u expected=%u",
                   reported, expected);
        fail(si468x::Result::MalformedReply, "GET_SYS_STATE image");
        break;
      }

      const uint8_t zero = 0;
      startRaw(si468x::Command::GET_FUNC_INFO, &zero, 1,
               State::IdentifyFunction, RADIO_COMMAND_TIMEOUT_US, 12);
      break;
    }

    case State::IdentifyFunction: {
      VerMajor = _workspace[4];
      VerMinor = _workspace[5];
      VerBuild = _workspace[6];
      if (_band == 0) {
        uint8_t args[3] = {1, 0, 0};
        si468x::writeLe16(args + 1, 0xB400);
        startRaw(si468x::Command::GET_PROPERTY, args, sizeof(args),
                 State::VerifySlideshowRead, RADIO_COMMAND_TIMEOUT_US, 6);
      } else {
        finishIdentification();
      }
      break;
    }

    case State::VerifySlideshowRead: {
      const uint16_t expected = dabXpadValue(_slideshowEnabled);
      const uint16_t actual = si468x::readLe16(_workspace + 4);
      diagnostic("[SLS] DAB_XPAD_ENABLE requested=0x%04X readback=0x%04X (%s)",
                 expected, actual,
                 _slideshowEnabled ? "DLS+MOT" : "DLS only");
      if (actual != expected) {
        ++_commandErrors;
        diagnostic("[SLS][WARN] DAB_XPAD_ENABLE mismatch");
      }
      if (_operation == RadioOperation::BandBoot) finishIdentification();
      else _state = State::Ready;
      break;
    }

    case State::FmTuneCommand:
      _state = State::FmTuneWaitStc;
      break;

    case State::FmSeekCommand:
      _state = State::FmSeekWaitStc;
      break;

    case State::DabTuneCommand:
      _dabTuneBusyRetries = 0;
      _dabTuneRetryNotBeforeMs = 0;
      _state = State::DabTuneWaitStc;
      break;

    case State::DabStopDataServiceCommand:
      diagnostic("[RADIO] DAB data service stopped SID=0x%08lX CID=0x%08lX",
                 static_cast<unsigned long>(_activeDataServiceId),
                 static_cast<unsigned long>(_activeDataComponentId));
      _activeDataServiceValid = false;
      _dataServiceStopPending = false;
      _dabSwitch.stopRetries = 0U;
      if (_serviceTransitionPending && _activeServiceValid) {
        startDabStopCommand();
      } else if (_serviceTransitionPending) {
        _serviceTransitionPending = false;
        _state = State::DabServiceSettle;
        _stateDeadlineMs = millis() + dab_switch::STOP_SETTLE_MS;
      } else {
        _state = State::Ready;
      }
      break;

    case State::DabStopServiceCommand:
      diagnostic("[RADIO] DAB service stopped SID=0x%08lX CID=0x%08lX",
                 static_cast<unsigned long>(_activeServiceId),
                 static_cast<unsigned long>(_activeComponentId));
      _activeServiceValid = false;
      resetDabAudioInfo();
      _dabSwitch.stopRetries = 0U;
      _serviceTransitionPending = false;
      _state = State::DabServiceSettle;
      _stateDeadlineMs = millis() + dab_switch::STOP_SETTLE_MS;
      break;

    case State::DabServiceCommand:
      freq_index = _dabTuneTarget;
      _activeServiceId = _serviceId;
      _activeComponentId = _componentId;
      _activeServiceValid = true;
      _serviceTransitionPending = false;
      {
        // START completion only confirms command acceptance, not that the
        // audio decoder and all metadata are immediately available. Restart
        // the documented 2/5/8/15/30 s phases from this point.
        const bool serviceListPending = _dabServiceListRefreshPending;
        resetPeriodicDeadlines(millis());
        resetDabAudioInfo(millis() +
                          dab_scheduler::DAB_AUDIO_INFO_INITIAL_DELAY_MS);
        _dabAudioRefreshPending = true;
        _dabServiceListRefreshPending = serviceListPending;
        _dataServiceRetryNotBeforeMs =
            millis() + dab_switch::AUDIO_TO_DATA_SETTLE_MS;
        _dabSwitch.state = dab_switch::State::AudioSettle;
      }
      finishOperation(true);
      break;

    case State::DabDataServiceCommand:
      _activeDataServiceId = _dataServiceId;
      _activeDataComponentId = _dataComponentId;
      _activeDataServiceValid = true;
      _dabSwitch.slsContextValid = true;
      _dabSwitch.state = dab_switch::State::Ready;
      _dataServicePending = false;
      dab_scheduler::resetRetry(_dataServiceRetryCount,
                                _dataServiceRetryNotBeforeMs);
      diagnostic("[RADIO] DAB data service started SID=0x%08lX CID=0x%08lX",
                 static_cast<unsigned long>(_activeDataServiceId),
                 static_cast<unsigned long>(_activeDataComponentId));
      _state = State::Ready;
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

    case State::AudioConfigCommand:
      error = 0;
      _state = State::Ready;
      break;

    case State::SlideshowProperty:
      {
        uint8_t args[3] = {1, 0, 0};
        si468x::writeLe16(args + 1, 0xB400);
        startRaw(si468x::Command::GET_PROPERTY, args, sizeof(args),
                 State::VerifySlideshowRead, RADIO_COMMAND_TIMEOUT_US, 6);
      }
      break;

    case State::FmRsqCommand:
      processFmStatusReply(_statusAcknowledgesStc);
      if (_statusAcknowledgesStc && _operation != RadioOperation::None)
        finishOperation(error == 0);
      else
        _state = State::Ready;
      break;

    case State::FmAcfCommand: {
      si468x::FmAcfStatus value;
      if (si468x::Si468x::parseFmAcfStatus(_workspace, 10, value) ==
          si468x::Result::Ok) {
        fmPilot = value.pilot;
        fmStereoBlend = value.stereoBlendPercent;
      } else {
        ++_commandErrors;
      }
      _state = State::Ready;
      break;
    }

    case State::FmRdsCommand:
      processRdsReply();
      _fmRdsConsecutiveErrors = 0;
      _state = State::Ready;
      break;

    case State::DabSignalCommand:
      processDabStatusReply(_statusAcknowledgesStc);
      if (_statusAcknowledgesStc && _operation != RadioOperation::None) {
        if (error != 0) finishOperation(false);
        else if (_stateAfterTune == State::DabServiceCommand)
          startDabServiceCommand();
        else
          finishOperation(true);
      } else {
        _state = State::Ready;
      }
      break;

    case State::DabEventCommand:
      processDabEventReply();
      _state = State::Ready;
      break;

    case State::DabServiceListCommand:
      processServiceListReply();
      _state = State::Ready;
      break;

    case State::DabEnsembleCommand: {
      si468x::DabEnsembleInfo value;
      if (si468x::Si468x::parseDabEnsembleInfo(_workspace, 26, value) ==
          si468x::Result::Ok) {
        EnsembleID = value.ensembleId;
        ECC = value.ecc;
        memcpy(Ensemble, value.label, 16);
        Ensemble[16] = 0;
      } else ++_commandErrors;
      _state = State::Ready;
      break;
    }

    case State::DabTimeCommand: {
      si468x::DabTimeInfo value;
      if (si468x::Si468x::parseDabTime(_workspace, 11, value) ==
          si468x::Result::Ok) {
        Year = value.year;
        Months = value.month;
        Days = value.day;
        Hours = value.hour;
        Minutes = value.minute;
        Seconds = value.second;
        _timeSampleGeneration =
            dab_scheduler::nextGeneration(_timeSampleGeneration);
      } else ++_commandErrors;
      _state = State::Ready;
      break;
    }

    case State::DabAudioCommand: {
      si468x::DabAudioInfo value;
      if (si468x::Si468x::parseDabAudioInfo(_workspace, 10, value) ==
          si468x::Result::Ok) {
        bitrate = value.bitRateKbps;
        samplerate = value.sampleRateHz;
        mode = static_cast<AudioMode>(value.audioMode & 0x03U);
        _dabAudioInfoValid = true;
        _dabAudioInfoRetryCount = 0;
        _dabAudioInfoNotBeforeMs = 0;
        _dabAudioNextDueMs = millis() + dab_scheduler::DAB_AUDIO_INTERVAL_MS;
        diagnostic("[DAB/AUDIO] info ready bitrate=%u samplerate=%u mode=%u",
                   bitrate, samplerate, static_cast<unsigned>(mode));
      } else ++_commandErrors;
      _state = State::Ready;
      break;
    }

    case State::DabServiceInfoCommand: {
      si468x::DabServiceInfo value;
      if (si468x::Si468x::parseDabServiceInfo(_workspace, 26, value) ==
          si468x::Result::Ok) {
        pty = value.pty;
        type = value.dataService ? SERVICE_DATA : SERVICE_AUDIO;
        memcpy(ActiveLabel, value.label, 16);
        ActiveLabel[16] = 0;
        ActiveCharset = value.charset == 0x04U ? 0x06U : value.charset;
        diagnostic("[RADIO] active label charset=%u head=%02X %02X %02X %02X",
                   static_cast<unsigned>(ActiveCharset),
                   static_cast<uint8_t>(ActiveLabel[0]),
                   static_cast<uint8_t>(ActiveLabel[1]),
                   static_cast<uint8_t>(ActiveLabel[2]),
                   static_cast<uint8_t>(ActiveLabel[3]));
      } else ++_commandErrors;
      _state = State::Ready;
      break;
    }

    case State::DabSubchannelCommand: {
      si468x::DabSubchannelInfo value;
      if (si468x::Si468x::parseDabSubchannelInfo(_workspace, 12, value) ==
          si468x::Result::Ok) {
        dabplus = value.serviceMode == 4;
        bitrate = value.bitRateKbps;
      } else ++_commandErrors;
      _state = State::Ready;
      break;
    }

    case State::DsrvCommand:
      processDsrvReply();
      _state = State::Ready;
      break;

    default:
      break;
  }
}

void DAB::startDabServiceCommand() {
  uint8_t args[11] = {0};
  si468x::writeLe32(args + 3, _serviceId);
  si468x::writeLe32(args + 7, _componentId);
  _dabSwitch.bindCommand();
  _dabSwitch.state = dab_switch::State::WaitAudioStart;
  startRaw(si468x::Command::START_DIGITAL_SERVICE, args, sizeof(args),
           State::DabServiceCommand);
}

void DAB::startDabDataServiceCommand() {
  uint8_t args[11] = {0};  // AN649: SERTYPE is zero for DAB.
  si468x::writeLe32(args + 3, _dataServiceId);
  si468x::writeLe32(args + 7, _dataComponentId);
  _dataServicePending = false;
  _dabSwitch.bindCommand();
  _dabSwitch.state = dab_switch::State::WaitDataStart;
  startRaw(si468x::Command::START_DIGITAL_SERVICE, args, sizeof(args),
           State::DabDataServiceCommand);
}

void DAB::startDabStopDataCommand() {
  uint8_t args[11] = {0};
  si468x::writeLe32(args + 3, _activeDataServiceId);
  si468x::writeLe32(args + 7, _activeDataComponentId);
  _dabSwitch.bindCommand();
  _dabSwitch.state = dab_switch::State::WaitDataStop;
  startRaw(si468x::Command::STOP_DIGITAL_SERVICE, args, sizeof(args),
           State::DabStopDataServiceCommand);
}

void DAB::startDabStopCommand() {
  uint8_t args[11] = {0};  // AN649: SERTYPE is zero for DAB.
  si468x::writeLe32(args + 3, _activeServiceId);
  si468x::writeLe32(args + 7, _activeComponentId);
  _dabSwitch.bindCommand();
  _dabSwitch.state = dab_switch::State::WaitAudioStop;
  startRaw(si468x::Command::STOP_DIGITAL_SERVICE, args, sizeof(args),
           State::DabStopServiceCommand);
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
  startIdentification();
}

void DAB::startIdentification() {
  const uint8_t zero = 0;
  startRaw(si468x::Command::GET_PART_INFO, &zero, 1,
           State::IdentifyPart, RADIO_COMMAND_TIMEOUT_US, 23);
}

void DAB::finishIdentification() {
  error = 0;
  _state = State::Ready;
  resetPeriodicDeadlines(millis());
  _bandReadyPending = true;
  _completedOperation = RadioOperation::BandBoot;
  _completedSuccess = true;
  _operationResultPending = true;
  _operation = RadioOperation::None;
  diagnostic("[RADIO] ready: part=Si%u rev=%u ROM=%u image=%s FW=%u.%u.%u IRQ=%lu",
             PartNo, ChipRevision, RomID, _band == 0 ? "DAB" : "FM",
             VerMajor, VerMinor, VerBuild,
             static_cast<unsigned long>(_irqCounter));
}

void DAB::resetPeriodicDeadlines(uint32_t now) {
  _fmRsqNextDueMs = now + dab_scheduler::FM_RSQ_INTERVAL_MS;
  _fmAcfNextDueMs = now + 250U;
  _fmRdsNextDueMs = now + dab_scheduler::FM_RDS_POLL_INTERVAL_MS;
  _dabSignalNextDueMs = now + dab_scheduler::DAB_SIGNAL_INTERVAL_MS;
  _dabAudioNextDueMs = now + dab_scheduler::DAB_AUDIO_INITIAL_PHASE_MS;
  _dabServiceInfoNextDueMs =
      now + dab_scheduler::DAB_SERVICE_INITIAL_PHASE_MS;
  _dabEnsembleNextDueMs = now + dab_scheduler::DAB_ENSEMBLE_INITIAL_PHASE_MS;
  _dabTimeNextDueMs = now + dab_scheduler::DAB_TIME_INITIAL_PHASE_MS;
  _dabSubchannelNextDueMs =
      now + dab_scheduler::DAB_SUBCHANNEL_INITIAL_PHASE_MS;
  _dabSignalRefreshPending = false;
  _dabServiceListRefreshPending = false;
  _dabEnsembleRefreshPending = false;
  _dabTimeRefreshPending = false;
  _dabAudioRefreshPending = false;
  _dabServiceInfoRefreshPending = false;
  _dabSubchannelRefreshPending = false;
  _lastLowPriorityCommandMs = now - dab_scheduler::DAB_LOW_PRIORITY_GAP_MS;
}

void DAB::resetDabAudioInfo(uint32_t notBeforeMs) {
  _dabAudioInfoValid = false;
  _dabAudioInfoRetryCount = 0;
  _dabAudioInfoNotBeforeMs = notBeforeMs;
  _dabAudioRefreshPending = false;
  bitrate = 0;
  samplerate = 0;
  mode = DUAL;
}

void DAB::updatePeriodicRequests(uint32_t now) {
  if (_band != 0) return;
  if (dab_scheduler::takePeriodicDeadline(
          now, _dabSignalNextDueMs,
          dab_scheduler::DAB_SIGNAL_INTERVAL_MS))
    _dabSignalRefreshPending = true;
  if (dab_scheduler::takePeriodicDeadline(
          now, _dabAudioNextDueMs,
          dab_scheduler::DAB_AUDIO_INTERVAL_MS))
    _dabAudioRefreshPending = true;
  if (dab_scheduler::takePeriodicDeadline(
          now, _dabServiceInfoNextDueMs,
          dab_scheduler::DAB_SERVICE_INTERVAL_MS))
    _dabServiceInfoRefreshPending = true;
  if (dab_scheduler::takePeriodicDeadline(
          now, _dabEnsembleNextDueMs,
          dab_scheduler::DAB_ENSEMBLE_INTERVAL_MS))
    _dabEnsembleRefreshPending = true;
  if (dab_scheduler::takePeriodicDeadline(
          now, _dabTimeNextDueMs,
          dab_scheduler::DAB_TIME_INTERVAL_MS))
    _dabTimeRefreshPending = true;
  if (dab_scheduler::takePeriodicDeadline(
          now, _dabSubchannelNextDueMs,
          dab_scheduler::DAB_SUBCHANNEL_INTERVAL_MS))
    _dabSubchannelRefreshPending = true;
}

void DAB::scheduleReadyWork(uint32_t now) {
  if (_volumePending) {
    serviceVolume();
    return;
  }
  if (_slideshowPropertyPending && _band == 0) {
    _slideshowPropertyPending = false;
    startProperty(0xB400, dabXpadValue(_slideshowEnabled),
                  State::SlideshowProperty);
    return;
  }
  if (_band == 0 && _dataServiceStopPending && _activeDataServiceValid) {
    startDabStopDataCommand();
    return;
  }
  if (_band == 0 && valid && _dataServicePending && _activeServiceValid &&
      !_activeDataServiceValid &&
      dab_scheduler::retryReady(now, _dataServiceRetryNotBeforeMs)) {
    startDabDataServiceCommand();
    return;
  }

  const uint8_t zero = 0;
  if (_band == 1) {
    // FM_RDS_STATUS is undefined before the first acquired FM station on some
    // Si4684 firmware builds and returns device error 0x12. Keep RSQ sampling
    // active, but do not touch the RDS FIFO until RSQ reports a valid tune.
    const bool rdsDue = valid && (_rdsPending ||
        dab_scheduler::deadlineReached(now, _fmRdsNextDueMs));
    const bool rsqDue =
        dab_scheduler::deadlineReached(now, _fmRsqNextDueMs);
    const bool acfDue =
        dab_scheduler::deadlineReached(now, _fmAcfNextDueMs);
    const dab_scheduler::FmWork work = dab_scheduler::chooseFmWork(
        false, false, rdsDue, rsqDue, acfDue);
    if (work == dab_scheduler::FmWork::Rds) {
      const uint8_t args[1] = {1};
      _rdsPending = false;
      dab_scheduler::takePeriodicDeadline(
          now, _fmRdsNextDueMs, dab_scheduler::FM_RDS_POLL_INTERVAL_MS);
      startRaw(si468x::Command::FM_RDS_STATUS, args, sizeof(args),
               State::FmRdsCommand, 100000UL, 20);
      return;
    }
    if (work == dab_scheduler::FmWork::IdleRsq) {
      dab_scheduler::takePeriodicDeadline(
          now, _fmRsqNextDueMs, dab_scheduler::FM_RSQ_INTERVAL_MS);
      startFmStatus(false);
      return;
    }
    if (work == dab_scheduler::FmWork::Acf) {
      const uint8_t args[1] = {1};
      dab_scheduler::takePeriodicDeadline(
          now, _fmAcfNextDueMs, dab_scheduler::FM_ACF_INTERVAL_MS);
      startRaw(si468x::Command::FM_ACF_STATUS, args, sizeof(args),
               State::FmAcfCommand, 100000UL, 10);
    }
    return;
  }

  const bool lowPriorityReady =
      static_cast<uint32_t>(now - _lastLowPriorityCommandMs) >=
      dab_scheduler::DAB_LOW_PRIORITY_GAP_MS;
  const bool lowPriorityPending = lowPriorityReady &&
      (_deviceEventPending ||
       (valid && (_dabServiceListRefreshPending ||
                  _dabEnsembleRefreshPending || _dabTimeRefreshPending)) ||
       (valid && _activeServiceValid &&
        ((_dabAudioRefreshPending &&
          dab_scheduler::retryReady(now, _dabAudioInfoNotBeforeMs)) ||
         _dabServiceInfoRefreshPending ||
         _dabSubchannelRefreshPending)));
  const dab_scheduler::BackgroundWork work =
      dab_scheduler::chooseBackgroundWork(
          _dsrvPending, _dsrvBurstCount, _dabSignalRefreshPending,
          lowPriorityPending);

  if (work == dab_scheduler::BackgroundWork::Dsrv) {
    if (_dsrvBurstCount >= dab_scheduler::DAB_MAX_DSRV_BURST)
      _dsrvBurstCount = 0;
    const uint8_t args[1] = {1};
    _dsrvPending = false;
    if (startRaw(si468x::Command::GET_DIGITAL_SERVICE_DATA,
                 args, sizeof(args), State::DsrvCommand,
                 100000UL, 24))
      ++_dsrvBurstCount;
    return;
  }
  if (work == dab_scheduler::BackgroundWork::Signal) {
    _dabSignalRefreshPending = false;
    _dsrvBurstCount = 0;
    startDabStatus(false);
    return;
  }
  if (work != dab_scheduler::BackgroundWork::LowPriority) return;
  _dsrvBurstCount = 0;
  _lastLowPriorityCommandMs = now;

  if (_deviceEventPending) {
    const uint8_t args[1] = {1};
    _deviceEventPending = false;
    startRaw(si468x::Command::DAB_GET_EVENT_STATUS, args, sizeof(args),
             State::DabEventCommand, 100000UL, 8);
  } else if (valid && _dabServiceListRefreshPending) {
    _dabServiceListRefreshPending = false;
    startRaw(si468x::Command::GET_DIGITAL_SERVICE_LIST, &zero, 1,
             State::DabServiceListCommand, 1000000UL, 8);
  } else if (valid && _dabEnsembleRefreshPending) {
    _dabEnsembleRefreshPending = false;
    startRaw(si468x::Command::DAB_GET_ENSEMBLE_INFO, &zero, 1,
             State::DabEnsembleCommand, 100000UL, 26);
  } else if (valid && _dabTimeRefreshPending) {
    _dabTimeRefreshPending = false;
    startRaw(si468x::Command::DAB_GET_TIME, &zero, 1,
             State::DabTimeCommand, 100000UL, 11);
  } else if (valid && _activeServiceValid &&
             _dabAudioRefreshPending && _serviceId != 0 &&
             dab_scheduler::retryReady(now, _dabAudioInfoNotBeforeMs)) {
    _dabAudioRefreshPending = false;
    startRaw(si468x::Command::DAB_GET_AUDIO_INFO, &zero, 1,
             State::DabAudioCommand, 100000UL, 10);
  } else if (valid && _activeServiceValid &&
             _dabServiceInfoRefreshPending && _serviceId != 0) {
    uint8_t args[7] = {0};
    si468x::writeLe32(args + 3, _serviceId);
    _dabServiceInfoRefreshPending = false;
    startRaw(si468x::Command::DAB_GET_SERVICE_INFO, args, sizeof(args),
             State::DabServiceInfoCommand, 100000UL, 26);
  } else if (valid && _activeServiceValid &&
             _dabSubchannelRefreshPending && _serviceId != 0) {
    uint8_t args[11] = {0};
    si468x::writeLe32(args + 3, _serviceId);
    si468x::writeLe32(args + 7, _componentId);
    _dabSubchannelRefreshPending = false;
    startRaw(si468x::Command::DAB_GET_SUBCHAN_INFO, args, sizeof(args),
             State::DabSubchannelCommand, 100000UL, 12);
  }
}

void DAB::processRdsReply() {
  si468x::FmRdsGroup group;
  if (si468x::Si468x::parseFmRdsStatus(_workspace, 20, group) !=
      si468x::Result::Ok) {
    ++_commandErrors;
    return;
  }
  rdsSync = group.sync;
  if (group.piValid) {
    if (pi != group.pi) {
      pi = group.pi;
      _fmAfList.clear(pi);
      _fmClockValidator.reset();
      fmClockValid = false;
    } else {
      pi = group.pi;
    }
  }
  if (group.tpPtyValid) {
    pty = group.pty;
    tp = group.tp;
  }
  const bool changed = decodeRdsGroup(group) != 0;
  _rdsPending = group.fifoUsed > 1;
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

void DAB::processDsrvReply() {
  si468x::DsrvHeader header;
  si468x::Result result =
      si468x::Si468x::parseDsrvHeader(_workspace, 24, header);
  if (result != si468x::Result::Ok) {
    ++_commandErrors;
    diagnostic("[RADIO][WARN] malformed DSRV header");
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
    ++_commandErrors;
    diagnostic("[RADIO][WARN] DSRV payload read: %s", resultName(result));
    _dsrvPending = false;
    return;
  }
  const si468x::Result parseResult =
      si468x::Si468x::parseDsrvHeader(_workspace, replyLength, header);
  if (parseResult != si468x::Result::Ok) {
    ++_commandErrors;
    diagnostic("[RADIO][WARN] malformed DSRV payload header");
    _dsrvPending = false;
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

  const bool audioContext = _activeServiceValid &&
      header.serviceId == _activeServiceId &&
      (header.componentId == 0U || header.componentId == _activeComponentId);
  const bool dataContext = _activeDataServiceValid &&
      header.serviceId == _activeDataServiceId &&
      (header.componentId == 0U || header.componentId == _activeDataComponentId);
  if (header.isPad() && !audioContext && !dataContext) {
    diagnostic("[SLS][WARN] packet outside active service context dropped SID=%08lX CID=%08lX",
               static_cast<unsigned long>(header.serviceId),
               static_cast<unsigned long>(header.componentId));
    _dsrvPending = header.buffersRemaining > 0;
    return;
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
    _slideshowPublishedPending = false;
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

// Insert a unique MOT segment into the single arena while keeping already
// received bytes packed in SegmentNumber order.  This is the same principle as
// the current SI4684-FMDAB-Receiver collector: out-of-order arrival only shifts
// the collected tail and does not require a second image-sized buffer.
bool DAB::storeSlideshowSegment(uint16_t segment, const uint8_t* data,
                                        uint16_t dataLength) {
  if (segment >= SLS_MAX_SEGMENTS || data == nullptr || dataLength == 0U ||
      dataLength > SLS_MAX_SEGMENT_BYTES ||
      !mot_assembly::canAppend(_slideshowReceivedBytes, dataLength,
                               DAB_SLS_ARENA_BYTES)) {
    return false;
  }

  uint32_t insertOffset = 0U;
  for (uint16_t i = 0; i < segment; ++i) {
    insertOffset += _slideshowSegmentLengths[i];
  }
  if (insertOffset > _slideshowReceivedBytes) return false;

  const uint32_t tailLength = _slideshowReceivedBytes - insertOffset;
  memmove(_slideshowArena + insertOffset + dataLength,
          _slideshowArena + insertOffset, tailLength);
  memcpy(_slideshowArena + insertOffset, data, dataLength);
  _slideshowSegmentLengths[segment] = dataLength;
  return true;
}

void DAB::processMotPacket(const si468x::DsrvHeader& header,
                           const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length == 0 || header.dscType != 60) return;
  // DSCTy 60 packets use 0x73 for MOT headers and 0x74 for object-body
  // chunks. This is the format visible in the Si4684 DSRV payload (for
  // example "74 10 00 ..."), not the lower-level X-PAD subfield layout.
  const uint8_t packetType = payload[0];
  if (packetType != 0x73U && packetType != 0x74U) return;
  ++_motPackets;
  if (!_slideshowEnabled || length < 11U) return;

  const uint16_t segmentField =
      (static_cast<uint16_t>(payload[2]) << 8) | payload[3];
  const bool last = (segmentField & 0x8000U) != 0U;
  const uint16_t segment = segmentField & 0x7FFFU;
  if (!mot_assembly::segmentInRange(segment, SLS_MAX_SEGMENTS)) {
    diagnostic("[SLS][WARN] invalid segment index=%u max=%u", segment,
               SLS_MAX_SEGMENTS - 1U);
    return;
  }
  const uint32_t objectId = (static_cast<uint32_t>(payload[4]) << 16) |
                            (static_cast<uint32_t>(payload[5]) << 8) |
                            payload[6];
  const uint16_t dataLength =
      (static_cast<uint16_t>(payload[7] & 0x1FU) << 8) | payload[8];
  const uint32_t requiredLength = 9UL + dataLength + 2UL;
  if (requiredLength > length) {
    ++_commandErrors;
    diagnostic("[SLS][WARN] short MOT packet type=%02X bytes=%u expected=%lu",
               packetType, length, static_cast<unsigned long>(requiredLength));
    return;
  }
  const uint8_t* data = payload + 9;
  if (packetType == 0x73U) {
    uint32_t bodySize = 0;
    uint16_t headerSize = 0;
    const bool validHeader = segment == 0U &&
        mot_assembly::decodeHeaderCore(data, dataLength, bodySize, headerSize) &&
        bodySize <= DAB_SLS_ARENA_BYTES;
    diagnostic("[SLS] header object=%06lX segment=%u%s bytes=%u body=%lu header=%u",
               static_cast<unsigned long>(objectId), segment,
               last ? " LAST" : "", dataLength,
               static_cast<unsigned long>(validHeader ? bodySize : 0U),
               validHeader ? headerSize : 0U);
    if (!validHeader || !mot_assembly::canAcceptTransport(
            _slideshowPublishedPending, _slideshowCompletedTransportValid,
            _slideshowCompletedTransportId, objectId)) return;

    if (!_slideshowCollecting || _slideshowTransportId != objectId ||
        (_slideshowExpectedLength != 0U &&
         _slideshowExpectedLength != bodySize)) {
      // Header metadata alone must not evict the cached image. The arena is
      // reused only when the first valid body segment is accepted below.
      resetSlideshowAssembler(false);
      _slideshowCollecting = true;
      _slideshowTransportId = objectId;
      _slideshowServiceId = header.serviceId;
      _slideshowComponentId = header.componentId;
    }
    _slideshowExpectedLength = bodySize;
    _slideshowLastActivityMs = millis();
    return;
  }

  if (dataLength == 0U || dataLength > SLS_MAX_SEGMENT_BYTES) {
    diagnostic("[SLS][WARN] segment dropped: index=%u length=%u max=%u",
               segment, dataLength, SLS_MAX_SEGMENT_BYTES);
    return;
  }
  if (!mot_assembly::canAcceptTransport(
          _slideshowPublishedPending, _slideshowCompletedTransportValid,
          _slideshowCompletedTransportId, objectId)) return;

  // Keep the last complete image available for reopening until a valid first
  // body segment of a different MOT object actually arrives. From this point
  // the single arena belongs to the new collector and the cached image can no
  // longer be decoded safely.
  if (_slideshowAvailable) {
    if (!mot_assembly::startsNewBodyOverCache(true, segment)) return;
    _slideshowAvailable = false;
    _slideshowUpdate = false;
    _slideshowImageLength = 0U;
  }

  if (!_slideshowCollecting) {
    resetSlideshowAssembler(true);
    _slideshowCollecting = true;
    _slideshowTransportId = objectId;
    _slideshowServiceId = header.serviceId;
    _slideshowComponentId = header.componentId;
  } else if (_slideshowTransportId != objectId) {
    if (!mot_assembly::objectPacketBelongs(
            true, _slideshowTransportId, objectId, segment)) return;
    diagnostic("[SLS] switching object %06lX -> %06lX at segment 0",
               static_cast<unsigned long>(_slideshowTransportId),
               static_cast<unsigned long>(objectId));
    resetSlideshowAssembler(true);
    _slideshowCollecting = true;
    _slideshowTransportId = objectId;
    _slideshowServiceId = header.serviceId;
    _slideshowComponentId = header.componentId;
  }

  const uint8_t mask = static_cast<uint8_t>(1U << (segment & 7U));
  uint8_t& bitmapByte = _slideshowSegmentBitmap[segment >> 3];
  if ((bitmapByte & mask) != 0U) {
    // Some broadcasters omit LAST. Repeated segment zero marks the next
    // carousel cycle, so a contiguous object collected before it is complete.
    const bool precedingSegmentsComplete =
        _slideshowHighestSegment > 0U &&
        allSlideshowSegmentsReceived(_slideshowHighestSegment + 1U);
    if (mot_assembly::repeatedZeroCompletes(
            true, segment, _slideshowTotalSegments,
            _slideshowHighestSegment, precedingSegmentsComplete)) {
      _slideshowTotalSegments = _slideshowHighestSegment + 1U;
      finishSlideshowObject();
    }
    return;
  }
  if (_slideshowExpectedLength != 0U &&
      (_slideshowReceivedBytes > _slideshowExpectedLength ||
       static_cast<uint32_t>(dataLength) >
           _slideshowExpectedLength - _slideshowReceivedBytes)) {
    diagnostic("[SLS][WARN] body exceeds header size object=%06lX received=%lu add=%u expected=%lu",
               static_cast<unsigned long>(objectId),
               static_cast<unsigned long>(_slideshowReceivedBytes), dataLength,
               static_cast<unsigned long>(_slideshowExpectedLength));
    resetSlideshowAssembler(true);
    return;
  }
  if (!storeSlideshowSegment(segment, data, dataLength)) {
    diagnostic("[SLS][WARN] cannot store object=%06lX seg=%u len=%u",
               static_cast<unsigned long>(objectId), segment, dataLength);
    resetSlideshowAssembler(true);
    return;
  }

  bitmapByte |= mask;
  _slideshowReceivedBytes += dataLength;
  if (segment > _slideshowHighestSegment) _slideshowHighestSegment = segment;
  const uint16_t totalFromLast =
      mot_assembly::totalSegmentsFromLast(segment, last);
  if (totalFromLast != 0U) _slideshowTotalSegments = totalFromLast;
  _slideshowLastActivityMs = millis();

  if (dab_scheduler::shouldLogMotSegment(
          false, segment, last, millis(), _lastMotSegmentLogMs)) {
    diagnostic("[SLS] body object=%06lX seg=%u%s len=%u total=%lu",
               static_cast<unsigned long>(objectId), segment,
               last ? " LAST" : "", dataLength,
               static_cast<unsigned long>(_slideshowReceivedBytes));
  }

  if (_slideshowTotalSegments != 0U &&
      allSlideshowSegmentsReceived(_slideshowTotalSegments) &&
      (_slideshowExpectedLength == 0U ||
       _slideshowReceivedBytes == _slideshowExpectedLength)) {
    finishSlideshowObject();
  }
}

void DAB::finishSlideshowObject() {
  const uint16_t count = _slideshowTotalSegments != 0
                             ? _slideshowTotalSegments
                             : _slideshowHighestSegment + 1;
  if (!allSlideshowSegmentsReceived(count)) return;
  if (_slideshowExpectedLength != 0U &&
      _slideshowReceivedBytes != _slideshowExpectedLength) return;

  // Segments are already packed in ascending SegmentNumber order.  Validate
  // the metadata and publish the contiguous arena directly; no second assembly
  // pass and no overlapping fixed-slot memmoves are needed.
  uint32_t outputLength = 0;
  for (uint16_t segment = 0; segment < count; ++segment) {
    const uint16_t length = _slideshowSegmentLengths[segment];
    if (length == 0 || outputLength + length > DAB_SLS_ARENA_BYTES) {
      diagnostic("[SLS][WARN] assembly bounds failure at segment %u", segment);
      resetSlideshowAssembler(true);
      return;
    }
    outputLength += length;
  }
  if (outputLength != _slideshowReceivedBytes) {
    diagnostic("[SLS][WARN] packed size mismatch assembled=%lu received=%lu",
               static_cast<unsigned long>(outputLength),
               static_cast<unsigned long>(_slideshowReceivedBytes));
    resetSlideshowAssembler(true);
    return;
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
  const bool changed = outputLength != _slideshowLastImageLength ||
                       hash != _slideshowImageHash;
  _slideshowLastImageLength = outputLength;
  // Even when a broadcaster rotates the identical artwork under a new
  // Transport ID, the freshly assembled bytes remain a valid cache for a
  // later SELECT reopen. Only the UI update notification is deduplicated.
  _slideshowImageLength = outputLength;
  _slideshowImageHash = hash;
  _slideshowCompletedTransportId = _slideshowTransportId;
  _slideshowCompletedTransportValid = true;
  _slideshowAvailable = true;
  _slideshowUpdate = changed;
  _slideshowPublishedPending = changed;
  diagnostic("[SLS] %s ready: %lu bytes, %u segments, imageOffset=%lu free=%u largest=%u%s",
             jpeg ? "JPEG" : "PNG", static_cast<unsigned long>(outputLength),
             count, static_cast<unsigned long>(imageOffset), ESP.getFreeHeap(),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             changed ? "" : " (unchanged, not republished)");
  resetSlideshowAssembler(false);
}

void DAB::processDabEventReply() {
  si468x::DabEventStatus event;
  if (si468x::Si468x::parseDabEventStatus(_workspace, 8, event) !=
      si468x::Result::Ok) {
    ++_commandErrors;
    return;
  }
  if (event.serviceListAvailable || event.serviceListInterrupt) {
    _dabServiceListRefreshPending = true;
    _dabEnsembleRefreshPending = true;
  }
  if (event.reconfiguration || event.reconfigurationWarning) {
    diagnostic("[RADIO] DAB reconfiguration event: warning=%u active=%u",
               event.reconfigurationWarning ? 1U : 0U,
               event.reconfiguration ? 1U : 0U);
  }
}

void DAB::processServiceListReply() {
  // Use the common typed streaming parser. It separates the 16-bit COMP_ID
  // from the two following information/flag bytes in each component entry.
  const uint16_t listSize = si468x::readLe16(_workspace + 4);
  const uint32_t fullLength = static_cast<uint32_t>(listSize) + 6U;
  if (fullLength < 9U || fullLength > sizeof(_workspace)) {
    ++_commandErrors;
    diagnostic("[RADIO][WARN] invalid service-list length=%lu",
               static_cast<unsigned long>(fullLength));
    return;
  }
  const si468x::Result readResult = _radio.readCurrentReply(
      _workspace, static_cast<uint16_t>(fullLength));
  if (readResult != si468x::Result::Ok) {
    ++_commandErrors;
    diagnostic("[RADIO][WARN] service-list payload: %s", resultName(readResult));
    return;
  }

  const uint8_t previousServiceCount = numberofservices;
  DABService previousServices[DAB_MAX_SERVICES];
  if (previousServiceCount != 0U) {
    memcpy(previousServices, service,
           static_cast<size_t>(previousServiceCount) * sizeof(DABService));
  }

  si468x::DabServiceListSink sink;
  sink.context = this;
  sink.onHeader = serviceListHeader;
  sink.onService = serviceListService;
  sink.onComponent = serviceListComponent;
  si468x::DabServiceListParser parser;
  parser.setSink(sink);

  // _workspace[0..3] are STATUS. RESP4/list-size begins at byte 4.
  const si468x::Result parseResult = parser.feed(
      _workspace + 4, static_cast<size_t>(fullLength - 4U));
  if (parseResult != si468x::Result::Ok || !parser.complete()) {
    ++_commandErrors;
    numberofservices = 0;
    diagnostic("[RADIO][WARN] malformed DAB service list");
    return;
  }

  _dabServiceListGeneration =
      dab_scheduler::nextGeneration(_dabServiceListGeneration);

  // Some ensembles briefly publish zero-filled labels while rebuilding the
  // same service list. Keep a previously received real label for the same
  // SID/COMP_ID instead of regressing it to the scan fallback name.
  for (uint8_t current = 0; current < numberofservices; ++current) {
    if (dabLabelHasContent(service[current].Label)) continue;
    for (uint8_t previous = 0; previous < previousServiceCount; ++previous) {
      if (previousServices[previous].ServiceID != service[current].ServiceID ||
          previousServices[previous].CompID != service[current].CompID ||
          !dabLabelHasContent(previousServices[previous].Label)) continue;
      memcpy(service[current].Label, previousServices[previous].Label, 17U);
      service[current].Charset = previousServices[previous].Charset;
      diagnostic("[RADIO] retained label SID=%08lX CID=%08lX from previous generation",
                 static_cast<unsigned long>(service[current].ServiceID),
                 static_cast<unsigned long>(service[current].CompID));
      break;
    }
  }

  // If this list already contains the currently requested service, publish
  // its label immediately. The periodic DAB_GET_SERVICE_INFO refresh will
  // later confirm/update the same live metadata.
  for (uint8_t i = 0; i < numberofservices; ++i) {
    if (_serviceId != 0U && service[i].ServiceID == _serviceId) {
      memcpy(ActiveLabel, service[i].Label, 16);
      ActiveLabel[16] = 0;
      ActiveCharset = service[i].Charset;
      break;
    }
  }

  if (_matchingDataCandidateCount == 1U ||
      (_matchingDataCandidateCount == 0U &&
       _fallbackDataCandidateCount == 1U)) {
    _dataServiceId = _matchingDataCandidateCount == 1U
                         ? _matchingDataServiceId : _fallbackDataServiceId;
    _dataComponentId = _matchingDataCandidateCount == 1U
                           ? _matchingDataComponentId
                           : _fallbackDataComponentId;
    if (_slideshowEnabled && _activeServiceValid && !_activeDataServiceValid) {
      _dataServicePending = true;
      _dabSwitch.requestDataEvaluation();
    }
  } else {
    _dataServiceId = 0;
    _dataComponentId = 0;
    _dataServicePending = false;
    _dabSwitch.slsContextValid =
        dab_switch::audioPadSlsContextValid(_activeServiceValid, false);
    if (_dabSwitch.slsContextValid) _dabSwitch.state = dab_switch::State::Ready;
  }

  diagnostic("[RADIO] service list: generation=%lu services=%u",
             static_cast<unsigned long>(_dabServiceListGeneration),
             numberofservices);
  for (uint8_t i = 0; i < numberofservices; ++i) {
    if (i < 4 || service[i].Type != SERVICE_AUDIO) {
      diagnostic("[RADIO] svc[%u] SID=%08lX CID=%08lX type=%u charset=%u label=%02X %02X %02X %02X",
                 i, static_cast<unsigned long>(service[i].ServiceID),
                 static_cast<unsigned long>(service[i].CompID),
                 static_cast<unsigned>(service[i].Type),
                 static_cast<unsigned>(service[i].Charset),
                 static_cast<uint8_t>(service[i].Label[0]),
                 static_cast<uint8_t>(service[i].Label[1]),
                 static_cast<uint8_t>(service[i].Label[2]),
                 static_cast<uint8_t>(service[i].Label[3]));
    }
  }
}

void DAB::startFmStatus(bool acknowledgeStc) {
  const uint8_t args[1] = {
      static_cast<uint8_t>(0x08U | (acknowledgeStc ? 0x01U : 0U))};
  _statusAcknowledgesStc = acknowledgeStc;
  startRaw(si468x::Command::FM_RSQ_STATUS, args, sizeof(args),
           State::FmRsqCommand, 100000UL, 18);
}

void DAB::processFmStatusReply(bool acknowledgeStc) {
  si468x::FmRsqStatus value;
  if (si468x::Si468x::parseFmRsqStatus(_workspace, 18, value) !=
      si468x::Result::Ok) {
    ++_commandErrors;
    return;
  }
  freq = value.frequency10kHz;
  signalstrength = value.rssi;
  snr = value.snr;
  valid = value.valid;
  _signalSampleGeneration =
      dab_scheduler::nextGeneration(_signalSampleGeneration);
  const uint32_t now = millis();
  if (acknowledgeStc) _fmAcfNextDueMs = now + 250U;
  error = 0;
  if (_lastStatusDiagnosticMs == 0 ||
      now - _lastStatusDiagnosticMs >= 5000) {
    diagnostic("[RADIO] FM status: %u.%02u MHz valid=%u RSSI=%d SNR=%d pilot=%u blend=%u%%",
               freq / 100, freq % 100, valid ? 1U : 0U, signalstrength, snr,
               fmPilot ? 1U : 0U, fmStereoBlend);
    _lastStatusDiagnosticMs = now;
  }
}

void DAB::startDabStatus(bool acknowledgeStc) {
  const uint8_t args[1] = {
      static_cast<uint8_t>(0x08U | (acknowledgeStc ? 0x01U : 0U))};
  _statusAcknowledgesStc = acknowledgeStc;
  startRaw(si468x::Command::DAB_DIGRAD_STATUS, args, sizeof(args),
           State::DabSignalCommand, 100000UL, 23);
}

void DAB::processDabStatusReply(bool acknowledgeStc) {
  si468x::DabDigradStatus grade;
  if (si468x::Si468x::parseDabDigradStatus(_workspace, 23, grade) !=
      si468x::Result::Ok) {
    ++_commandErrors;
    return;
  }
  const bool wasValid = valid;
  freq_index = grade.tuneIndex;
  signalstrength = grade.rssi;
  snr = static_cast<int8_t>(grade.cnr);
  quality = grade.ficQuality;
  valid = grade.valid && grade.acquired;
  _signalSampleGeneration =
      dab_scheduler::nextGeneration(_signalSampleGeneration);
  error = 0;
  if (valid && (acknowledgeStc || !wasValid)) {
    _dabServiceListRefreshPending = true;
  }
  const uint32_t now = millis();
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
      if (rdsGroup == RDS_GROUP_0A && group.blockUsable(2)) {
        if (_fmAfList.pi != pi) _fmAfList.clear(pi);
        const bool firstAdded = _fmAfList.addCode(blockC >> 8);
        const bool secondAdded = _fmAfList.addCode(blockC & 0xFFU);
        if (firstAdded || secondAdded) {
          diagnostic("[RDS] AF list PI=%04X entries=%u expected=%u", pi,
                     _fmAfList.count, _fmAfList.expected);
          changed |= 0x0020;
        }
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
      fm_features::ClockTime decoded;
      fm_features::ClockTime confirmed;
      if (!fm_features::decodeClockTime(blockB, blockC, blockD, decoded)) break;
      const fm_features::ClockSampleResult result =
          _fmClockValidator.ingest(decoded, pi, millis(), confirmed);
      if (result == fm_features::ClockSampleResult::Confirmed) {
        fmLocalOffsetHalfHours = confirmed.localOffsetHalfHours;
        fm_features::applyLocalOffset(confirmed);
        Year = confirmed.year;
        Months = confirmed.month;
        Days = confirmed.day;
        Hours = confirmed.hour;
        Minutes = confirmed.minute;
        Seconds = 0;
        fmClockValid = true;
        _fmClockGeneration =
            dab_scheduler::nextGeneration(_fmClockGeneration);
        diagnostic("[RDS] CT confirmed %04u-%02u-%02u %02u:%02u offset=%+d/2h",
                   Year, Months, Days, Hours, Minutes,
                   fmLocalOffsetHalfHours);
        changed |= 0x0010;
      }
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
