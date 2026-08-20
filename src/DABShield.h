/*
 * Si4684 application adapter for FMDABRadio.
 *
 * The public data fields intentionally preserve the compact interface used by
 * the original sketch. Radio commands are delegated to the platform-neutral
 * Si468x core while this adapter owns ESP32 SPI/GPIO, INTB scheduling, boot
 * sequencing and the small amount of application-level RDS/DLS state.
 */
#ifndef FMDABRADIO_DABSHIELD_H
#define FMDABRADIO_DABSHIELD_H

#include <Arduino.h>
#include "Si468x.h"

constexpr uint8_t DAB_MAX_SERVICES = 32;
constexpr uint16_t DAB_MAX_SERVICEDATA_LEN = 129;
constexpr uint32_t DAB_SLS_ARENA_BYTES = 48UL * 1024UL;

extern const uint32_t dab_freq[];
constexpr uint8_t DAB_FREQS = 38;

enum ServiceType : uint8_t {
  SERVICE_NONE,
  SERVICE_AUDIO,
  SERVICE_DATA
};

enum AudioMode : uint8_t {
  DUAL = 0,
  MONO,
  STEREO,
  JOINT_STEREO
};

struct DABService {
  uint8_t Freq;
  uint32_t ServiceID;
  uint32_t CompID;
  char Label[17];
  uint8_t Charset;
  ServiceType Type;
};

struct DABTime {
  uint16_t Year;
  uint8_t Months;
  uint8_t Days;
  uint8_t Hours;
  uint8_t Minutes;
  uint8_t Seconds;
};

enum class RadioOperation : uint8_t {
  None,
  BandBoot,
  FmTune,
  FmSeek,
  DabTune,
  DabService
};

class DAB {
 public:
  DAB();

  void configurePins(uint8_t chipSelect, uint8_t interrupt, uint8_t reset, uint8_t powerEnable);
  void configureAudioPins(uint8_t gain0, uint8_t gain1);
  void setDiagnostics(Stream* stream);
  void setCallback(void (*serviceDataCallback)(void));
  bool setSlideshowEnabled(bool enabled);

  // Cooperative API used by the interactive application path.
  bool beginAsync(uint8_t band);
  bool requestFmTune(uint16_t frequency10kHz);
  bool requestFmSeek(bool up, bool wrap);
  bool requestDabTune(uint8_t frequencyIndex);
  bool requestDabService(uint8_t frequencyIndex, uint32_t serviceId, uint32_t componentId);
  void requestVolume(uint8_t volume);
  void task();

  bool ready() const;
  bool busy() const;
  uint8_t band() const;
  bool takeBandReady();
  bool takeOperationResult(RadioOperation& operation, bool& success);
  const char* stateName() const;

  bool status();
  bool status(uint32_t serviceId, uint32_t componentId);
  bool time(DABTime* value);
  void mono(bool enable);
  void mute(bool left, bool right);
  uint32_t freq_khz(uint8_t index) const;

  uint16_t ECC;
  uint32_t EnsembleID;
  char Ensemble[17];
  char ServiceData[DAB_MAX_SERVICEDATA_LEN];
  uint16_t ServiceDataLength;
  uint8_t ServiceDataCharset;

  uint8_t error;
  uint8_t freq_index;
  DABService service[DAB_MAX_SERVICES];
  uint8_t numberofservices;
  uint8_t ChipRevision;
  uint8_t RomID;
  uint16_t PartNo;
  uint8_t VerMajor;
  uint8_t VerMinor;
  uint8_t VerBuild;

  uint16_t freq;
  int8_t signalstrength;
  int8_t snr;
  uint8_t quality;
  bool valid;

  uint16_t bitrate;
  uint16_t samplerate;
  ServiceType type;
  AudioMode mode;
  bool dabplus;
  bool fmPilot;
  uint8_t fmStereoBlend;
  uint8_t pty;

  uint16_t pi;
  char ps[9];
  bool rdsSync;
  bool tp;
  bool ta;

  uint16_t Year;
  uint8_t Months;
  uint8_t Days;
  uint8_t Hours;
  uint8_t Minutes;

  uint32_t irqCount() const;
  uint32_t commandErrorCount() const;
  uint32_t dsrvOverflowCount() const;
  uint32_t dsrvPacketCount() const;
  uint32_t dlsPacketCount() const;
  uint32_t motPacketCount() const;
  bool slideshowEnabled() const;
  bool slideshowAvailable() const;
  bool takeSlideshowUpdate();
  void discardSlideshow();
  const uint8_t* slideshowData() const;
  uint32_t slideshowLength() const;
  bool urgentDataPending() const;

 private:
  enum class State : uint8_t {
    Off,
    ResetHold,
    ResetRelease,
    PowerUp,
    LoadInitPatch,
    LoadPatch,
    PatchDelay,
    LoadInitFlash,
    ConfigureFlash,
    FlashLoad,
    Boot,
    ConfigureBand,
    Identify,
    Ready,
    FmTuneCommand,
    FmTuneWaitStc,
    FmSeekCommand,
    FmSeekWaitStc,
    DabTuneCommand,
    DabTuneWaitStc,
    DabServiceCommand,
    DabServiceRetry,
    VolumeCommand,
    SlideshowProperty,
    Failed
  };

