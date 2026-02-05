#pragma once

#include "wled.h"
#include <type_traits> // Needed for std::is_same

// Configuration defines with defaults
#ifndef HOUR_EFFECT_ENABLED_USERMOD
  #define HOUR_EFFECT_ENABLED_USERMOD false
#endif

#ifndef HOUR_EFFECT_ENABLED_3D_BLINK
  #define HOUR_EFFECT_ENABLED_3D_BLINK false
#endif

#ifndef HOUR_EFFECT_ENABLED_HOUR_EFFECT
  #define HOUR_EFFECT_ENABLED_HOUR_EFFECT false
#endif

#ifndef HOUR_EFFECT_ENABLE_NIGHT_MODE_POWER_OFF
  #define HOUR_EFFECT_ENABLE_NIGHT_MODE_POWER_OFF false
#endif

#ifndef HOUR_EFFECT_ENABLED_NIGHT_MODE_POWER_ON
  #define HOUR_EFFECT_ENABLED_NIGHT_MODE_POWER_ON false
#endif

#ifndef HOUR_EFFECT_ENABLE_PRESENCE_DURING_NIGHT_MODE
  #define HOUR_EFFECT_ENABLE_PRESENCE_DURING_NIGHT_MODE false
#endif

#ifndef HOUR_EFFECT_NIGHT_MODE_ON
  #define HOUR_EFFECT_NIGHT_MODE_ON 1
#endif

#ifndef HOUR_EFFECT_NIGHT_MODE_OFF
  #define HOUR_EFFECT_NIGHT_MODE_OFF 8
#endif

#ifndef HOUR_EFFECT_MQTT_PRESENCE
  #define HOUR_EFFECT_MQTT_PRESENCE ""
#endif

#ifndef HOUR_EFFECT_MQTT_PRESENCE_BLOCKER
  #define HOUR_EFFECT_MQTT_PRESENCE_BLOCKER ""
#endif

#ifndef HOUR_EFFECT_MQTT_LUX
  #define HOUR_EFFECT_MQTT_LUX ""
#endif

#ifndef HOUR_EFFECT_LUX_THRESHOLD
  #define HOUR_EFFECT_LUX_THRESHOLD 30
#endif

#ifndef HOUR_EFFECT_TRIGGER_MODE
  #define HOUR_EFFECT_TRIGGER_MODE 1  // 0=none, 1=presence only, 2=lux only (no turn off), 3=lux only (with turn off), 4=lux and presence (off on presence), 5=lux and presence (off on both)
#endif

#ifndef HOUR_EFFECT_INPUT_PIN
  #define HOUR_EFFECT_INPUT_PIN -1
#endif

#ifndef HOUR_EFFECT_INPUT_ACTIVE_LOW
  #define HOUR_EFFECT_INPUT_ACTIVE_LOW false
#endif

#ifndef HOUR_EFFECT_MQTT_LAMPS
  #define HOUR_EFFECT_MQTT_LAMPS ""
#endif

// debounce window to ignore rapid duplicate 3D printer triggers (ms)
#ifndef MIN_3D_TRIGGER_MS
  #define MIN_3D_TRIGGER_MS 3000UL
#endif