  static bool writeCommand(void* context, uint8_t command, const uint8_t* args, uint16_t length);
  static bool readReply(void* context, uint8_t* destination, uint16_t length);
  static uint32_t timeUs(void* context);
  static void idle(void* context);
  static void setReset(void* context, bool asserted);
  static void setPower(void* context, bool enabled);
  static void statusCallback(void* context, const si468x::Status& status);
  static size_t patchReader(void* context, uint32_t offset, uint8_t* destination, size_t length);
  static void serviceListHeader(void* context, const si468x::DabServiceListHeader& header);
  static void serviceListService(void* context, const si468x::DabServiceEntry& entry);
  static void serviceListComponent(void* context, const si468x::DabComponentEntry& entry);

  bool startCore(si468x::Result result, State state);
  bool startRaw(si468x::Command command, const uint8_t* args, uint16_t length, State state,
                uint32_t timeoutUs = 1000000UL);
  bool startProperty(uint16_t property, uint16_t value, State state);
  void advanceState();
  void commandCompleted();
  void finishOperation(bool success);
  void fail(si468x::Result result, const char* where);
  void configureNextProperty();
  void identify();
  void processPendingEvents();
  void processRds();
  void processDsrv();
  void verifySlideshowProperty();
  void resetDlsAssembler();
  void publishDlsSegments(uint8_t firstSegment, uint8_t lastSegment,
                          bool complete);
  bool allocateSlideshowArena();
  void releaseSlideshowArena();
  void resetSlideshowAssembler(bool clearImage);
  void processMotPacket(const si468x::DsrvHeader& header,
                        const uint8_t* payload, uint16_t length);
  bool allSlideshowSegmentsReceived(uint16_t count) const;
  void finishSlideshowObject();
  void processDabEvent();
  bool refreshDabServiceList();
  void updateFmStatus(bool acknowledgeStc);
  void updateDabStatus(bool acknowledgeStc);
  void startDabServiceCommand();
  void serviceVolume();
  void setTpaGain(int8_t gainDb);
  uint16_t decodeRdsGroup(const si468x::FmRdsGroup& group);
  void diagnostic(const char* format, ...);
  static const char* resultName(si468x::Result result);

  si468x::HostInterface _host;
  si468x::Si468x _radio;
  // Complete GET_DIGITAL_SERVICE_DATA reply: 24-byte transport header plus
  // the largest Si468x DSRV payload used by DLS/MOT.
  uint8_t _workspace[568];
  Stream* _diagnostics;
  void (*_callback)(void);

  uint8_t _chipSelectPin;
  uint8_t _interruptPin;
  uint8_t _resetPin;
  uint8_t _powerEnablePin;
  uint8_t _gain0Pin;
  uint8_t _gain1Pin;
  uint8_t _band;
  State _state;
  State _stateAfterTune;
  RadioOperation _operation;
  RadioOperation _completedOperation;
  bool _completedSuccess;
  bool _operationResultPending;
  bool _bandReadyPending;
  bool _commandPending;
  bool _stcPending;
  bool _rdsPending;
  bool _dsrvPending;
  bool _deviceEventPending;
  bool _volumePending;
  bool _slideshowPropertyPending;
  bool _gainAfterVolume;
  uint8_t _desiredUserVolume;
  uint8_t _volumeCommandUser;
  uint8_t _volumeCommandSi;
  uint8_t _currentSiVolume;
  int8_t _volumeCommandGainDb;
  int8_t _currentGainDb;
  uint8_t _propertyIndex;
  uint32_t _stateDeadlineMs;
  uint32_t _operationDeadlineMs;
  uint32_t _patchOffset;
  uint16_t _fmTuneTarget;
  uint8_t _dabTuneTarget;
  uint32_t _serviceId;
  uint32_t _componentId;
  uint8_t _serviceStartRetries;

  uint8_t _rdsText[2][64];
  uint8_t _rdsProgramService[2][8];
  uint8_t _rdsPsSeenMask;
  uint8_t _rdsPsStableMask;
  uint8_t _lastTextAbState;
  uint8_t _dlsSegments[8][16];
  uint8_t _dlsSegmentLengths[8];
  uint8_t _dlsReceivedMask;
  uint8_t _dlsLastSegment;
  uint8_t _dlsToggle;
  uint8_t _dlsCharset;
  uint8_t _currentServiceIndex;
  bool _currentServiceStored;

  static constexpr uint8_t SLS_MAX_SEGMENTS = 96;
  static constexpr uint16_t SLS_SEGMENT_SLOT_BYTES = 512;
  uint8_t* _slideshowArena;
  uint16_t _slideshowSegmentLengths[SLS_MAX_SEGMENTS];
  uint8_t _slideshowSegmentBitmap[(SLS_MAX_SEGMENTS + 7) / 8];
  uint32_t _slideshowTransportId;
  uint16_t _slideshowHighestSegment;
  uint16_t _slideshowTotalSegments;
  uint32_t _slideshowExpectedLength;
  uint32_t _slideshowReceivedBytes;
  uint32_t _slideshowImageLength;
  uint32_t _slideshowLastActivityMs;
  uint32_t _slideshowServiceId;
  uint32_t _slideshowComponentId;
  uint32_t _slideshowImageHash;
  bool _slideshowEnabled;
  bool _slideshowCollecting;
  bool _slideshowAvailable;
  bool _slideshowUpdate;

  uint32_t _irqCounter;
  uint32_t _commandErrors;
  uint32_t _dsrvOverflows;
  uint32_t _dsrvPackets;
  uint32_t _dlsPackets;
  uint32_t _motPackets;
  uint8_t _dsrvDiagnosticDivider;
  uint32_t _lastAudioStatusMs;
  uint32_t _lastMetadataStatusMs;
  uint32_t _lastStatusDiagnosticMs;
};

#endif