// logging macro:
#define _logUsermodHourEffect(fmt, ...) \
  DEBUG_PRINTF("[HourEffect] " fmt "\n", ##__VA_ARGS__)

#ifdef USERMOD_NIXIECLOCK
  #include "nixieclock_v2.h"
#endif

#ifdef USERMOD_SSDR
  #include "seven_segment_display_reloaded_v2.h"
#endif

#define ENABLE_LOOP_STATE_DUMP 0  // Set to 1 to enable periodic state dumps

///////////////////////////////////////////////////////////////////////////////
// Template helper to backup and restore a segment's settings
///////////////////////////////////////////////////////////////////////////////
// Helper struct to backup/restore global color state
struct GlobalColorBackup {
  uint8_t backupPri[4];
  uint8_t backupSec[4];

  void backup();
  void restore();
};

template<typename T>
struct BackupHelper {
  bool hasData = false;
  uint8_t mode       = 0;
  uint8_t speed      = 0;
  uint8_t intensity  = 0;
  uint8_t palette    = 0;
  uint32_t color[3]  = {0,0,0}; // packed per-segment colors
  uint8_t segmentOn  = 0;       // use uint8_t to match seg.getOption(...)
  uint8_t opacity    = 255;
  bool    isSelected = false;
  
  // Add missing effect parameters
  uint8_t custom1 = 0;
  uint8_t custom2 = 0;
  uint8_t custom3 = 0;

  // Backup the current state of a segment.
  void backup(T &seg);

  // Restore the saved state to a segment.
  void restore(T &seg);
};

struct MqttSensor {
  String id;
  String topic;
  String path;           // JSON path (empty = direct value)
  String onValues;       // Comma-separated list of values that mean "true"
  bool currentState;     // Current evaluated state
  
  MqttSensor() : currentState(false) {}
};

struct SensorConfig {
  std::vector<MqttSensor> sensors;
  String logicTrue;      // Logic expression for true state
  String logicFalse;     // Logic expression for false state (optional)
  
  void clear() {
    sensors.clear();
    logicTrue = "";
    logicFalse = "";
  }
};

///////////////////////////////////////////////////////////////////////////////
// UsermodHourEffect: A usermod to trigger hourly effects, handle MQTT commands,
// backup/restore LED states, and interact with other usermods.
///////////////////////////////////////////////////////////////////////////////
class UsermodHourEffect : public Usermod {
private:
  ////////////////// Timing Variables //////////////////
  unsigned long lastTime = 0;        // Used to check hourly trigger (in millis)
  unsigned long lastInputCheck = 0;  // Last time we checked the input pin

  bool isTopicMatch(const char* topic, const char* suffix) const;
  
  ////////////////// Hour-trigger bug fix //////////////////
  int lastEffectTriggerHour = -1;    // which hour we last fired on
  int lastNightModeOnHour = -1;      // which hour we last turned NightMode ON
  int lastNightModeOffHour = -1;     // which hour we last turned NightMode OFF
  
  ////////////////// Enable Flags //////////////////
  bool enabledUsermod       = HOUR_EFFECT_ENABLED_USERMOD; // Global on/off for this usermod
  bool enabled3DBlink       = HOUR_EFFECT_ENABLED_3D_BLINK; // Enable 3D printer finished blink effect
  bool enabledHourEffect    = HOUR_EFFECT_ENABLED_HOUR_EFFECT; // Enable hourly effect
  bool enableNightModePowerOff  = HOUR_EFFECT_ENABLE_NIGHT_MODE_POWER_OFF; // If true, power off LEDs when NightMode starts
  bool enabledNightModePowerOn  = HOUR_EFFECT_ENABLED_NIGHT_MODE_POWER_ON; // If true, power on LEDs when NightMode ends
  bool enablePresenceDuringNightMode = HOUR_EFFECT_ENABLE_PRESENCE_DURING_NIGHT_MODE; // Enable presence detection during NightMode

  ////////////////// Mode & Presence Flags //////////////////
  bool NightMode = false;
  bool NotHome   = false;
  int NightModeOn  = HOUR_EFFECT_NIGHT_MODE_ON; // Hour to turn NightMode ON (default: 01:00)
  int NightModeOff = HOUR_EFFECT_NIGHT_MODE_OFF; // Hour to turn NightMode OFF (default: 08:00)

  // MQTT-related configuration and state
  String MqttPresence = HOUR_EFFECT_MQTT_PRESENCE;
  bool presenceValue = false;
  String MqttPresenceBlocker = HOUR_EFFECT_MQTT_PRESENCE_BLOCKER;
  bool PresenceBlocker = false;
  std::map<String, bool> topicStates; // Store state of each topic
  
  std::vector<String> parseTopicList(const String& topicList);

  ////////////////// Lux Sensor Configuration //////////////////
  String MqttLux = HOUR_EFFECT_MQTT_LUX;
  float luxValue = 0;
  int luxThreshold = HOUR_EFFECT_LUX_THRESHOLD;
  uint8_t triggerMode = HOUR_EFFECT_TRIGGER_MODE; // 0=none, 1=presence only, 2=lux only (no turn off), 3=lux only (with turn off), 4=lux+presence (off on presence), 5=lux+presence (off on both)

  ////////////////// Input Pin Configuration //////////////////
  int8_t inputPin[1] = { HOUR_EFFECT_INPUT_PIN };
  int8_t oldInputPin = -1;
  bool inputActiveLow = HOUR_EFFECT_INPUT_ACTIVE_LOW;
  bool inputPinAllocated = false;
  bool lastInputState = false;
  const unsigned long INPUT_CHECK_INTERVAL = 100; // Check input every 100ms

  ////////////////// MQTT Lamp Control //////////////////
  String MqttLamps = HOUR_EFFECT_MQTT_LAMPS; // Comma-separated list of MQTT paths

  ////////////////// Backup/Restore Control //////////////////
  bool BlockTriggers = false;  // Block presence/lux triggers during effects
  unsigned long last3DTriggerTime = 0;
  int LastBriValue   = 300;     // Backup for brightness

  // Nixie clock LED control flag (if NixieClock usermod is defined)
  bool NixieLed = true;

  // Flags to track backup/restore state
  bool hasBackupRun  = false;
  
  // Track if state change is from this usermod
  bool internalStateChange = false;
  uint8_t lastBrightness = 0;
  
  // Enhanced MQTT sensor configurations
  SensorConfig presenceConfig;
  SensorConfig luxConfig;
  SensorConfig blockerConfig;
  
  // Simple mode flags (backwards compatibility)
  bool useSimplePresence = true;
  bool useSimpleLux = true;
  bool useSimpleBlocker = true;

  ////////////////// Static Config Strings //////////////////
  static const char _name[];
  static const char _enabledUsermod[];
  static const char _enabled3DBlink[];
  static const char _enabledHourEffect[];
  static const char _enableNightModePowerOff[];
  static const char _enabledNightModePowerOn[];
  static const char _enablePresenceDuringNightMode[];
  static const char _NightModeOn[];
  static const char _NightModeOff[];
  static const char _MqttPresence[];
  static const char _MqttPresenceBlocker[];
  static const char _MqttLux[];
  static const char _luxThreshold[];
  static const char _triggerMode[];
  static const char _inputPin[];
  static const char _inputActiveLow[];
  static const char _MqttLamps[];
  static const char _MqttPresenceAdvanced[];
  static const char _MqttLuxAdvanced[];
  static const char _MqttBlockerAdvanced[];

  ////////////////// Pointers to Other Usermods //////////////////
  #ifdef USERMOD_NIXIECLOCK
    UsermodNixieClock *nixie = nullptr;
  #else
    #define nixie nullptr
  #endif

  #ifdef USERMOD_SSDR
    UsermodSSDR *ssdr = nullptr;
  #else
    #define ssdr nullptr
  #endif

  ////////////////// Helper Methods //////////////////
  void handlePresenceChange();
  void handleLuxChange();
  void handlePresenceLuxTrigger();
  void validateHourValues();
  void allocateInputPin();
  void deallocateInputPin();
  void checkInputPin();
  void controlMqttLamps(bool state);
  
  void handleSimpleMultiTopicPresence(const String& topics, const char* topic, const char* payload);
  void handleSimpleMultiTopicLux(const String& topics, const char* topic, const char* payload);
  
  // Helper methods for sensor management
  bool parseJsonConfig(const String& json, SensorConfig& config);
  bool evaluateLogicExpression(const String& expression, const SensorConfig& config);
  bool evaluatePresenceState(const SensorConfig& config);
  bool evaluateSensorState(const MqttSensor& sensor, const String& payload);
  void updateSensorState(SensorConfig& config, const char* topic, const char* payload);
  float extractLuxValue(const MqttSensor& sensor, const String& payload);

public:
  ////////////////// Effect Parameters //////////////////
  bool ResetEffect = false;   // When true, a reset of the effect is scheduled
  uint8_t GotEffect = 0;      // Effect mode received via MQTT
  uint8_t effectSpeed = 128;   // Effect speed value
  uint8_t effectIntensity = 128; // Effect intensity value
  uint8_t pal = 0;            // Palette index
  
  unsigned long resetScheduledTime = 0;
  const unsigned long RESET_DELAY_MS = 10000UL;

  ////////////////// Backup Storage for Segments //////////////////
  BackupHelper<Segment> mainSegmentBackup; // Backup for main segment
  BackupHelper<Segment> *segmentBackup = nullptr; // Dynamic array for other segments
  GlobalColorBackup globalColorBackup;

  // Usermod interface
  void onMqttConnect(bool sessionPresent) override;
  void onStateChange(uint8_t mode) override;
  bool isTimeMatch(int targetHour);
  void NightNothomeTrigger();
  void executeNightModeLogic();
  void publishMessage(const char* topicSuffix, const String& value);
  void setup() override;
  void loop() override;
  void applyEffectSettings(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t effectMode);
  bool onMqttMessage(char* topic, char* payload) override;
  void _SetLedsOn(bool state);
  void _BackupCurrentLedState();
  void _RestoreLedState();
  void addToConfig(JsonObject& root) override;
  bool readFromConfig(JsonObject& root) override;
  void appendConfigData() override;
  ~UsermodHourEffect();
  uint16_t getId() override;
};