#include "hour_effect_v2.h"
//ScheduledModeManager

///////////////////////////////////////////////////////////////////////////////
// GlobalColorBackup implementation
///////////////////////////////////////////////////////////////////////////////
void GlobalColorBackup::backup() {
  // copy from WLED globals into our backup arrays
  memcpy(backupPri, ::colPri, sizeof(backupPri));
  memcpy(backupSec, ::colSec, sizeof(backupSec));
  _logUsermodHourEffect("[GLOBAL-BACKUP] Pri(%d,%d,%d,%d) Sec(%d,%d,%d,%d)",
                        backupPri[0], backupPri[1], backupPri[2], backupPri[3],
                        backupSec[0], backupSec[1], backupSec[2], backupSec[3]);
}

void GlobalColorBackup::restore() {
  // copy back into WLED globals
  memcpy(::colPri, backupPri, sizeof(backupPri));
  memcpy(::colSec, backupSec, sizeof(backupSec));
  _logUsermodHourEffect("[GLOBAL-RESTORE] Pri(%d,%d,%d,%d) Sec(%d,%d,%d,%d)",
                        ::colPri[0], ::colPri[1], ::colPri[2], ::colPri[3],
                        ::colSec[0], ::colSec[1], ::colSec[2], ::colSec[3]);
}

// Backup the current state of a segment.
template<typename T>
void BackupHelper<T>::backup(T &seg) {
  // store id so we can match segments reliably during restore
  this->mode = seg.mode;
  this->speed = seg.speed;
  this->intensity = seg.intensity;
  this->palette = seg.palette;

  // copy packed per-segment colors (these are uint32_t in WLED)
  this->color[0] = seg.colors[0];
  this->color[1] = seg.colors[1];
  this->color[2] = seg.colors[2];

  // on/off and other single-byte fields - adjust names if necessary
  this->segmentOn = seg.getOption(SEG_OPTION_ON);
  this->opacity   = seg.opacity;     // if present in your WLED version
  this->custom1   = seg.custom1;     // replace with actual custom field names
  this->custom2   = seg.custom2;
  this->custom3   = seg.custom3;

  // For Segment type, also store its "selected" state.
  if (std::is_same<T, Segment>::value) {
    this->isSelected = static_cast<Segment &>(seg).isSelected();
  }

  this->hasData = true;
}

// Restore the saved state to a segment.
template<typename T>
void BackupHelper<T>::restore(T &seg) {
  if (!hasData) return; // nothing to do

  // mode/palette/speed/intensity
  if (seg.mode != this->mode) seg.setMode(this->mode);
  if (seg.palette != this->palette) seg.setPalette(this->palette);
  if (seg.speed != this->speed) seg.speed = this->speed;
  if (seg.intensity != this->intensity) seg.intensity = this->intensity;

  // restore per-segment packed colors
  if (seg.colors[0] != this->color[0]) seg.setColor(0, this->color[0]);
  if (seg.colors[1] != this->color[1]) seg.setColor(1, this->color[1]);
  if (seg.colors[2] != this->color[2]) seg.setColor(2, this->color[2]);

  // on/off, opacity, custom fields (adjust if field names differ)
  seg.setOption(SEG_OPTION_ON, this->segmentOn);
  seg.opacity = this->opacity;
  seg.custom1 = this->custom1; seg.custom2 = this->custom2; seg.custom3 = this->custom3;

  // For Segment type, restore its "selected" state.
  if (std::is_same<T, Segment>::value) {
    static_cast<Segment &>(seg).selected = this->isSelected;
  }

  _logUsermodHourEffect("[RESTORE] Restored segment start=%d: mode=%d, speed=%d, intensity=%d, palette=%d, color0=%u, color1=%u, color2=%u, segmentOn=%d",
                       seg.start, this->mode, this->speed, this->intensity, this->palette,
                       this->color[0], this->color[1], this->color[2], this->segmentOn);
}

///////////////////////////////////////////////////////////////////////////////
// Handle comma-separated presence topics in simple mode
// Logic: ALL topics must be ON for presence=true, ANY OFF means presence=false
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::handleSimpleMultiTopicPresence(const String& topics, const char* topic, const char* payload) {
  _logUsermodHourEffect("[MULTI-PRESENCE] Processing message: topic=%s, payload=%s", topic, payload);
  
  String topicStr = String(topic);
  String payloadStr = String(payload);
  payloadStr.toLowerCase();
  
  // Check if this message is for one of our configured topics
  bool foundMatch = false;
  std::vector<String> topicList = parseTopicList(topics);
  
  for (const auto& configuredTopic : topicList) {
    _logUsermodHourEffect("[MULTI-PRESENCE] Checking configured topic: '%s'", configuredTopic.c_str());
    
    // Check if incoming topic matches this configured topic
    if (topicStr.endsWith(configuredTopic) || topicStr.indexOf(configuredTopic) >= 0) {
      foundMatch = true;
      _logUsermodHourEffect("[MULTI-PRESENCE] Topic match found!");
      
      // Determine ON/OFF state from payload
      bool newState = false;
      
      if (payloadStr.startsWith("{")) {
        // JSON payload
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, payload);
        if (!error && doc.containsKey("presence")) {
          newState = doc["presence"].as<bool>();
          _logUsermodHourEffect("[MULTI-PRESENCE] JSON presence value: %s", newState ? "true" : "false");
        } else {
          _logUsermodHourEffect("[MULTI-PRESENCE] JSON parse error or missing 'presence' key");
          return; // Don't update if we can't parse
        }
      } else {
        // Simple boolean payload
        newState = (payloadStr == "true" || payloadStr == "1" || payloadStr == "on");
      }
      
      // Check if this topic's state actually changed
      auto it = topicStates.find(configuredTopic);
      bool previousState = (it != topicStates.end()) ? it->second : false;
      
      if (previousState == newState) {
        _logUsermodHourEffect("[MULTI-PRESENCE] Topic '%s' state unchanged (%s), ignoring", 
                              configuredTopic.c_str(), newState ? "ON" : "OFF");
        return; // No change, don't trigger
      }
      
      // Update state for this topic
      topicStates[configuredTopic] = newState;
      _logUsermodHourEffect("[MULTI-PRESENCE] Topic '%s' state CHANGED: %s -> %s", 
                            configuredTopic.c_str(), 
                            previousState ? "ON" : "OFF",
                            newState ? "ON" : "OFF");
      break;
    }
  }
  
  if (!foundMatch) {
    _logUsermodHourEffect("[MULTI-PRESENCE] No matching topic found, ignoring message");
    return;
  }
  
  // Evaluate combined state: ALL must be ON
  _logUsermodHourEffect("[MULTI-PRESENCE] Evaluating combined state (ALL must be ON):");
  bool allOn = true;
  for (const auto& configuredTopic : topicList) {
    bool topicState = (topicStates.find(configuredTopic) != topicStates.end() && 
                       topicStates[configuredTopic]);
    _logUsermodHourEffect("[MULTI-PRESENCE]   Topic '%s': %s", configuredTopic.c_str(), topicState ? "ON" : "OFF");
    
    if (!topicState) {
      allOn = false;
      break;
    }
  }
  
  // Update presence value only if combined state changed
  if (allOn != presenceValue) {
    presenceValue = allOn;
    _logUsermodHourEffect("[MULTI-PRESENCE] Combined presence CHANGED: %s -> %s", 
                          !allOn ? "OFF" : "ON", allOn ? "ON" : "OFF");
    
    if (!PresenceBlocker && !BlockTriggers) {
      _logUsermodHourEffect("[MULTI-PRESENCE] Triggering presence/lux handler");
      handlePresenceLuxTrigger();
    } else {
      _logUsermodHourEffect("[MULTI-PRESENCE] Blocked (PresenceBlocker=%d, BlockTriggers=%d)", 
                            PresenceBlocker, BlockTriggers);
    }
  } else {
    _logUsermodHourEffect("[MULTI-PRESENCE] Combined presence unchanged: %s", presenceValue ? "ON" : "OFF");
  }
}

///////////////////////////////////////////////////////////////////////////////
// Handle comma-separated lux topics in simple mode
// Logic: Use FIRST topic that provides valid lux value
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::handleSimpleMultiTopicLux(const String& topics, const char* topic, const char* payload) {
  _logUsermodHourEffect("[MULTI-LUX] Processing message: topic=%s, payload=%s", topic, payload);
  _logUsermodHourEffect("[MULTI-LUX] Configured topics: %s", topics.c_str());
  
  String topicStr = String(topic);
  String payloadStr = String(payload);
  
  // Parse comma-separated topic list
  std::vector<String> topicList = parseTopicList(topics);
  
  for (const auto& configuredTopic : topicList) {
    _logUsermodHourEffect("[MULTI-LUX] Checking configured topic: '%s'", configuredTopic.c_str());
    
    // Check if incoming topic matches this configured topic
    if (topicStr.endsWith(configuredTopic) || topicStr.indexOf(configuredTopic) >= 0) {
      _logUsermodHourEffect("[MULTI-LUX] Topic match found!");
      float newLuxValue;
      
      if (payloadStr.startsWith("{")) {
        // JSON payload
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
          _logUsermodHourEffect("[MULTI-LUX] JSON parsing error: %s", error.c_str());
          return;
        }
        
        if (doc.containsKey("illuminance")) {
          newLuxValue = doc["illuminance"].as<float>();
          _logUsermodHourEffect("[MULTI-LUX] Found 'illuminance' field: %.1f", newLuxValue);
        } else if (doc.containsKey("lux")) {
          newLuxValue = doc["lux"].as<float>();
          _logUsermodHourEffect("[MULTI-LUX] Found 'lux' field: %.1f", newLuxValue);
        } else {
          _logUsermodHourEffect("[MULTI-LUX] No illuminance/lux field found in JSON");
          return;
        }
      } else {
        // Simple numeric payload
        newLuxValue = payloadStr.toFloat();
        _logUsermodHourEffect("[MULTI-LUX] Simple numeric payload: %.1f", newLuxValue);
      }
      
      // Check if lux value actually changed (use threshold of 0.5 to avoid float precision issues)
      if (abs(newLuxValue - luxValue) < 0.5) {
        _logUsermodHourEffect("[MULTI-LUX] Lux value unchanged (%.1f), ignoring", luxValue);
        return;
      }
      
      _logUsermodHourEffect("[MULTI-LUX] Lux value CHANGED: %.1f -> %.1f", luxValue, newLuxValue);
      luxValue = newLuxValue;
      
      if (!PresenceBlocker && !BlockTriggers) {
        _logUsermodHourEffect("[MULTI-LUX] Triggering presence/lux handler");
        handlePresenceLuxTrigger();
      } else {
        _logUsermodHourEffect("[MULTI-LUX] Blocked (PresenceBlocker=%d, BlockTriggers=%d)", 
                              PresenceBlocker, BlockTriggers);
      }
      return; // Process only first matching topic
    }
  }
  
  _logUsermodHourEffect("[MULTI-LUX] No matching topic found, ignoring message");
}

///////////////////////////////////////////////////////////////////////////////
// onStateChange: Called when WLED state changes (GUI, API, etc.)
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::onStateChange(uint8_t mode) {
  if (!enabledUsermod) {
    //_logUsermodHourEffect("[STATE-CHANGE] Usermod disabled, ignoring state change");
    return;
  }
  
  // Skip if this change was triggered by this usermod
  if (internalStateChange) {
    _logUsermodHourEffect("[STATE-CHANGE] Skipping - internal change (mode=%d)", mode);
    return;
  }
  
  _logUsermodHourEffect("[STATE-CHANGE] External state change detected (mode=%d): bri=%d, lastBrightness=%d", mode, bri, lastBrightness);
  
  // Check if brightness changed (LED turned ON or OFF externally)
  if (bri != lastBrightness) {
    bool isOn = (bri > 0);
    bool wasOn = (lastBrightness > 0);
    
    _logUsermodHourEffect("[STATE-CHANGE] Brightness changed: %d -> %d, isOn=%d, wasOn=%d", lastBrightness, bri, isOn, wasOn);
    
    // Only control lamps if the ON/OFF state actually changed
    if (isOn != wasOn) {
      _logUsermodHourEffect("[STATE-CHANGE] ON/OFF state changed, controlling MQTT lamps: %s", isOn ? "ON" : "OFF");
      controlMqttLamps(isOn);
    } else {
      _logUsermodHourEffect("[STATE-CHANGE] ON/OFF state unchanged, no MQTT lamp control needed");
    }
    
    if (bri > 0) {
      lastNonZeroBrightness = bri;
    }
    lastBrightness = bri;
  } else {
    _logUsermodHourEffect("[STATE-CHANGE] Brightness unchanged at %d", bri);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Helper: subscribe to one topic and log it
///////////////////////////////////////////////////////////////////////////////
static void subscribeAndLog(const char* topic) {
#ifndef WLED_DISABLE_MQTT
  if (mqtt) {
    mqtt->subscribe(topic, 2);
    _logUsermodHourEffect("[MQTT-SUB] Subscribed to topic: %s", topic);
  } else {
    _logUsermodHourEffect("[MQTT-SUB] MQTT not available, cannot subscribe to: %s", topic);
  }
#else
  _logUsermodHourEffect("[MQTT-SUB] MQTT disabled in build, cannot subscribe to: %s", topic);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Helper function to publish MQTT messages with timestamp
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::publishMessage(const char* topicSuffix, const String& value) {
#ifndef WLED_DISABLE_MQTT
  if (!mqtt) {
    _logUsermodHourEffect("[MQTT-PUB] MQTT not available, cannot publish %s", topicSuffix);
    return;
  }

  char buffer[128];
  sprintf_P(buffer, PSTR("%s/config/Options/%s"), mqttDeviceTopic, topicSuffix);
  
  _logUsermodHourEffect("[MQTT-PUB] Publishing: topic=%s, value=%s", buffer, value.c_str());
  bool success = mqtt->publish(buffer, 2, true, value.c_str());
  
  if (success) {
    _logUsermodHourEffect("[MQTT-PUB] Publish successful");
  } else {
    _logUsermodHourEffect("[MQTT-PUB] Publish FAILED");
  }
#else
  _logUsermodHourEffect("[MQTT-PUB] MQTT disabled in build, cannot publish %s", topicSuffix);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Helper method to control MQTT lamps
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::controlMqttLamps(bool state) {
#ifndef WLED_DISABLE_MQTT
  if (!mqtt) {
    _logUsermodHourEffect("[MQTT-LAMPS] MQTT not available, cannot control lamps");
    return;
  }
  
  if (MqttLamps.isEmpty()) {
    _logUsermodHourEffect("[MQTT-LAMPS] No lamps configured");
    return;
  }

  _logUsermodHourEffect("[MQTT-LAMPS] Controlling lamps, state=%s, configured lamps: %s", state ? "ON" : "OFF", MqttLamps.c_str());

  // Parse comma-separated list of MQTT paths
  String lampList = MqttLamps;
  lampList.trim();
  
  int lampCount = 0;
  int startIdx = 0;
  int commaIdx = lampList.indexOf(',');
  
  while (commaIdx >= 0 || startIdx < lampList.length()) {
    String lampPath;
    
    if (commaIdx >= 0) {
      lampPath = lampList.substring(startIdx, commaIdx);
      startIdx = commaIdx + 1;
      commaIdx = lampList.indexOf(',', startIdx);
    } else {
      lampPath = lampList.substring(startIdx);
      startIdx = lampList.length();
    }
    
    lampPath.trim();
    
    if (lampPath.length() > 0) {
      lampCount++;
      // Prepare JSON payload
      String payload = state ? "{\"state\":\"ON\"}" : "{\"state\":\"OFF\"}";
      
      _logUsermodHourEffect("[MQTT-LAMPS] Publishing to lamp %d: %s -> %s", lampCount, lampPath.c_str(), state ? "ON" : "OFF");
      
      // Publish to the lamp's set topic
      bool success = mqtt->publish(lampPath.c_str(), 0, false, payload.c_str());
      
      if (success) {
        _logUsermodHourEffect("[MQTT-LAMPS] Lamp %d control successful", lampCount);
      } else {
        _logUsermodHourEffect("[MQTT-LAMPS] Lamp %d control FAILED", lampCount);
      }
    }
  }
  
  _logUsermodHourEffect("[MQTT-LAMPS] Controlled %d lamps total", lampCount);
#else
  _logUsermodHourEffect("[MQTT-LAMPS] MQTT disabled in build, cannot control lamps");
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Helper method to handle presence change logic
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::handlePresenceChange() {
  _logUsermodHourEffect("[PRESENCE-CHANGE] Starting: presenceValue=%d, NotHome=%d, NightMode=%d, enablePresenceDuringNightMode=%d",
                        presenceValue, NotHome, NightMode, enablePresenceDuringNightMode);
  
  // Check if we should ignore this presence change due to NightMode
  if (presenceValue && NightMode && !enablePresenceDuringNightMode) {
    _logUsermodHourEffect("[PRESENCE-CHANGE] Ignoring presence=true during NightMode (enablePresenceDuringNightMode=false)");
    return;
  }
  
  if (presenceValue && !NotHome && (enablePresenceDuringNightMode || !NightMode)) {
    _logUsermodHourEffect("[PRESENCE-CHANGE] Conditions met for turning LEDs ON");
    NightNothomeTrigger();
    _SetLedsOn(true);
    _logUsermodHourEffect("[PRESENCE-CHANGE] LEDs turned ON");
  } else {
    _logUsermodHourEffect("[PRESENCE-CHANGE] Conditions met for turning LEDs OFF");
    
    // Turn off LEDs based on conditions
    if (NotHome) {
      // During NightMode: always turn off when no presence
      _logUsermodHourEffect("[PRESENCE-CHANGE] NotHome state - turning LEDs OFF");
      NightNothomeTrigger();
      _SetLedsOn(false);
    } else if (NightMode && enableNightModePowerOff) {
      // NotHome: always turn off
      _logUsermodHourEffect("[PRESENCE-CHANGE] NightMode with power-off enabled - turning LEDs OFF");
      NightNothomeTrigger();
      _SetLedsOn(false);  
    } else if (!presenceValue && NightMode && !enablePresenceDuringNightMode) {
      // Standard nightmode behavior when presence feature is disabled
      _logUsermodHourEffect("[PRESENCE-CHANGE] NightMode without presence feature - turning LEDs OFF");
      NightNothomeTrigger();
      _SetLedsOn(false);
    } else if (!presenceValue) {
      // Turn off when no presence in normal mode (not NightMode, not NotHome)
      _logUsermodHourEffect("[PRESENCE-CHANGE] No presence in normal mode - turning LEDs OFF");
      NightNothomeTrigger();
      _SetLedsOn(false);
    } else {
      _logUsermodHourEffect("[PRESENCE-CHANGE] No action taken (preserving current state)");
    }
  }
  
  _logUsermodHourEffect("[PRESENCE-CHANGE] Completed");
}

///////////////////////////////////////////////////////////////////////////////
// Helper method to handle lux change logic
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::handleLuxChange() {
  _logUsermodHourEffect("[LUX-CHANGE] Starting: luxValue=%.1f, luxThreshold=%d, NotHome=%d, NightMode=%d, triggerMode=%d, bri=%d", 
                        luxValue, luxThreshold, NotHome, NightMode, triggerMode, bri);
  
  if (NotHome || NightMode) {
    _logUsermodHourEffect("[LUX-CHANGE] Skipping - NotHome or NightMode active");
    return;
  }
  
  bool luxBelowThreshold = (luxValue < luxThreshold);
  _logUsermodHourEffect("[LUX-CHANGE] Lux comparison: %.1f %s %d = %s", 
                        luxValue, "<", luxThreshold, luxBelowThreshold ? "BELOW" : "ABOVE/EQUAL");
  
  if (luxBelowThreshold) {
    _logUsermodHourEffect("[LUX-CHANGE] Lux below threshold - turning LEDs ON");
    NightNothomeTrigger();
    _SetLedsOn(true);
  } else if (triggerMode == 3) {
    _logUsermodHourEffect("[LUX-CHANGE] Lux above threshold with turn-off enabled (mode 3) - turning LEDs OFF");
    NightNothomeTrigger();
    _SetLedsOn(false);
  } else {
    _logUsermodHourEffect("[LUX-CHANGE] Lux above threshold but turn-off not enabled (mode %d) - no action", triggerMode);
  }
  
  _logUsermodHourEffect("[LUX-CHANGE] Completed");
}

///////////////////////////////////////////////////////////////////////////////
// Helper method to handle combined presence and lux trigger logic
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::handlePresenceLuxTrigger() {
  // Check if effects are currently running - if so, don't interrupt
  if (BlockTriggers) {
    _logUsermodHourEffect("[TRIGGER] Blocked - effect is currently running (BlockTriggers=true)");
    return;
  }

  _logUsermodHourEffect("[TRIGGER] ========== Combined Trigger Start ==========");
  _logUsermodHourEffect("[TRIGGER] Mode=%d, Presence=%d, Lux=%.1f, Threshold=%d, NotHome=%d, NightMode=%d, PresenceBlocker=%d, bri=%d", 
                        triggerMode, presenceValue, luxValue, luxThreshold, NotHome, NightMode, PresenceBlocker, bri);
  
  // Check if we should skip due to NotHome or NightMode (except for presence-only mode)
  bool skipDueToMode = (NotHome || (NightMode && !enablePresenceDuringNightMode));
  _logUsermodHourEffect("[TRIGGER] Skip due to mode check: %s", skipDueToMode ? "YES" : "NO");
  
  switch (triggerMode) {
    case 0: // None - do nothing
      _logUsermodHourEffect("[TRIGGER] Mode 0: None - no action taken");
      break;
      
    case 1: // Presence only
      _logUsermodHourEffect("[TRIGGER] Mode 1: Presence only");
      handlePresenceChange();
      break;
      
    case 2: // Lux only (no turn off)
      _logUsermodHourEffect("[TRIGGER] Mode 2: Lux only (no turn off)");
      if (skipDueToMode) {
        _logUsermodHourEffect("[TRIGGER] Skipped - mode restriction");
        return;
      }
      handleLuxChange();
      break;
      
    case 3: // Lux only (with turn off)
      _logUsermodHourEffect("[TRIGGER] Mode 3: Lux only (with turn off)");
      if (skipDueToMode) {
        _logUsermodHourEffect("[TRIGGER] Skipped - mode restriction");
        return;
      }
      handleLuxChange();
      break;
      
    case 4: // Lux and presence - LED ON when both true, OFF when presence false
      _logUsermodHourEffect("[TRIGGER] Mode 4: Lux+Presence (OFF on presence false)");
      
      if (presenceValue && (luxValue < luxThreshold) && !skipDueToMode && bri == 0) {
        _logUsermodHourEffect("[TRIGGER] Mode 4: Both conditions met, turning LEDs ON");
        NightNothomeTrigger();
        _SetLedsOn(true);
      } else if (!presenceValue && bri != 0) {
        _logUsermodHourEffect("[TRIGGER] Mode 4: Presence false, turning LEDs OFF");
        NightNothomeTrigger();
        _SetLedsOn(false);
      } else {
        _logUsermodHourEffect("[TRIGGER] Mode 4: No action (bri=%d, presence=%d, lux=%.1f)", bri, presenceValue, luxValue);
      }
      break;

    case 5: // Lux and presence - LED ON when both true, OFF when BOTH false
      _logUsermodHourEffect("[TRIGGER] Mode 5: Lux+Presence (OFF on both false)");
      
      if (presenceValue && (luxValue < luxThreshold) && !skipDueToMode && bri == 0) {
        _logUsermodHourEffect("[TRIGGER] Mode 5: Both conditions met, turning LEDs ON");
        NightNothomeTrigger();
        _SetLedsOn(true);
      } else if (!presenceValue && (luxValue >= luxThreshold) && bri != 0) {
        _logUsermodHourEffect("[TRIGGER] Mode 5: Both conditions false, turning LEDs OFF");
        NightNothomeTrigger();
        _SetLedsOn(false);
      } else {
        _logUsermodHourEffect("[TRIGGER] Mode 5: One condition true - no action (bri=%d)", bri);
      }
      break;
      
    default:
      _logUsermodHourEffect("[TRIGGER] Unknown trigger mode: %d", triggerMode);
      break;
  }
  
  _logUsermodHourEffect("[TRIGGER] ========== Combined Trigger End ==========");
}

///////////////////////////////////////////////////////////////////////////////
// Helper method to validate hour values
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::validateHourValues() {
  _logUsermodHourEffect("[VALIDATE] Validating hour values: NightModeOn=%d, NightModeOff=%d", NightModeOn, NightModeOff);
  
  if (NightModeOn < 0 || NightModeOn > 23) {
    _logUsermodHourEffect("[VALIDATE] Invalid NightModeOn value: %d, resetting to 1", NightModeOn);
    NightModeOn = 1;
  }
  if (NightModeOff < 0 || NightModeOff > 23) {
    _logUsermodHourEffect("[VALIDATE] Invalid NightModeOff value: %d, resetting to 8", NightModeOff);
    NightModeOff = 8;
  }
  
  _logUsermodHourEffect("[VALIDATE] Hour values are valid");
}

///////////////////////////////////////////////////////////////////////////////
// Helper method to allocate input pin
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::allocateInputPin() {
  int8_t pin = inputPin[0];

  if (pin < 0) {
    _logUsermodHourEffect("[INPUT-PIN] Input pin not configured (pin=-1), skipping allocation");
    return;
  }

  _logUsermodHourEffect("[INPUT-PIN] Allocating input pin %d...", pin);
  if (PinManager::allocatePin(pin, false, PinOwner::UM_HOUR_EFFECT)) {
    pinMode(pin, inputActiveLow ? INPUT_PULLUP : INPUT);	
    inputPinAllocated = true;
    lastInputState = digitalRead(pin);
    _logUsermodHourEffect("[INPUT-PIN] Successfully allocated input pin %d (Active %s). Initial state: %s", 
                          pin, inputActiveLow ? "LOW" : "HIGH",
                          lastInputState ? "HIGH" : "LOW");
  } else {
    _logUsermodHourEffect("[INPUT-PIN] Failed to allocate input pin %d! Pin may be in use.", pin);
    inputPinAllocated = false;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Helper method to deallocate input pin
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::deallocateInputPin() {
  if (oldInputPin < 0) {
    _logUsermodHourEffect("[INPUT-PIN] No old input pin to deallocate");
    return;
  }

  _logUsermodHourEffect("[INPUT-PIN] Deallocating old input pin %d...", oldInputPin);
  if (PinManager::isPinAllocated(oldInputPin, PinOwner::UM_HOUR_EFFECT)) {
    PinManager::deallocatePin(oldInputPin, PinOwner::UM_HOUR_EFFECT);
	
	if (PinManager::isPinOk(oldInputPin)) {
		_logUsermodHourEffect("[INPUT-PIN] Input pin deallocated successfully: %d", oldInputPin);
	} else {
		_logUsermodHourEffect("[INPUT-PIN] Failed to deallocate input pin: %d", oldInputPin);
	}
  } else {
    _logUsermodHourEffect("[INPUT-PIN] Old input pin %d was not allocated by this usermod", oldInputPin);
  }
  inputPinAllocated = false;
}

///////////////////////////////////////////////////////////////////////////////
// Helper method to check input pin and trigger presence detection
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::checkInputPin() {
  int8_t pin = inputPin[0];
  if (pin < 0 || !inputPinAllocated) {
    return; // No logging - would spam
  }

  unsigned long currentMillis = millis();
  if (currentMillis - lastInputCheck < INPUT_CHECK_INTERVAL) {
    return; // Don't check too frequently - no logging to avoid spam
  }
  lastInputCheck = currentMillis;

  bool currentState = digitalRead(pin);
  
  // Determine if input is "active" based on configuration
  bool inputActive = inputActiveLow ? !currentState : currentState;
  bool wasActive = inputActiveLow ? !lastInputState : lastInputState;
  
  // Detect state change
  if (inputActive != wasActive) {
    _logUsermodHourEffect("[INPUT-PIN] Pin %d state changed: %s -> %s (Raw: %s -> %s, Active %s)", 
                          pin,
                          wasActive ? "ACTIVE" : "INACTIVE",
                          inputActive ? "ACTIVE" : "INACTIVE",
                          lastInputState ? "HIGH" : "LOW",
                          currentState ? "HIGH" : "LOW",
                          inputActiveLow ? "LOW" : "HIGH");
    
    lastInputState = currentState;
    
    // Treat input activation as presence detection
    presenceValue = inputActive;
    
    _logUsermodHourEffect("[INPUT-PIN] Presence value updated from input pin: %d", presenceValue);
    
    _logUsermodHourEffect("[INPUT-PIN] Current system state: NightMode=%d, NotHome=%d, PresenceBlocker=%d, bri=%d", 
                          NightMode, NotHome, PresenceBlocker, bri);
                        
    // Only act if not blocked
    if (!PresenceBlocker) {
      _logUsermodHourEffect("[INPUT-PIN] Blocker not active, triggering presence/lux handler");
      handlePresenceLuxTrigger();
    } else {
      _logUsermodHourEffect("[INPUT-PIN] Blocker ACTIVE, skipping trigger");
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// onMqttConnect: Called when MQTT connects. Subscribes to required topics.
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::onMqttConnect(bool sessionPresent) {
#ifndef WLED_DISABLE_MQTT
  if (!WLED_MQTT_CONNECTED || !enabledUsermod) return;

  _logUsermodHourEffect("[MQTT-CONNECT] MQTT Connected: sessionPresent=%d, enabledUsermod=%d", 
                        sessionPresent, enabledUsermod);

  char subBuffer[128];
  // triggert eingehende Meldungen wenn unter config etwas published wird
  //if (mqttDeviceTopic[0]) {
	// Subscribe to device configuration topics.
	//sprintf_P(subBuffer, PSTR("%s/config/#"), mqttDeviceTopic);
	//subscribeAndLog(subBuffer);

	//resetScheduledTime = millis();
	//ResetEffect = true;
	//_logUsermodHourEffect("[MQTT-CONNECT] Effect reset scheduled for %lu ms from now", RESET_DELAY_MS);
  //}
  if (mqttGroupTopic[0]) {
    // Subscribe to group topics for NightMode, NotHome, NewEffect, and 3D printer finished.
    _logUsermodHourEffect("[MQTT-CONNECT] Subscribing to mode topics (NightMode, NotHome, NewEffect, 3dPrinterFinshed)");
    const char* topics[] = {"NightMode", "NotHome", "NewEffect", "3dPrinterFinshed"};

    for (auto topic : topics) {
      sprintf_P(subBuffer, PSTR("%s/%s"), mqttGroupTopic, topic);
      subscribeAndLog(subBuffer);
    }
    
    // Subscribe based on mode - PRESENCE
    if (useSimplePresence && MqttPresence.length()) {
      _logUsermodHourEffect("[MQTT-CONNECT] Subscribing to simple presence: %s", MqttPresence.c_str());
      
      // Check if multi-topic
      if (MqttPresence.indexOf(',') >= 0) {
        _logUsermodHourEffect("[MQTT-CONNECT] Detected multi-topic presence configuration");
        std::vector<String> topics = parseTopicList(MqttPresence);
        
        for (const auto& topic : topics) {
          subscribeAndLog(topic.c_str());
        }
      } else {
        subscribeAndLog(MqttPresence.c_str());
      }
    } else if (!useSimplePresence) {
      _logUsermodHourEffect("[MQTT-CONNECT] Subscribing to advanced presence: %d sensors", presenceConfig.sensors.size());
      for (const auto& sensor : presenceConfig.sensors) {
        subscribeAndLog(sensor.topic.c_str());
      }
    }
    
    // Subscribe based on mode - BLOCKER
    if (useSimpleBlocker && MqttPresenceBlocker.length()) {
      _logUsermodHourEffect("[MQTT-CONNECT] Subscribing to simple blocker: %s", MqttPresenceBlocker.c_str());
      subscribeAndLog(MqttPresenceBlocker.c_str());
    } else if (!useSimpleBlocker) {
      _logUsermodHourEffect("[MQTT-CONNECT] Subscribing to advanced blocker: %d sensors", blockerConfig.sensors.size());
      for (const auto& sensor : blockerConfig.sensors) {
        subscribeAndLog(sensor.topic.c_str());
      }
    }
    
    // Subscribe based on mode - LUX
    if (useSimpleLux && MqttLux.length()) {
      _logUsermodHourEffect("[MQTT-CONNECT] Subscribing to simple lux: %s", MqttLux.c_str());
      
      if (MqttLux.indexOf(',') >= 0) {
        _logUsermodHourEffect("[MQTT-CONNECT] Detected multi-topic lux configuration");
        std::vector<String> topics = parseTopicList(MqttLux);
        
        for (const auto& topic : topics) {
          subscribeAndLog(topic.c_str());
        }
      } else {
        subscribeAndLog(MqttLux.c_str());
      }
    } else if (!useSimpleLux) {
      _logUsermodHourEffect("[MQTT-CONNECT] Subscribing to advanced lux: %d sensors", luxConfig.sensors.size());
      for (const auto& sensor : luxConfig.sensors) {
        subscribeAndLog(sensor.topic.c_str());
      }
    }

    resetScheduledTime = millis();
    ResetEffect = true;
    _logUsermodHourEffect("[MQTT-CONNECT] MQTT setup complete, effect reset scheduled for %lu ms from now", RESET_DELAY_MS);
  }
#endif
}

///////////////////////////////////////////////////////////////////////////////
// isTimeMatch: Returns true if current local time equals targetHour:00:00.
///////////////////////////////////////////////////////////////////////////////
bool UsermodHourEffect::isTimeMatch(int targetHour) {
  bool result = (hour(localTime) == targetHour && minute(localTime) == 0 && second(localTime) == 0);
  if (result) {
    _logUsermodHourEffect("[TIME-MATCH] Time match for hour %d: %02d:%02d:%02d", 
                          targetHour, hour(localTime), minute(localTime), second(localTime));
  }
  return result;
}

///////////////////////////////////////////////////////////////////////////////
// NightNothomeTrigger: Interacts with other usermods to enable/disable LED
// output based on NightMode or NotHome flags.
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::NightNothomeTrigger() {
  _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] Called: NightMode=%d, NotHome=%d, enablePresenceDuringNightMode=%d, presenceValue=%d",
                        NightMode, NotHome, enablePresenceDuringNightMode, presenceValue);
  
  bool shouldDisable;

  // NOTHOME has highest priority: if true -> always disable
  if (NotHome) {
    shouldDisable = true;
    _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] NotHome is true -> forcing disable (highest priority)");
  } else if (NightMode) {
    // NIGHT behavior (NotHome is false here)
    if (enablePresenceDuringNightMode) {
      // Presence override enabled: only allow LEDs when presence detected.
      if (!presenceValue) {
        shouldDisable = true;
        _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] NightMode + presence-override ON but NO presence -> shouldDisable=true");
      } else {
        shouldDisable = false;
        _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] NightMode + presence-override ON and presence detected -> shouldDisable=false");
      }
    } else {
      // Presence override not enabled: NightMode forces disable
      shouldDisable = true;
      _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] NightMode active and presence-override OFF -> shouldDisable=true");
    }
  } else {
    // DAY behavior (NotHome and NightMode are false)
    if (!presenceValue) {
      shouldDisable = true;
      _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] Day: no presence -> shouldDisable=true");
    } else {
      shouldDisable = false;
      _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] Day: presence detected -> shouldDisable=false");
    }
  }
  
  #ifdef USERMOD_NIXIECLOCK
    if (nixie != nullptr) {
      _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] Calling setNixiePower(%s) on NixieClock usermod", 
                            shouldDisable ? "true" : "false");
      nixie->setNixiePower(shouldDisable);
    } else {
      _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] NixieClock usermod not loaded (nixie is null)");
    }
  #else
    _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] USERMOD_NIXIECLOCK NOT defined.");
  #endif

  #ifdef USERMOD_SSDR
    if (ssdr != nullptr) {
      _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] Calling disableOutputFunction(%s) on SSDR usermod", 
                            shouldDisable ? "true" : "false");
      ssdr->disableOutputFunction(shouldDisable);
    } else {
      _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] SSDR usermod not loaded (ssdr is null)");
    }
  #else
    _logUsermodHourEffect("[NIGHT-HOME-TRIGGER] USERMOD_SSDR NOT defined.");
  #endif
}

///////////////////////////////////////////////////////////////////////////////
// executeNightModeLogic: toggles NightMode based on time.
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::executeNightModeLogic() {
  if (NotHome) return;
  
  // Prevent log spam - only log once per hour
  static int lastLoggedHour = -1;
  int currentHour = hour(localTime);
  if (currentHour != lastLoggedHour) {
    _logUsermodHourEffect("[NIGHT-LOGIC] Checking NightMode logic: current hour=%d, NightModeOn=%d, NightModeOff=%d, NightMode=%d",
                          currentHour, NightModeOn, NightModeOff, NightMode);
    lastLoggedHour = currentHour;
  }
  
  // Build a timestamp string for logging.
  String timestamp = String(day(localTime)) + "." + String(month(localTime)) + "." +
                     String(year(localTime)) + " " + String(hour(localTime)) + ":" +
                     String(minute(localTime)) + ":" + String(second(localTime));
  
  // Turn NightMode OFF at the designated hour
  if (isTimeMatch(NightModeOff) && hour(localTime) != lastNightModeOffHour) {
    lastNightModeOffHour = hour(localTime);
    _logUsermodHourEffect("[NIGHT-LOGIC] NightMode OFF trigger at hour %d", NightModeOff);
    
    if (NightMode) {
      NightMode = false;
      _logUsermodHourEffect("[NIGHT-LOGIC] NightMode state changed: true -> false");
      
      if (enabledNightModePowerOn) {
        _logUsermodHourEffect("[NIGHT-LOGIC] Power-on enabled, turning LEDs ON");
        NightNothomeTrigger();
        _SetLedsOn(true);
      } else {
        _logUsermodHourEffect("[NIGHT-LOGIC] Power-on disabled, LEDs state unchanged");
      }
      publishMessage("NightMode", String("false") + " (" + timestamp + ")");
    } else {
      _logUsermodHourEffect("[NIGHT-LOGIC] NightMode was already OFF, no action needed");
    }
  }
  
  // Turn NightMode ON at the designated hour
  if (isTimeMatch(NightModeOn) && hour(localTime) != lastNightModeOnHour) {
    lastNightModeOnHour = hour(localTime);
    _logUsermodHourEffect("[NIGHT-LOGIC] NightMode ON trigger at hour %d", NightModeOn);
    
    if (!NightMode) {
      NightMode = true;
      _logUsermodHourEffect("[NIGHT-LOGIC] NightMode state changed: false -> true");
      
      if (enableNightModePowerOff) {
        _logUsermodHourEffect("[NIGHT-LOGIC] Power-off enabled, evaluating conditions:");
        _logUsermodHourEffect("[NIGHT-LOGIC]   enablePresenceDuringNightMode=%d, presenceValue=%d", 
                             enablePresenceDuringNightMode, presenceValue);
        
        // Call NightNothomeTrigger to handle SSDR/Nixie
        NightNothomeTrigger();
        
        // Only turn LEDs off if conditions are met
        // Turn OFF unless BOTH presence-during-nightmode is enabled AND presence is detected
        if (!enablePresenceDuringNightMode || !presenceValue) {
        //if (!presenceValue) {
          _logUsermodHourEffect("[NIGHT-LOGIC] Turning LEDs OFF (enablePresenceDuringNightMode=%d, presenceValue=%d)", 
                               enablePresenceDuringNightMode, presenceValue);
          _SetLedsOn(false);
        } else {
          _logUsermodHourEffect("[NIGHT-LOGIC] Presence detected with enablePresenceDuringNightMode=true, keeping LEDs ON");
          // Don't turn off - keep current state
        }
      } else {
        _logUsermodHourEffect("[NIGHT-LOGIC] Power-off disabled, LEDs state unchanged");
      }
      publishMessage("NightMode", String("true") + " (" + timestamp + ")");
    } else {
      _logUsermodHourEffect("[NIGHT-LOGIC] NightMode was already ON, no action needed");
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// setup: Called once at startup.
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::setup() {
  _logUsermodHourEffect("[SETUP] Setup started...");
  NightMode = false;
  NotHome   = false;
  
  // Initialize lastBrightness
  lastBrightness = bri;
  if (bri > 0) {
    lastNonZeroBrightness = bri;
  } else if (briLast > 0) {
    lastNonZeroBrightness = briLast;
  } else {
    lastNonZeroBrightness = 128;
  }
  _logUsermodHourEffect("[SETUP] Initialized lastBrightness=%d", lastBrightness);

  #ifdef USERMOD_NIXIECLOCK
    nixie = (UsermodNixieClock*) UsermodManager::lookup(USERMOD_ID_NIXIECLOCK);
    _logUsermodHourEffect("[SETUP] USERMOD_NIXIECLOCK enabled, setup complete.");
  #else
    _logUsermodHourEffect("[SETUP] USERMOD_NIXIECLOCK NOT defined.");
  #endif

  #ifdef USERMOD_SSDR
    ssdr = (UsermodSSDR*) UsermodManager::lookup(USERMOD_ID_SSDR);
    _logUsermodHourEffect("[SETUP] USERMOD_SSDR enabled, setup complete.");
  #else
    _logUsermodHourEffect("[SETUP] USERMOD_SSDR NOT defined.");
  #endif

  // Validate hour values
  validateHourValues();
  
  // Allocate input pin if configured
  allocateInputPin();

  // Dynamically allocate backup storage for each segment.
  uint8_t numSegments = strip.getMaxSegments();
  if (segmentBackup != nullptr) {
    delete[] segmentBackup; // Prevent memory leak if setup called multiple times
    segmentBackup = nullptr; // Set to nullptr after deletion
  }
  segmentBackup = new BackupHelper<Segment>[numSegments];

  // Log complete configuration summary
  _logUsermodHourEffect("[SETUP] Usermod configuration summary:");
  _logUsermodHourEffect("[SETUP]   enabled=%d, 3DBlink=%d, hourEffect=%d", 
                        enabledUsermod, enabled3DBlink, enabledHourEffect);
  _logUsermodHourEffect("[SETUP]   nightModePowerOff=%d, nightModePowerOn=%d, presenceDuringNightMode=%d",
                        enableNightModePowerOff, enabledNightModePowerOn, enablePresenceDuringNightMode);
  _logUsermodHourEffect("[SETUP]   NightModeOn=%d, NightModeOff=%d", NightModeOn, NightModeOff);
  _logUsermodHourEffect("[SETUP] Presence configuration:");
  _logUsermodHourEffect("[SETUP]   useSimple=%d, topic='%s', sensors=%d, blocker=%d",
                        useSimplePresence, MqttPresence.c_str(), presenceConfig.sensors.size(), PresenceBlocker);
  _logUsermodHourEffect("[SETUP] Lux configuration:");
  _logUsermodHourEffect("[SETUP]   useSimple=%d, topic='%s', threshold=%d, triggerMode=%d",
                        useSimpleLux, MqttLux.c_str(), luxThreshold, triggerMode);
  _logUsermodHourEffect("[SETUP] Input pin: %d (activeLow=%d, allocated=%d)", 
                        inputPin[0], inputActiveLow, inputPinAllocated);
  _logUsermodHourEffect("[SETUP] MQTT lamps: '%s'", MqttLamps.c_str());

  _logUsermodHourEffect("[SETUP] Setup completed: numSegments=%d", numSegments);
}

///////////////////////////////////////////////////////////////////////////////
// loop: Called repeatedly.
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::loop() {
  if (!enabledUsermod) {
    // Log once every 5 minutes that usermod is disabled
    static unsigned long lastDisabledLog = 0;
    if (millis() - lastDisabledLog > 300000) {  // 300000ms = 5 minutes
      _logUsermodHourEffect("[LOOP] Usermod disabled, skipping loop execution");
      lastDisabledLog = millis();
    }
    return;
  }

  // Periodic state dump (every 60 seconds) - helps with debugging
#if ENABLE_LOOP_STATE_DUMP
  static unsigned long lastStateDump = 0;
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastStateDump > 60000) {
    lastStateDump = currentMillis;
    _logUsermodHourEffect("[LOOP-STATE] Periodic state dump:");
    _logUsermodHourEffect("[LOOP-STATE]   bri=%d, NightMode=%d, NotHome=%d, presenceValue=%d, PresenceBlocker=%d", 
                          bri, NightMode, NotHome, presenceValue, PresenceBlocker);
    _logUsermodHourEffect("[LOOP-STATE]   luxValue=%.1f, BlockTriggers=%d, ResetEffect=%d",
                          luxValue, BlockTriggers, ResetEffect);
    _logUsermodHourEffect("[LOOP-STATE]   Current time: %02d:%02d:%02d", 
                          hour(localTime), minute(localTime), second(localTime));
  }
#else
  unsigned long currentMillis = millis();
#endif

  // Check input pin for presence detection
  checkInputPin();

  // Execute NightMode logic based on the current time
  executeNightModeLogic();

  // Determine if it is time to trigger an hourly effect
  bool isEffectTime = (currentMillis - lastTime > 1000 &&
                       minute(localTime) == 0 && second(localTime) == 0);

  if (isEffectTime && !NightMode && !NotHome && enabledHourEffect && hour(localTime) != lastEffectTriggerHour) {
    // Check if we should run the hourly effect based on trigger mode
    bool shouldRunEffect = (triggerMode == 0) || (bri > 0);

    if (!shouldRunEffect) {
      lastEffectTriggerHour = hour(localTime); // Mark as processed
      _logUsermodHourEffect("[LOOP] Skipping hourly effect at %02d:00:00 - trigger mode %d active and LEDs are OFF (bri=%d)", 
                           hour(localTime), triggerMode, bri);
    } else {
      // CRITICAL FIX: Check if we're already in effect mode
      if (BlockTriggers) {
        _logUsermodHourEffect("[LOOP] Effect already running (BlockTriggers=true), skipping hourly trigger");
        lastEffectTriggerHour = hour(localTime); // Mark as processed to prevent retry
      } else {
        lastEffectTriggerHour = hour(localTime);

        BlockTriggers = true;  // Block presence/lux triggers during effect
        _logUsermodHourEffect("[LOOP] ========== Hourly Effect Trigger ==========");
        _logUsermodHourEffect("[LOOP] ALL triggers blocked for effect");
        lastTime = currentMillis;

        // Small delay to ensure block is processed
        delay(50);

        _BackupCurrentLedState();

        // Apply the effect settings
        applyEffectSettings(255, 255, 255, 255, GotEffect);

        // Schedule reset after 10 seconds
        resetScheduledTime = currentMillis;
        ResetEffect = true;
        _logUsermodHourEffect("[LOOP] Hourly effect applied, reset scheduled for %lu ms", RESET_DELAY_MS);
      }
    }
  }

  // Check if the effect should be reset (after 10 seconds)
  if (ResetEffect && (currentMillis - resetScheduledTime > RESET_DELAY_MS)) {
    ResetEffect = false;
    _logUsermodHourEffect("[LOOP] ========== Effect Reset ==========");
    _logUsermodHourEffect("[LOOP] Restoring LED state after effect");
	
	unsigned long now = millis();
    _logUsermodHourEffect("[LOOP] Reset check - now=%lu, resetScheduledTime=%lu, currentMillis=%d", now, resetScheduledTime, currentMillis);

    _RestoreLedState();

    // CRITICAL: Unblock triggers AFTER restore is complete
    BlockTriggers = false;
    _logUsermodHourEffect("[LOOP] ALL triggers unblocked after effect restore");
    
    // Re-evaluate current conditions
    if (!PresenceBlocker) {
      _logUsermodHourEffect("[LOOP] Re-evaluating conditions after restore");
      handlePresenceLuxTrigger();
    }
    
    _logUsermodHourEffect("[LOOP] Reset complete");
  }
}

///////////////////////////////////////////////////////////////////////////////
// applyEffectSettings: Apply color and effect settings to the main segment
// and all active, selected segments.
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::applyEffectSettings(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t effectMode) {
  _logUsermodHourEffect("[APPLY-EFFECT] Starting: r=%d, g=%d, b=%d, w=%d, mode=%d", r, g, b, w, effectMode);
  
  // Set flag to indicate this is an internal state change
  internalStateChange = true;
  _logUsermodHourEffect("[APPLY-EFFECT] Set internalStateChange=true");

  // Set the main segment's color.  
  auto &mainSegment = strip.getMainSegment();

  // Set the main segment's color
  mainSegment.setColor(0, RGBW32(r, g, b, w));
  
  // Clear colors for additional segments.
  for (uint8_t i = 1; i < 3; ++i) {
    mainSegment.setColor(i, 0);
  }

  _logUsermodHourEffect("[APPLY-EFFECT] Settings: speed=%d, intensity=%d, palette=%d, mode=%d",
           effectSpeed, effectIntensity, pal, effectMode);

  // Apply effect parameters to the main segment.
  mainSegment.speed      = effectSpeed;
  mainSegment.intensity  = effectIntensity;
  mainSegment.palette    = pal;
  mainSegment.mode       = effectMode;

  // Apply effect parameters to all active and selected segments
  int segmentCount = 0;
  const uint8_t segCount = strip.getSegmentsNum();

  for (uint8_t i = 0; i < segCount; i++) {
    Segment& seg = strip.getSegment(i);

    if (!seg.isActive() || !seg.isSelected()) {
      _logUsermodHourEffect("[APPLY-EFFECT] Skipping segment %d (active=%d, selected=%d)", 
                           i, seg.isActive(), seg.isSelected());
      continue;
    }

    // Apply the effect
    seg.setColor(0, RGBW32(r, g, b, w));
    seg.setColor(1, 0);
    seg.setColor(2, 0);
    seg.speed      = effectSpeed;
    seg.intensity  = effectIntensity;
    seg.palette    = pal;
    seg.mode       = effectMode;

    segmentCount++;
    _logUsermodHourEffect("[APPLY-EFFECT] Applied effect to segment %d", i);
  }
  
  _logUsermodHourEffect("[APPLY-EFFECT] Applied to %d segments total (out of %d configured)", segmentCount, segCount);
  
  NightNothomeTrigger();
  
  // Notify about color/effect changes BEFORE turning on LEDs
  stateChanged = true;
  stateUpdated(CALL_MODE_DIRECT_CHANGE);
  _logUsermodHourEffect("[APPLY-EFFECT] Color/effect changes applied");
  
  // Now turn on LEDs
  _SetLedsOn(true);
  
  // Reset flag
  internalStateChange = false;
  _logUsermodHourEffect("[APPLY-EFFECT] Set internalStateChange=false, completed");
}

bool UsermodHourEffect::isTopicMatch(const char* topic, const char* suffix) const {
  if (!topic || !suffix) {
    _logUsermodHourEffect("[TOPIC-MATCH] Null topic or suffix");
    return false;
  }
  
  size_t topicLen = strlen(topic);
  size_t suffixLen = strlen(suffix);
  
  if (topicLen < suffixLen) {
    return false; // No log - would spam
  }
  
  // Check exact match
  if (strcmp(topic, suffix) == 0) {
    _logUsermodHourEffect("[TOPIC-MATCH] Exact match: '%s' == '%s'", topic, suffix);
    return true;
  }
  
  // Check if topic ends with suffix
  bool endsWith = strcmp(topic + topicLen - suffixLen, suffix) == 0;
  if (endsWith) {
    _logUsermodHourEffect("[TOPIC-MATCH] Suffix match: '%s' ends with '%s'", topic, suffix);
  }
  return endsWith;
}

///////////////////////////////////////////////////////////////////////////////
// Helper: Parse comma-separated topic list into vector
///////////////////////////////////////////////////////////////////////////////
std::vector<String> UsermodHourEffect::parseTopicList(const String& topicList) {
  std::vector<String> topics;
  String list = topicList;
  list.trim();
  
  int startIdx = 0;
  int commaIdx = list.indexOf(',');
  
  while (commaIdx >= 0 || startIdx < list.length()) {
    String topic;
    
    if (commaIdx >= 0) {
      topic = list.substring(startIdx, commaIdx);
      startIdx = commaIdx + 1;
      commaIdx = list.indexOf(',', startIdx);
    } else {
      topic = list.substring(startIdx);
      startIdx = list.length();
    }
    
    topic.trim();
    if (topic.length() > 0) {
      topics.push_back(topic);
    }
  }
  
  _logUsermodHourEffect("[PARSE-TOPICS] Parsed %d topics from: %s", topics.size(), topicList.c_str());
  return topics;
}

///////////////////////////////////////////////////////////////////////////////
// onMqttMessage: Processes incoming MQTT messages and updates LED settings,
// NightMode, NotHome, and effect parameters accordingly.
///////////////////////////////////////////////////////////////////////////////
bool UsermodHourEffect::onMqttMessage(char* topic, char* payload) {
#ifndef WLED_DISABLE_MQTT
  if (!WLED_MQTT_CONNECTED || !enabledUsermod || !topic || !payload) {
    if (!enabledUsermod) _logUsermodHourEffect("[MQTT-MSG] Usermod disabled, ignoring MQTT message");
    if (!topic || !payload) _logUsermodHourEffect("[MQTT-MSG] Invalid MQTT topic or payload (null)");
    return false;
  }

  _logUsermodHourEffect("[MQTT-MSG] Message received: topic=%s, payload=%s", topic, payload);
  String payloadString = String(payload);
  
  _logUsermodHourEffect("[MQTT-MSG] Config status: useSimplePresence=%s, sensors=%d", 
                        useSimplePresence ? "true" : "false", presenceConfig.sensors.size());
  _logUsermodHourEffect("[MQTT-MSG] Blocker config: useSimpleBlocker=%s, sensors=%d", 
                        useSimpleBlocker ? "true" : "false", blockerConfig.sensors.size());
  _logUsermodHourEffect("[MQTT-MSG] Lux config: useSimpleLux=%s, sensors=%d", 
                        useSimpleLux ? "true" : "false", luxConfig.sensors.size());
  
  // Build a timestamp string for logging.
  String timestamp = String(day(localTime)) + "." + String(month(localTime)) + "." +
                     String(year(localTime)) + " " + String(hour(localTime)) + ":" +
                     String(minute(localTime)) + ":" + String(second(localTime));

  if (isTopicMatch(topic, "/NewEffect")) {
    _logUsermodHourEffect("[MQTT-MSG] Processing /NewEffect");
    GotEffect = atoi(payload);
    ntpLastSyncTime = NTP_NEVER; // Force a new NTP query.
    publishMessage("NewEffect", payloadString + " (" + timestamp + ")");
    return true;
  }

  if (isTopicMatch(topic, "/NightMode")) {
    _logUsermodHourEffect("[MQTT-MSG] Processing /NightMode");
    bool isNightMode = (payloadString == "true");
    if (isNightMode != NightMode) {
      NightMode = isNightMode;
      _logUsermodHourEffect("[MQTT-MSG] NightMode CHANGED: %d", NightMode);
      //NightNothomeTrigger();
      //if (NightMode && enableNightModePowerOff && (!enablePresenceDuringNightMode || !presenceValue)) {
      if (NightMode && enableNightModePowerOff) {
        // Turning nightmode on - turn off LEDs unless presence during nightmode is enabled and presence detected
        if (!enablePresenceDuringNightMode || !presenceValue) {
          NightNothomeTrigger();
          _SetLedsOn(false);
          _logUsermodHourEffect("[MQTT-MSG] Turned LEDs OFF due to NightMode activation");
        } else {
          _logUsermodHourEffect("[MQTT-MSG] NightMode ON but presence detected, keeping LEDs ON");
        }
        //_SetLedsOn(false);
        //_logUsermodHourEffect("Turned LEDs OFF due to NightMode activation.");
      //} else if (hour(localTime) >= NightModeOff && !NotHome && enabledNightModePowerOn) {
      } else if (!NightMode && hour(localTime) >= NightModeOff && !NotHome && enabledNightModePowerOn) {
        // Turning nightmode off and not away from home - turn on LEDs
        NightNothomeTrigger();
        _SetLedsOn(true);
        _logUsermodHourEffect("[MQTT-MSG] Turned LEDs ON after NightMode ended");
      }
      publishMessage("NightMode", payloadString + " (" + timestamp + ")");
    } else {
      _logUsermodHourEffect("[MQTT-MSG] NightMode value unchanged, ignoring");
    }
    return true;
  }

  if (isTopicMatch(topic, "/NotHome")) {
    _logUsermodHourEffect("[MQTT-MSG] Processing /NotHome");
    bool isNotHome = (payloadString == "true");
    if (isNotHome != NotHome) {
      NotHome = isNotHome;
      _logUsermodHourEffect("[MQTT-MSG] NotHome CHANGED: %d", NotHome);
      if (isNotHome) {
        NightNothomeTrigger();
        _SetLedsOn(false);
        _logUsermodHourEffect("[MQTT-MSG] Turned LEDs OFF due to NotHome state");
      }
      publishMessage("NotHome", payloadString + " (" + timestamp + ")");
    } else {
      _logUsermodHourEffect("[MQTT-MSG] NotHome value unchanged, ignoring");
    }
    return true;
  }

  if (isTopicMatch(topic, "/3dPrinterFinshed") && enabled3DBlink && !NotHome && !NightMode) {
    _logUsermodHourEffect("[MQTT-MSG] Processing /3dPrinterFinshed");

    unsigned long now = millis();

    // If an effect is already running/being reset, ignore duplicates.
    // Also ignore repeated messages that come in within MIN_3D_TRIGGER_MS ms (debounce).
    //if (BlockTriggers) {
    if (BlockTriggers || ResetEffect || (now - last3DTriggerTime < MIN_3D_TRIGGER_MS)) {
      _logUsermodHourEffect("[MQTT-MSG] Ignoring duplicate/late 3dPrinter trigger (BlockTriggers=%d ResetEffect=%d dt=%lu)",
                          BlockTriggers, ResetEffect, now - last3DTriggerTime);
      last3DTriggerTime = now;
      return true;
    }

    last3DTriggerTime = now;

    if (payloadString == "true") {
      BlockTriggers = true;  // Block presence/lux triggers during effect
      _logUsermodHourEffect("[MQTT-MSG] Effect mode activated, ALL triggers blocked");

      // Small delay to ensure block is processed
      delay(50);

      _BackupCurrentLedState();

      // Apply green blink effect
      applyEffectSettings(0, 255, 0, 0, 1);

      // Schedule reset after 10 seconds
      resetScheduledTime = millis();
      ResetEffect = true;
	  _logUsermodHourEffect("[MQTT-MSG] Scheduled effect reset in %lu ms (now=%lu target=%lu)", RESET_DELAY_MS, millis(), resetScheduledTime);

      publishMessage("3dPrinterFinshed", String("3D Druck fertig um: ") + timestamp);
      _logUsermodHourEffect("[MQTT-MSG] 3D printer effect applied, reset scheduled");
    }
    return true;
  }

  // Advanced presence mode
  if (!useSimplePresence && presenceConfig.sensors.size() > 0) {
    // Check if this message is for any presence sensor
    bool isPresenceTopic = false;
    for (const auto& sensor : presenceConfig.sensors) {
      if (isTopicMatch(topic, sensor.topic.c_str())) {
        isPresenceTopic = true;
        break;
      }
    }

    if (isPresenceTopic) {
      _logUsermodHourEffect("[MQTT-MSG] Processing advanced presence sensor");
      updateSensorState(presenceConfig, topic, payload);
      bool newPresenceValue = evaluatePresenceState(presenceConfig);

      if (newPresenceValue != presenceValue) {
        presenceValue = newPresenceValue;
        _logUsermodHourEffect("[MQTT-MSG] Presence CHANGED (advanced): %d", presenceValue);
        if (!PresenceBlocker && !BlockTriggers) {
          handlePresenceLuxTrigger();
        }
      } else {
        _logUsermodHourEffect("[MQTT-MSG] Presence unchanged (advanced): %d", presenceValue);
      }
      return true;
    }
  }
 
  // Simple presence mode
  if (useSimplePresence && !MqttPresence.isEmpty()) {
    // Check if this is a multi-topic configuration
    if (MqttPresence.indexOf(',') >= 0) {
      _logUsermodHourEffect("[MQTT-MSG] Processing multi-topic simple presence");
      handleSimpleMultiTopicPresence(MqttPresence, topic, payload);
      return true;
    }
    
    // Single topic - original behavior
    if (strcmp(topic, MqttPresence.c_str()) == 0) {
      _logUsermodHourEffect("[MQTT-MSG] Processing simple presence");
      
      bool newPresenceValue;
      float newLuxValue = luxValue;
      bool luxFound = false;
      
      if (payloadString.startsWith("{")) {
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
          _logUsermodHourEffect("[MQTT-MSG] JSON parsing error: %s", error.c_str());
          return false;
        }
        newPresenceValue = doc["presence"].as<bool>();
        
        if (doc.containsKey("illuminance")) {
          newLuxValue = doc["illuminance"].as<float>();
          luxFound = true;
          _logUsermodHourEffect("[MQTT-MSG] Found illuminance in presence JSON: %.1f", newLuxValue);
        }
      } else {
        payloadString.toLowerCase();
        newPresenceValue = (payloadString == "true" || payloadString == "1" || payloadString == "on");
      }

      // Check if values actually changed
      bool presenceChanged = (newPresenceValue != presenceValue);
      bool luxChanged = luxFound && (abs(newLuxValue - luxValue) >= 0.5);

      if (presenceChanged || luxChanged) {
        if (luxChanged) {
          luxValue = newLuxValue;
          _logUsermodHourEffect("[MQTT-MSG] Lux CHANGED: %.1f", luxValue);
        }
        if (presenceChanged) {
          presenceValue = newPresenceValue;
          _logUsermodHourEffect("[MQTT-MSG] Presence CHANGED: %d", presenceValue);
        }

        if (!PresenceBlocker && !BlockTriggers) {
          handlePresenceLuxTrigger();
        }
      } else {
        _logUsermodHourEffect("[MQTT-MSG] Presence/lux values unchanged, ignoring");
      }
      return true;
    }
  }

  // Advanced lux mode
  if (!useSimpleLux && luxConfig.sensors.size() > 0) {
    bool isLuxTopic = false;
    for (const auto& sensor : luxConfig.sensors) {
      if (isTopicMatch(topic, sensor.topic.c_str())) {
        isLuxTopic = true;
        break;
      }
    }
  
    if (isLuxTopic) {
      _logUsermodHourEffect("[MQTT-MSG] Processing advanced lux sensor");
      updateSensorState(luxConfig, topic, payload);
      for (const auto& sensor : luxConfig.sensors) {
        if (sensor.currentState) {
          float newLuxValue = extractLuxValue(sensor, payloadString);
          if (abs(newLuxValue - luxValue) >= 0.5) {
            luxValue = newLuxValue;
            _logUsermodHourEffect("[MQTT-MSG] Lux CHANGED (advanced): %.1f", luxValue);
            if (!PresenceBlocker && !BlockTriggers) {
              handlePresenceLuxTrigger();
            }
          } else {
            _logUsermodHourEffect("[MQTT-MSG] Lux unchanged (advanced): %.1f", luxValue);
          }
          break;
        }
      }
      return true;
    }
  }

  // Simple lux mode
  if (useSimpleLux && !MqttLux.isEmpty()) {
    // Check if this is a multi-topic configuration
    if (MqttLux.indexOf(',') >= 0) {
      _logUsermodHourEffect("[MQTT-MSG] Processing multi-topic simple lux");
      handleSimpleMultiTopicLux(MqttLux, topic, payload);
      return true;
    }
    
    // Single topic - original behavior
    if (strcmp(topic, MqttLux.c_str()) == 0) {
      _logUsermodHourEffect("[MQTT-MSG] Processing simple lux");
      
      float newLuxValue;
      
      if (payloadString.startsWith("{")) {
        // JSON payload
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, payloadString);
        if (error) {
          _logUsermodHourEffect("[MQTT-MSG] JSON parsing error: %s", error.c_str());
          return false;
        }
        
        // Try to find lux/illuminance field
        if (doc.containsKey("illuminance")) {
          newLuxValue = doc["illuminance"].as<float>();
        } else if (doc.containsKey("lux")) {
          newLuxValue = doc["lux"].as<float>();
        } else {
          _logUsermodHourEffect("[MQTT-MSG] No illuminance/lux field found in JSON");
          return false;
        }
        
        // Check for presence in same message
        if (doc.containsKey("presence")) {
          bool newPresenceValue = doc["presence"].as<bool>();
          if (newPresenceValue != presenceValue) {
            presenceValue = newPresenceValue;
            _logUsermodHourEffect("[MQTT-MSG] Presence CHANGED from lux message: %d", presenceValue);
          }
        }
      } else {
        // Simple numeric payload
        newLuxValue = payloadString.toFloat();
      }
      
      // Check if lux actually changed
      if (abs(newLuxValue - luxValue) >= 0.5) {
        luxValue = newLuxValue;
        _logUsermodHourEffect("[MQTT-MSG] Lux CHANGED: %.1f", luxValue);
        
        if (!PresenceBlocker && !BlockTriggers) {
          handlePresenceLuxTrigger();
        }
      } else {
        _logUsermodHourEffect("[MQTT-MSG] Lux value unchanged, ignoring");
      }
      return true;
    }
  }

  // Advanced blocker mode
  if (!useSimpleBlocker && blockerConfig.sensors.size() > 0) {
    // Check if this message is for any blocker sensor
    bool isBlockerTopic = false;
    for (const auto& sensor : blockerConfig.sensors) {
      if (isTopicMatch(topic, sensor.topic.c_str())) {
        isBlockerTopic = true;
        break;
      }
    }

    if (isBlockerTopic) {
      _logUsermodHourEffect("[BLOCKER] Processing blocker message");
      updateSensorState(blockerConfig, topic, payload);
      _logUsermodHourEffect("[BLOCKER] Evaluating blocker logic_true: '%s'", blockerConfig.logicTrue.c_str());
      bool newBlockerValue = evaluateLogicExpression(blockerConfig.logicTrue, blockerConfig);

      _logUsermodHourEffect("[BLOCKER] Blocker result: %s", newBlockerValue ? "BLOCK" : "ALLOW");
      if (newBlockerValue != PresenceBlocker) {
        PresenceBlocker = newBlockerValue;
        _logUsermodHourEffect("[BLOCKER] Blocker CHANGED to: %s", PresenceBlocker ? "ACTIVE" : "INACTIVE");
      } else {
        _logUsermodHourEffect("[BLOCKER] Blocker unchanged: %d", PresenceBlocker);
      }
      return true;
    }
  }

  // Simple blocker mode
  if (useSimpleBlocker && !MqttPresenceBlocker.isEmpty() && strcmp(topic, MqttPresenceBlocker.c_str()) == 0) {
    _logUsermodHourEffect("[MQTT-MSG] Processing simple blocker");
    payloadString.toLowerCase();
    
    bool newBlockerValue = (payloadString == "true" || payloadString == "1" || payloadString == "on");
    
    if (newBlockerValue != PresenceBlocker) {
      PresenceBlocker = newBlockerValue;
      _logUsermodHourEffect("[MQTT-MSG] Blocker CHANGED: %d", PresenceBlocker);
    } else {
      _logUsermodHourEffect("[MQTT-MSG] Blocker unchanged, ignoring");
    }
    return true;
  }
#endif
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// _SetLedsOn: Turn LEDs on/off by adjusting brightness. Checks with the
// NixieClock usermod if available.
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::_SetLedsOn(bool state) {
  #ifdef USERMOD_NIXIECLOCK
    if (nixie) {
      NixieLed = nixie->getLedEnabled();
      _logUsermodHourEffect("[SET-LEDS] Retrieved NixieLed UM enabled status: %s", NixieLed ? "TRUE" : "FALSE");
    } else {
      _logUsermodHourEffect("[SET-LEDS] NixieClock usermod not loaded (nixie is null)");
    }
  #else
    _logUsermodHourEffect("[SET-LEDS] USERMOD_NIXIECLOCK NOT defined.");
    NixieLed = true;
  #endif

  if (!NixieLed) {
    if (state && BlockTriggers) {
      _logUsermodHourEffect("[SET-LEDS] NixieLed disabled but effect running, overriding LED gate");
    } else {
      _logUsermodHourEffect("[SET-LEDS] NixieLed disabled, skipping LED control");
      return;
    }
  }
  
  _logUsermodHourEffect("[SET-LEDS] Turning LEDs %s: current brightness=%d, briLast=%d", 
                        state ? "ON" : "OFF", bri, briLast);
  
  // Set flag to indicate this is an internal state change
  internalStateChange = true;
  
  if (state) {
    // Restore brightness if it is currently off.
    if (bri == 0) {
    //if (bri == 0 && briLast > 0) {  // Prevent restoring 0 brightness
      uint8_t targetBrightness = briLast > 0 ? briLast : lastNonZeroBrightness;
      if (targetBrightness == 0) {
        targetBrightness = 128;
      }
      bri = targetBrightness;
      lastNonZeroBrightness = bri;
      _logUsermodHourEffect("[SET-LEDS] Restoring brightness: 0 -> %d", bri);
      applyFinalBri();
      lastBrightness = bri;
      controlMqttLamps(true);
    } else {
      lastNonZeroBrightness = bri;
      _logUsermodHourEffect("[SET-LEDS] LEDs already ON (bri=%d), no action", bri);
    }
  } else {
    // Save current brightness and then turn off LEDs.
    if (bri != 0) {
      briLast = bri;
      lastNonZeroBrightness = bri;
	  //if (bri > 0) {  // Only update if bri is actually non-zero
      _logUsermodHourEffect("[SET-LEDS] Saving brightness: %d -> briLast", briLast);
      bri = 0;
      applyFinalBri();
      lastBrightness = bri;
      controlMqttLamps(false);
    } else {
      _logUsermodHourEffect("[SET-LEDS] LEDs already OFF (bri=%d), no action", bri);
    }
  }
  
  // Reset flag after a short delay to allow state changes to propagate
  internalStateChange = false;
  _logUsermodHourEffect("[SET-LEDS] Completed");
}

///////////////////////////////////////////////////////////////////////////////
// _BackupCurrentLedState: Saves the current brightness and segment settings.
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::_BackupCurrentLedState() {
  _logUsermodHourEffect("[BACKUP-STATE] Starting LED state backup for effect");

  // Add safety check
  if (segmentBackup == nullptr) {
    _logUsermodHourEffect("[BACKUP-STATE] ERROR: Segment backup array is null! Re-initializing...");
    uint8_t numSegments = strip.getMaxSegments();
    segmentBackup = new BackupHelper<Segment>[numSegments];
  }
  
  // Small delay to let any ongoing transitions complete
  delay(50);

  LastBriValue = bri;  // Save current brightness.
  _logUsermodHourEffect("[BACKUP-STATE] Saved LastBriValue=%d", LastBriValue);

  // Backup global colors
  globalColorBackup.backup();
  
  // Backup each active, selected segment
  int backedUpSegments = 0;
  const uint8_t segCount = strip.getSegmentsNum();

  for (uint8_t i = 0; i < segCount; i++) {
    Segment& seg = strip.getSegment(i);
    if (!seg.isActive() || !seg.isSelected()) continue;
    if (seg.start >= seg.stop) continue;

    segmentBackup[i].backup(seg);
    segmentBackup[i].hasData = true;
    backedUpSegments++;
  
    _logUsermodHourEffect("[BACKUP-STATE] Segment %d backed up: mode=%d, speed=%d, intensity=%d, palette=%d, color0=%u, color1=%u, color2=%u",
             i, segmentBackup[i].mode, segmentBackup[i].speed, segmentBackup[i].intensity, segmentBackup[i].palette,
             segmentBackup[i].color[0], segmentBackup[i].color[1], segmentBackup[i].color[2]);
  }
  
  hasBackupRun = true;
  _logUsermodHourEffect("[BACKUP-STATE] Backup complete: backed up %d segments", backedUpSegments);
}

///////////////////////////////////////////////////////////////////////////////
// _RestoreLedState: Restores brightness and segment settings from backup.
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::_RestoreLedState() {
  _logUsermodHourEffect("[RESTORE-STATE] Starting LED state restore after effect");
  
  if (!hasBackupRun) {
    _logUsermodHourEffect("[RESTORE-STATE] No backup available, skipping restore");
    return;
  }

  // Add safety check
  if (segmentBackup == nullptr) {
    _logUsermodHourEffect("[RESTORE-STATE] ERROR: Segment backup array is null! Cannot restore.");
    hasBackupRun = false;
    return;
  }

  // Set flag to indicate this is an internal state change
  internalStateChange = true;
  _logUsermodHourEffect("[RESTORE-STATE] Set internalStateChange=true");

  // Restore global colors FIRST
  globalColorBackup.restore();

  // Restore each backed-up segment
  int restoredSegments = 0;
  const uint8_t segCount = strip.getSegmentsNum();

  for (uint8_t i = 0; i < segCount; i++) {
    if (!segmentBackup[i].hasData) continue;
    Segment& seg = strip.getSegment(i);
    segmentBackup[i].restore(seg);
    restoredSegments++;

    _logUsermodHourEffect("[RESTORE-STATE] Segment %d restored: mode=%d, color0=%u, color1=%u, color2=%u",
             i, segmentBackup[i].mode, segmentBackup[i].color[0], 
             segmentBackup[i].color[1], segmentBackup[i].color[2]);
  }

  _logUsermodHourEffect("[RESTORE-STATE] Restored %d segments total", restoredSegments);

  // Restore brightness
  _logUsermodHourEffect("[RESTORE-STATE] Restoring brightness: %d -> %d", bri, LastBriValue);
  bri = LastBriValue;
  if (bri > 0) {
    lastNonZeroBrightness = bri;
  }
  lastBrightness = bri;

  // Update display
  stateChanged = true;
  stateUpdated(CALL_MODE_DIRECT_CHANGE);
  _logUsermodHourEffect("[RESTORE-STATE] State updated (colors and effects restored)");

  // Clear backup data
  for (uint8_t i = 0; i < segCount; i++) {
    segmentBackup[i].hasData = false;
  }

  hasBackupRun = false;  // Clear backup flag after successful restore
  
  // Reset flag
  internalStateChange = false;
  _logUsermodHourEffect("[RESTORE-STATE] Set internalStateChange=false");
  
  _logUsermodHourEffect("[RESTORE-STATE] Restoration complete");
}

///////////////////////////////////////////////////////////////////////////////
// Parse JSON configuration into SensorConfig structure
///////////////////////////////////////////////////////////////////////////////
bool UsermodHourEffect::parseJsonConfig(const String& json, SensorConfig& config) {
  _logUsermodHourEffect("[JSON-PARSE] Starting JSON config parse (len=%d)", json.length());
  
  config.clear();
  
  if (json.isEmpty()) {
    _logUsermodHourEffect("[JSON-PARSE] Empty JSON string");
    return false;
  }
  
  if (!json.startsWith("{")) {
    _logUsermodHourEffect("[JSON-PARSE] Not JSON format (doesn't start with '{')");
    return false;
  }

  if (json.length() > 4096) {
    _logUsermodHourEffect("[JSON-PARSE] ERROR: JSON too large (%d bytes, max 4096)", json.length());
    return false;
  }
  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    _logUsermodHourEffect("[JSON-PARSE] JSON parse error: %s (code=%d)", error.c_str(), error.code());
    // Log first 200 chars of problematic JSON
    _logUsermodHourEffect("[JSON-PARSE] Problematic JSON (first 200 chars): %.200s", json.c_str());
    return false;
  }
  
  // Parse sensors array
  JsonArray sensorsArray = doc["sensors"];
  if (!sensorsArray.isNull()) {
    _logUsermodHourEffect("[JSON-PARSE] Found sensors array with %d elements", sensorsArray.size());
    
    for (JsonObject sensorObj : sensorsArray) {
      MqttSensor sensor;
      sensor.id = sensorObj["id"].as<String>();
      sensor.topic = sensorObj["topic"].as<String>();
      sensor.path = sensorObj["path"] | "";
      sensor.onValues = sensorObj["on_values"].as<String>();
      
      if (sensor.id.length() > 0 && sensor.topic.length() > 0) {
        config.sensors.push_back(sensor);
        _logUsermodHourEffect("[JSON-PARSE] Added sensor: id=%s, topic=%s, path=%s", 
                              sensor.id.c_str(), sensor.topic.c_str(), sensor.path.c_str());
      } else {
        _logUsermodHourEffect("[JSON-PARSE] Skipping sensor with missing id or topic");
      }
    }
  } else {
    _logUsermodHourEffect("[JSON-PARSE] No sensors array found");
  }
  
  // Parse logic expressions
  config.logicTrue = doc["logic_true"] | doc["logic"] | "";
  config.logicFalse = doc["logic_false"] | "";
  
  _logUsermodHourEffect("[JSON-PARSE] Parsed config: %d sensors, logic_true='%s', logic_false='%s'", 
                        config.sensors.size(), config.logicTrue.c_str(), config.logicFalse.c_str());
  
  bool success = config.sensors.size() > 0;
  _logUsermodHourEffect("[JSON-PARSE] Parse result: %s", success ? "SUCCESS" : "FAILED");
  
  return success;
}

///////////////////////////////////////////////////////////////////////////////
// Evaluate a sensor's state based on payload
///////////////////////////////////////////////////////////////////////////////
bool UsermodHourEffect::evaluateSensorState(const MqttSensor& sensor, const String& payload) {
  String value = payload;
  
  _logUsermodHourEffect("[SENSOR-EVAL] Evaluating sensor '%s'", sensor.id.c_str());
  
  // If path is specified, extract from JSON
  if (sensor.path.length() > 0) {
    _logUsermodHourEffect("[SENSOR-EVAL] Extracting JSON path: '%s'", sensor.path.c_str());
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      value = doc[sensor.path].as<String>();
      _logUsermodHourEffect("[SENSOR-EVAL] Extracted value: '%s'", value.c_str());
    } else {
      _logUsermodHourEffect("[SENSOR-EVAL] JSON parse error: %s", error.c_str());
      return false;
    }
  } else {
    _logUsermodHourEffect("[SENSOR-EVAL] Using direct payload value: '%s'", value.c_str());
  }
  
  // Check if value matches any of the "on" values
  value.toLowerCase();
  String onValues = sensor.onValues;
  onValues.toLowerCase();
  
  _logUsermodHourEffect("[SENSOR-EVAL] Comparing (lowercase): value='%s' against on_values='%s'", value.c_str(), onValues.c_str());
  
  int startIdx = 0;
  int commaIdx = onValues.indexOf(',');
  
  while (commaIdx >= 0 || startIdx < onValues.length()) {
    String onValue;
    
    if (commaIdx >= 0) {
      onValue = onValues.substring(startIdx, commaIdx);
      startIdx = commaIdx + 1;
      commaIdx = onValues.indexOf(',', startIdx);
    } else {
      onValue = onValues.substring(startIdx);
      startIdx = onValues.length();
    }
    
    onValue.trim();
    
    if (onValue.length() > 0 && value.indexOf(onValue) >= 0) {
      _logUsermodHourEffect("[SENSOR-EVAL] MATCH found: '%s' contains '%s' => TRUE", value.c_str(), onValue.c_str());
      return true;
    }
  }
  
  _logUsermodHourEffect("[SENSOR-EVAL] No match found => FALSE");
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// Extract lux value from sensor
///////////////////////////////////////////////////////////////////////////////
float UsermodHourEffect::extractLuxValue(const MqttSensor& sensor, const String& payload) {
  _logUsermodHourEffect("[EXTRACT-LUX] Extracting lux from sensor '%s', payload: %s", sensor.id.c_str(), payload.c_str());
  
  String value = payload;
  
  // If path is specified, extract from JSON
  if (sensor.path.length() > 0) {
    _logUsermodHourEffect("[EXTRACT-LUX] Using JSON path: '%s'", sensor.path.c_str());
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      float result = doc[sensor.path].as<float>();
      _logUsermodHourEffect("[EXTRACT-LUX] Extracted from JSON: %.1f", result);
      return result;
    } else {
      _logUsermodHourEffect("[EXTRACT-LUX] JSON parse error: %s, falling back to direct numeric conversion", error.c_str());
      // Fall through to try direct numeric parsing
    }
  }
  
  // Direct numeric value (or fallback if JSON parsing failed)
  float result = payload.toFloat();
  _logUsermodHourEffect("[EXTRACT-LUX] Direct numeric value: %.1f", result);
  return result;
}

///////////////////////////////////////////////////////////////////////////////
// Update sensor state in config when MQTT message received
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::updateSensorState(SensorConfig& config, const char* topic, const char* payload) {
  String topicStr = String(topic);
  String payloadStr = String(payload);
  
  _logUsermodHourEffect("[SENSOR-UPDATE] Received MQTT: topic='%s', payload='%s'", topic, payload);
  
  for (auto& sensor : config.sensors) {
    if (topicStr.endsWith(sensor.topic) || topicStr.indexOf(sensor.topic) >= 0) {
      _logUsermodHourEffect("[SENSOR-UPDATE] Matched sensor '%s'", sensor.id.c_str());
      
      bool newState = evaluateSensorState(sensor, payloadStr);
      
      if (newState != sensor.currentState) {
        sensor.currentState = newState;
        _logUsermodHourEffect("[SENSOR-UPDATE] Sensor '%s' state CHANGED: %s -> %s", 
          sensor.id.c_str(), 
          !newState ? "FALSE" : "TRUE",
          newState ? "TRUE" : "FALSE");
      } else {
        _logUsermodHourEffect("[SENSOR-UPDATE] Sensor '%s' state unchanged: %s", 
          sensor.id.c_str(), 
          newState ? "TRUE" : "FALSE");
      }
      break;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Evaluate logic expression with sensor IDs
// Supports: AND, OR, NOT (!), parentheses
// Example: "(sensor1 OR sensor2) AND !sensor3"
///////////////////////////////////////////////////////////////////////////////
bool UsermodHourEffect::evaluateLogicExpression(const String& expression, const SensorConfig& config) {
  if (expression.isEmpty()) {
    _logUsermodHourEffect("[LOGIC-EVAL] No logic specified - using ANY (OR all sensors)");
    // No logic specified - default to ANY (OR all sensors)
    for (const auto& sensor : config.sensors) {
      if (sensor.currentState) {
        _logUsermodHourEffect("[LOGIC-EVAL] Found active sensor, returning true");
        return true;
      }
    }
    _logUsermodHourEffect("[LOGIC-EVAL] No active sensors, returning false");
    return false;
  }
  
  // Simple recursive descent parser for boolean logic
  String expr = expression;
  expr.trim();
  expr.toLowerCase();
  
  _logUsermodHourEffect("[LOGIC-EVAL] Evaluating: '%s'", expr.c_str());
  
  // Handle parentheses recursively
  int parenStart = expr.indexOf('(');
  if (parenStart >= 0) {
    int parenEnd = expr.lastIndexOf(')');
    if (parenEnd > parenStart) {
      String inner = expr.substring(parenStart + 1, parenEnd);
      _logUsermodHourEffect("[LOGIC-EVAL] Processing parentheses: (%s)", inner.c_str());
      bool innerResult = evaluateLogicExpression(inner, config);
      
      // Replace parenthesized expression with result
      String before = expr.substring(0, parenStart);
      String after = expr.substring(parenEnd + 1);
      expr = before + (innerResult ? "true" : "false") + after;
      _logUsermodHourEffect("[LOGIC-EVAL] After parentheses replacement: '%s'", expr.c_str());
      
      return evaluateLogicExpression(expr, config);
    }
  }

  // Handle AND operator (higher precedence than OR)
  int andIdx = expr.indexOf(" and ");
  if (andIdx > 0) {
    String left = expr.substring(0, andIdx);
    String right = expr.substring(andIdx + 5);
    bool leftResult = evaluateLogicExpression(left, config);
    bool rightResult = evaluateLogicExpression(right, config);
    _logUsermodHourEffect("[LOGIC-EVAL] AND: '%s'=%s AND '%s'=%s => %s", 
      left.c_str(), leftResult?"T":"F", right.c_str(), rightResult?"T":"F", (leftResult&&rightResult)?"T":"F");
    return leftResult && rightResult;
  }
  
  // Handle OR operator
  int orIdx = expr.indexOf(" or ");
  if (orIdx > 0) {
    String left = expr.substring(0, orIdx);
    String right = expr.substring(orIdx + 4);
    bool leftResult = evaluateLogicExpression(left, config);
    bool rightResult = evaluateLogicExpression(right, config);
    _logUsermodHourEffect("[LOGIC-EVAL] OR: '%s'=%s OR '%s'=%s => %s", 
      left.c_str(), leftResult?"T":"F", right.c_str(), rightResult?"T":"F", (leftResult||rightResult)?"T":"F");
    return leftResult || rightResult;
  }
  
  // Handle NOT operator - only applies to immediate next term
  if (expr.startsWith("!")) {
    String remaining = expr.substring(1);
    remaining.trim();
    // Make sure we don't have AND or OR after the !
    if (remaining.indexOf(" and ") < 0 && remaining.indexOf(" or ") < 0) {
      bool result = evaluateLogicExpression(remaining, config);
      _logUsermodHourEffect("[LOGIC-EVAL] NOT: !'%s'=%s => %s", remaining.c_str(), result?"T":"F", !result?"T":"F");
      return !result;
    } else {
      // If there's AND/OR, we need to handle just the first term
      _logUsermodHourEffect("[LOGIC-EVAL] ERROR: NOT with complex expression without parentheses: %s, maintaining current state", expr.c_str());
      return false;
    }
  }
  
  // Handle boolean literals
  if (expr == "true") {
    _logUsermodHourEffect("[LOGIC-EVAL] Literal: true");
    return true;
  }
  if (expr == "false") {
    _logUsermodHourEffect("[LOGIC-EVAL] Literal: false");
    return false;
  }
  
  // Must be a sensor ID
  for (const auto& sensor : config.sensors) {
    String sensorId = sensor.id;
    sensorId.toLowerCase();
    if (expr == sensorId) {
      _logUsermodHourEffect("[LOGIC-EVAL] Sensor '%s' = %s", sensor.id.c_str(), sensor.currentState?"TRUE":"FALSE");
      return sensor.currentState;
    }
  }
  
  _logUsermodHourEffect("[LOGIC-EVAL] Unknown: '%s' => false", expr.c_str());
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// Enhanced: Evaluate presence/blocker state with logic_false support
// Returns true if presence detected, false otherwise
///////////////////////////////////////////////////////////////////////////////
bool UsermodHourEffect::evaluatePresenceState(const SensorConfig& config) {
  _logUsermodHourEffect("[PRESENCE-EVAL] Starting evaluation");
  _logUsermodHourEffect("[PRESENCE-EVAL] Sensors configured: %d", config.sensors.size());
  for (const auto& sensor : config.sensors) {
    _logUsermodHourEffect("[PRESENCE-EVAL]   Sensor '%s': %s", sensor.id.c_str(), sensor.currentState ? "TRUE" : "FALSE");
  }
  
  // Step 1: Evaluate logic_true
  if (config.logicTrue.length() > 0) {
    _logUsermodHourEffect("[PRESENCE-EVAL] Evaluating logic_true: '%s'", config.logicTrue.c_str());
    bool trueResult = evaluateLogicExpression(config.logicTrue, config);
    _logUsermodHourEffect("[PRESENCE-EVAL] logic_true result: %s", trueResult ? "TRUE" : "FALSE");
    
    // If true logic passes, return true (presence detected)
    if (trueResult) {
      _logUsermodHourEffect("[PRESENCE-EVAL] => FINAL RESULT: TRUE (logic_true satisfied)");
      return true;
    }
    
    // Step 2: If true logic fails, check if we have false logic
    if (config.logicFalse.length() > 0) {
      _logUsermodHourEffect("[PRESENCE-EVAL] logic_true failed, evaluating logic_false: '%s'", config.logicFalse.c_str());
      bool falseResult = evaluateLogicExpression(config.logicFalse, config);
      _logUsermodHourEffect("[PRESENCE-EVAL] logic_false result: %s", falseResult ? "TRUE" : "FALSE");
      
      // If false logic is satisfied, return false (no presence)
      if (falseResult) {
        _logUsermodHourEffect("[PRESENCE-EVAL] => FINAL RESULT: FALSE (logic_false satisfied)");
        return false;
      }
      
      // If neither true nor false logic is satisfied, maintain current state
      _logUsermodHourEffect("[PRESENCE-EVAL] => FINAL RESULT: %s (keeping current, neither logic satisfied)", presenceValue ? "TRUE" : "FALSE");
      return presenceValue; // Keep current state
    } else {
      // No false logic defined, so NOT true = false
      _logUsermodHourEffect("[PRESENCE-EVAL] => FINAL RESULT: FALSE (no logic_false, true failed)");
      return false;
    }
  }
  
  // No logic defined - default to ANY sensor (OR)
  _logUsermodHourEffect("[PRESENCE-EVAL] No logic defined, checking for ANY active sensor");
  bool hasActiveSensor = false;
  for (const auto& sensor : config.sensors) {
    if (sensor.currentState) {
      hasActiveSensor = true;
      break;
    }
  }
  _logUsermodHourEffect("[PRESENCE-EVAL] => FINAL RESULT: %s (no logic, ANY sensor)", hasActiveSensor ? "TRUE" : "FALSE");
  return hasActiveSensor;
}

///////////////////////////////////////////////////////////////////////////////
// Configuration functions: addToConfig, readFromConfig, and appendConfigData.
///////////////////////////////////////////////////////////////////////////////
void UsermodHourEffect::addToConfig(JsonObject& root) {
  _logUsermodHourEffect("[CONFIG-SAVE] Starting config save");
  
  JsonObject top = root.createNestedObject(FPSTR(_name));
  top[FPSTR(_enabledUsermod)]                 = enabledUsermod;
  top[FPSTR(_enabled3DBlink)]                 = enabled3DBlink;
  top[FPSTR(_enabledHourEffect)]              = enabledHourEffect;
  top[FPSTR(_enableNightModePowerOff)]        = enableNightModePowerOff;
  top[FPSTR(_enabledNightModePowerOn)]        = enabledNightModePowerOn;
  top[FPSTR(_enablePresenceDuringNightMode)]  = enablePresenceDuringNightMode;
  top[FPSTR(_NightModeOn)]                    = NightModeOn;
  top[FPSTR(_NightModeOff)]                   = NightModeOff;
  
  // Save presence config - simple or advanced
  if (!useSimplePresence && presenceConfig.sensors.size() > 0) {
    _logUsermodHourEffect("[CONFIG-SAVE] Saving advanced presence config (%d sensors)", presenceConfig.sensors.size());
    // Serialize back to JSON string for display in GUI
    DynamicJsonDocument doc(1024);
    JsonArray sensors = doc.createNestedArray("sensors");
    for (const auto& sensor : presenceConfig.sensors) {
      JsonObject s = sensors.createNestedObject();
      s["id"] = sensor.id;
      s["topic"] = sensor.topic;
      s["path"] = sensor.path;
      s["on_values"] = sensor.onValues;
    }
    doc["logic_true"] = presenceConfig.logicTrue;
    if (presenceConfig.logicFalse.length() > 0) {
      doc["logic_false"] = presenceConfig.logicFalse;
    }
    
    String jsonStr;
    serializeJson(doc, jsonStr);

    _logUsermodHourEffect("[CONFIG-SAVE] Presence JSON before escape (len=%d): %s", jsonStr.length(), jsonStr.c_str());
    jsonStr.replace("\"", "&quot;");
    _logUsermodHourEffect("[CONFIG-SAVE] Presence JSON after escape (len=%d): First 180 chars: %.180s", jsonStr.length(), jsonStr.c_str());
	  
    top[FPSTR(_MqttPresence)] = jsonStr;
  } else {
    _logUsermodHourEffect("[CONFIG-SAVE] Saving simple presence mode: %s", MqttPresence.c_str());
    top[FPSTR(_MqttPresence)] = MqttPresence;
  }
  
  // Save blocker config
  if (!useSimpleBlocker && blockerConfig.sensors.size() > 0) {
    _logUsermodHourEffect("[CONFIG-SAVE] Saving advanced blocker config (%d sensors)", blockerConfig.sensors.size());
    DynamicJsonDocument doc(1024);
    JsonArray sensors = doc.createNestedArray("sensors");
    for (const auto& sensor : blockerConfig.sensors) {
      JsonObject s = sensors.createNestedObject();
      s["id"] = sensor.id;
      s["topic"] = sensor.topic;
      s["path"] = sensor.path;
      s["on_values"] = sensor.onValues;
    }
    doc["logic_true"] = blockerConfig.logicTrue;
    if (blockerConfig.logicFalse.length() > 0) {
      doc["logic_false"] = blockerConfig.logicFalse;
    }
    
    String jsonStr;
    serializeJson(doc, jsonStr);
  
    _logUsermodHourEffect("[CONFIG-SAVE] Blocker JSON before escape (len=%d): %s", jsonStr.length(), jsonStr.c_str());
    jsonStr.replace("\"", "&quot;");
    _logUsermodHourEffect("[CONFIG-SAVE] Blocker JSON after escape (len=%d)", jsonStr.length());

    top[FPSTR(_MqttPresenceBlocker)] = jsonStr;
  } else {
    _logUsermodHourEffect("[CONFIG-SAVE] Saving simple blocker mode: %s", MqttPresenceBlocker.c_str());
    top[FPSTR(_MqttPresenceBlocker)] = MqttPresenceBlocker;
  }
  
  // Save lux config
  if (!useSimpleLux && luxConfig.sensors.size() > 0) {
    _logUsermodHourEffect("[CONFIG-SAVE] Saving advanced lux config (%d sensors)", luxConfig.sensors.size());
    DynamicJsonDocument doc(1024);
    JsonArray sensors = doc.createNestedArray("sensors");
    for (const auto& sensor : luxConfig.sensors) {
      JsonObject s = sensors.createNestedObject();
      s["id"] = sensor.id;
      s["topic"] = sensor.topic;
      s["path"] = sensor.path;
    }
    doc["logic_true"] = luxConfig.logicTrue;
    
    String jsonStr;
    serializeJson(doc, jsonStr);

    _logUsermodHourEffect("[CONFIG-SAVE] Lux JSON before escape (len=%d): %s", jsonStr.length(), jsonStr.c_str());
    jsonStr.replace("\"", "&quot;");
    _logUsermodHourEffect("[CONFIG-SAVE] Lux JSON after escape (len=%d)", jsonStr.length());

    top[FPSTR(_MqttLux)] = jsonStr;
  } else {
    _logUsermodHourEffect("[CONFIG-SAVE] Saving simple lux mode: %s", MqttLux.c_str());
    top[FPSTR(_MqttLux)] = MqttLux;
  }
  
  top[FPSTR(_luxThreshold)]                   = luxThreshold;
  top[FPSTR(_triggerMode)]                    = triggerMode;
  
  // Store inputPin as array for WLED's automatic pin dropdown
  JsonArray pinArray = top.createNestedArray(FPSTR(_inputPin));
  pinArray.add(inputPin[0]);
  
  top[FPSTR(_inputActiveLow)]                 = inputActiveLow;
  top[FPSTR(_MqttLamps)]                      = MqttLamps;
  
  _logUsermodHourEffect("[CONFIG-SAVE] Config save completed");
}

bool UsermodHourEffect::readFromConfig(JsonObject& root) {
  _logUsermodHourEffect("[CONFIG-LOAD] Starting config load");
  
  JsonObject top = root[FPSTR(_name)];
  bool configComplete = !top.isNull();
  
  if (top.isNull()) {
    _logUsermodHourEffect("[CONFIG-LOAD] Config object is null, using defaults");
  }
  
  configComplete &= getJsonValue(top[FPSTR(_enabledUsermod)], enabledUsermod);
  configComplete &= getJsonValue(top[FPSTR(_enabled3DBlink)], enabled3DBlink);
  configComplete &= getJsonValue(top[FPSTR(_enabledHourEffect)], enabledHourEffect);
  configComplete &= getJsonValue(top[FPSTR(_enableNightModePowerOff)], enableNightModePowerOff);
  configComplete &= getJsonValue(top[FPSTR(_enabledNightModePowerOn)], enabledNightModePowerOn);
  configComplete &= getJsonValue(top[FPSTR(_enablePresenceDuringNightMode)], enablePresenceDuringNightMode);
  
  _logUsermodHourEffect("[CONFIG-LOAD] Basic settings loaded: enabled=%d, nightModePowerOff=%d, nightModePowerOn=%d",
                        enabledUsermod, enableNightModePowerOff, enabledNightModePowerOn);
  
  // Read hour values with validation
  if (top[FPSTR(_NightModeOn)].is<int>()) {
    int tempValue = top[FPSTR(_NightModeOn)].as<int>();
    if (tempValue >= 0 && tempValue <= 23) {
      NightModeOn = tempValue;
      configComplete &= true;
    } else {
      _logUsermodHourEffect("[CONFIG-LOAD] Invalid NightModeOn value: %d, keeping default: %d", tempValue, NightModeOn);
      configComplete = false;
    }
  }
  
  if (top[FPSTR(_NightModeOff)].is<int>()) {
    int tempValue = top[FPSTR(_NightModeOff)].as<int>();
    if (tempValue >= 0 && tempValue <= 23) {
      NightModeOff = tempValue;
      configComplete &= true;
    } else {
      _logUsermodHourEffect("[CONFIG-LOAD] Invalid NightModeOff value: %d, keeping default: %d", tempValue, NightModeOff);
      configComplete = false;
    }
  }
  
  _logUsermodHourEffect("[CONFIG-LOAD] Night mode hours: On=%d, Off=%d", NightModeOn, NightModeOff);
  
  // Read presence config
  String tempMqttPresence;
  if (getJsonValue(top[FPSTR(_MqttPresence)], tempMqttPresence)) {
    _logUsermodHourEffect("[CONFIG-LOAD] Raw presence from config (len=%d): First 180 chars: %.180s", 
                          tempMqttPresence.length(), tempMqttPresence.c_str());
	  
    // Count special characters for debugging
    int crCount = 0, lfCount = 0, tabCount = 0, quoteCount = 0;
    for (size_t i = 0; i < tempMqttPresence.length(); i++) {
      char c = tempMqttPresence.charAt(i);
      if (c == 13) crCount++;
      if (c == 10) lfCount++;
      if (c == 9) tabCount++;
      if (c == '"') quoteCount++;
    }
    _logUsermodHourEffect("[CONFIG-LOAD] Found: CR=%d, LF=%d, TAB=%d, quotes=%d", crCount, lfCount, tabCount, quoteCount);
	  
    tempMqttPresence.trim();

    // Unescape HTML entities first
    int quotReplaced = 0;
    while (tempMqttPresence.indexOf("&quot;") >= 0) {
      tempMqttPresence.replace("&quot;", "\"");
      quotReplaced++;
      if (quotReplaced > 100) break; // Safety limit
    }
    _logUsermodHourEffect("[CONFIG-LOAD] Replaced %d '&quot;' with quotes", quotReplaced);

    // Remove line breaks by creating new string without them
    String cleaned = "";
    cleaned.reserve(tempMqttPresence.length());
    for (size_t i = 0; i < tempMqttPresence.length(); i++) {
      char c = tempMqttPresence.charAt(i);
      if (c != 13 && c != 10 && c != 9) {  // Skip CR, LF, TAB
        cleaned += c;
      }
    }
    tempMqttPresence = cleaned;
 
    _logUsermodHourEffect("[CONFIG-LOAD] After cleaning (len=%d): First 180 chars: %.180s", 
                          tempMqttPresence.length(), tempMqttPresence.c_str());

    if (tempMqttPresence.startsWith("{")) {
      _logUsermodHourEffect("[CONFIG-LOAD] Parsing presence JSON...");
      if (parseJsonConfig(tempMqttPresence, presenceConfig)) {
        useSimplePresence = false;
        _logUsermodHourEffect("[CONFIG-LOAD] Successfully parsed: %d sensors", presenceConfig.sensors.size());
        for (const auto& sensor : presenceConfig.sensors) {
          _logUsermodHourEffect("[CONFIG-LOAD]   Sensor: id=%s, topic=%s, path=%s", 
          sensor.id.c_str(), sensor.topic.c_str(), sensor.path.c_str());
        }
        _logUsermodHourEffect("[CONFIG-LOAD] Presence logic_true=%s, logic_false=%s", 
                              presenceConfig.logicTrue.c_str(), presenceConfig.logicFalse.c_str());
      } else {
        _logUsermodHourEffect("[CONFIG-LOAD] JSON parse FAILED, using simple mode");
        useSimplePresence = true;
        MqttPresence = "";
      }
    } else {
      // Regular simple mode topic(s)
      if (tempMqttPresence.length() <= 256) {
        MqttPresence = tempMqttPresence;
        useSimplePresence = true;
        _logUsermodHourEffect("[CONFIG-LOAD] Using simple presence mode: %s", MqttPresence.c_str());
        configComplete &= true;
      } else {
        _logUsermodHourEffect("[CONFIG-LOAD] ERROR: Presence topic too long (%d chars, max 256), ignoring", tempMqttPresence.length());
        MqttPresence = "";
        configComplete = false;
      }
    }
  }
  
  // Read blocker config (similar pattern)
  String tempMqttPresenceBlocker;
  if (getJsonValue(top[FPSTR(_MqttPresenceBlocker)], tempMqttPresenceBlocker)) {
    _logUsermodHourEffect("[CONFIG-LOAD] Raw blocker from config (len=%d): First 180 chars: %.180s", 
                          tempMqttPresenceBlocker.length(), tempMqttPresenceBlocker.c_str());
  
    tempMqttPresenceBlocker.trim();
  
    // Unescape
    int quotReplaced = 0;
    while (tempMqttPresenceBlocker.indexOf("&quot;") >= 0) {
      tempMqttPresenceBlocker.replace("&quot;", "\"");
      quotReplaced++;
      if (quotReplaced > 100) break;
    }
    _logUsermodHourEffect("[CONFIG-LOAD] Blocker replaced %d '&quot;'", quotReplaced);
  
    // Clean line breaks
    String cleaned = "";
    cleaned.reserve(tempMqttPresenceBlocker.length());
    for (size_t i = 0; i < tempMqttPresenceBlocker.length(); i++) {
      char c = tempMqttPresenceBlocker.charAt(i);
      if (c != 13 && c != 10 && c != 9) {
        cleaned += c;
      }
    }
    tempMqttPresenceBlocker = cleaned;
  
    _logUsermodHourEffect("[CONFIG-LOAD] Blocker after cleaning (len=%d): %.180s", 
                          tempMqttPresenceBlocker.length(), tempMqttPresenceBlocker.c_str());

    if (tempMqttPresenceBlocker.startsWith("{")) {
      _logUsermodHourEffect("[CONFIG-LOAD] Parsing blocker JSON...");
      if (parseJsonConfig(tempMqttPresenceBlocker, blockerConfig)) {
        useSimpleBlocker = false;
		
        _logUsermodHourEffect("[CONFIG-LOAD] Blocker parsed: %d sensors", blockerConfig.sensors.size());

        for (const auto& sensor : blockerConfig.sensors) {
          _logUsermodHourEffect("[CONFIG-LOAD]   Blocker sensor: id=%s, topic=%s, path=%s", 
          sensor.id.c_str(), sensor.topic.c_str(), sensor.path.c_str());
        }
        _logUsermodHourEffect("[CONFIG-LOAD] Blocker logic_true=%s, logic_false=%s", 
                              blockerConfig.logicTrue.c_str(), blockerConfig.logicFalse.c_str());
      } else {
        _logUsermodHourEffect("[CONFIG-LOAD] Blocker JSON parse FAILED");
        useSimpleBlocker = true;
        MqttPresenceBlocker = "";
      }
    } else {
      if (tempMqttPresenceBlocker.length() <= 128) {
        MqttPresenceBlocker = tempMqttPresenceBlocker;
        useSimpleBlocker = true;
        _logUsermodHourEffect("[CONFIG-LOAD] Using simple blocker mode: %s", MqttPresenceBlocker.c_str());
        configComplete &= true;
      } else {
        _logUsermodHourEffect("[CONFIG-LOAD] ERROR: Blocker topic too long (%d chars, max 128), ignoring", tempMqttPresenceBlocker.length());
        MqttPresenceBlocker = "";
        configComplete = false;
      }
    }
  }
  
  // Read lux config (similar pattern)
  String tempMqttLux;
  if (getJsonValue(top[FPSTR(_MqttLux)], tempMqttLux)) {
    _logUsermodHourEffect("[CONFIG-LOAD] Raw lux from config (len=%d): First 180 chars: %.180s", 
                          tempMqttLux.length(), tempMqttLux.c_str());

    tempMqttLux.trim();
  
    // Unescape
    int quotReplaced = 0;
    while (tempMqttLux.indexOf("&quot;") >= 0) {
      tempMqttLux.replace("&quot;", "\"");
      quotReplaced++;
      if (quotReplaced > 100) break;
    }
    _logUsermodHourEffect("[CONFIG-LOAD] Lux replaced %d '&quot;'", quotReplaced);
  
    // Clean line breaks
    String cleaned = "";
    cleaned.reserve(tempMqttLux.length());
    for (size_t i = 0; i < tempMqttLux.length(); i++) {
      char c = tempMqttLux.charAt(i);
      if (c != 13 && c != 10 && c != 9) {
        cleaned += c;
      }
    }
    tempMqttLux = cleaned;

    _logUsermodHourEffect("[CONFIG-LOAD] Lux after cleaning (len=%d): %.180s", 
                          tempMqttLux.length(), tempMqttLux.c_str());

    if (tempMqttLux.startsWith("{")) {
      _logUsermodHourEffect("[CONFIG-LOAD] Parsing lux JSON...");
      if (parseJsonConfig(tempMqttLux, luxConfig)) {
        useSimpleLux = false;
        _logUsermodHourEffect("[CONFIG-LOAD] Lux parsed: %d sensors", luxConfig.sensors.size());

        for (const auto& sensor : luxConfig.sensors) {
          _logUsermodHourEffect("[CONFIG-LOAD]   Lux sensor: id=%s, topic=%s, path=%s", 
          sensor.id.c_str(), sensor.topic.c_str(), sensor.path.c_str());
        }
        _logUsermodHourEffect("[CONFIG-LOAD] Lux logic_true=%s", luxConfig.logicTrue.c_str());
      } else {
        _logUsermodHourEffect("[CONFIG-LOAD] Lux JSON parse FAILED");
        useSimpleLux = true;
        MqttLux = "";
      }
    } else {
      if (tempMqttLux.length() <= 256) {
        MqttLux = tempMqttLux;
        useSimpleLux = true;
        _logUsermodHourEffect("[CONFIG-LOAD] Using simple lux mode: %s", MqttLux.c_str());
        configComplete &= true;
      } else {
        _logUsermodHourEffect("[CONFIG-LOAD] ERROR: Lux topic too long (%d chars, max 256), ignoring", tempMqttLux.length());
        MqttLux = "";
        configComplete = false;
      }
    }
  }
  
  // Load advanced presence config from dedicated field (alternative to string-encoded JSON)
  JsonObject presenceJson = top[FPSTR(_MqttPresenceAdvanced)];
  if (!presenceJson.isNull() && presenceJson.containsKey("sensors")) {
    _logUsermodHourEffect("[CONFIG-LOAD] Found advanced presence JSON object in dedicated field");
    presenceConfig.clear();
    
    JsonArray sensors = presenceJson["sensors"];
    _logUsermodHourEffect("[CONFIG-LOAD] Presence sensors array has %d elements", sensors.size());
    
    for (JsonObject sensorObj : sensors) {
      MqttSensor sensor;
      sensor.id = sensorObj["id"].as<String>();
      sensor.topic = sensorObj["topic"].as<String>();
      sensor.path = sensorObj["path"] | "";
      sensor.onValues = sensorObj["on_values"].as<String>();
      
      if (sensor.id.length() > 0 && sensor.topic.length() > 0) {
        presenceConfig.sensors.push_back(sensor);
        _logUsermodHourEffect("[CONFIG-LOAD]   Added presence sensor: id=%s, topic=%s", 
                              sensor.id.c_str(), sensor.topic.c_str());
      } else {
        _logUsermodHourEffect("[CONFIG-LOAD]   Skipped presence sensor with missing id or topic");
      }
    }
    
    presenceConfig.logicTrue = presenceJson["logic_true"] | "";
    presenceConfig.logicFalse = presenceJson["logic_false"] | "";
    _logUsermodHourEffect("[CONFIG-LOAD] Presence logic: true='%s', false='%s'", 
                          presenceConfig.logicTrue.c_str(), presenceConfig.logicFalse.c_str());
    
    if (presenceConfig.sensors.size() > 0) {
      useSimplePresence = false;
      _logUsermodHourEffect("[CONFIG-LOAD] Loaded advanced presence config from dedicated field: %d sensors", 
                            presenceConfig.sensors.size());
    } else {
      _logUsermodHourEffect("[CONFIG-LOAD] No valid presence sensors found in dedicated field");
    }
  } else {
    _logUsermodHourEffect("[CONFIG-LOAD] No advanced presence config in dedicated field");
  }
  
  // Load advanced lux config from dedicated field
  JsonObject luxJson = top[FPSTR(_MqttLuxAdvanced)];
  if (!luxJson.isNull() && luxJson.containsKey("sensors")) {
    _logUsermodHourEffect("[CONFIG-LOAD] Found advanced lux JSON object in dedicated field");
    luxConfig.clear();
    
    JsonArray sensors = luxJson["sensors"];
    _logUsermodHourEffect("[CONFIG-LOAD] Lux sensors array has %d elements", sensors.size());
    
    for (JsonObject sensorObj : sensors) {
      MqttSensor sensor;
      sensor.id = sensorObj["id"].as<String>();
      sensor.topic = sensorObj["topic"].as<String>();
      sensor.path = sensorObj["path"] | "";
      
      if (sensor.id.length() > 0 && sensor.topic.length() > 0) {
        luxConfig.sensors.push_back(sensor);
        _logUsermodHourEffect("[CONFIG-LOAD]   Added lux sensor: id=%s, topic=%s, path=%s", 
                              sensor.id.c_str(), sensor.topic.c_str(), sensor.path.c_str());
      } else {
        _logUsermodHourEffect("[CONFIG-LOAD]   Skipped lux sensor with missing id or topic");
      }
    }
    
    luxConfig.logicTrue = luxJson["logic_true"] | "";
    _logUsermodHourEffect("[CONFIG-LOAD] Lux logic_true: '%s'", luxConfig.logicTrue.c_str());
    
    if (luxConfig.sensors.size() > 0) {
      useSimpleLux = false;
      _logUsermodHourEffect("[CONFIG-LOAD] Loaded advanced lux config from dedicated field: %d sensors", 
                            luxConfig.sensors.size());
    } else {
      _logUsermodHourEffect("[CONFIG-LOAD] No valid lux sensors found in dedicated field");
    }
  } else {
    _logUsermodHourEffect("[CONFIG-LOAD] No advanced lux config in dedicated field");
  }
  
  // Load advanced blocker config from dedicated field
  JsonObject blockerJson = top[FPSTR(_MqttBlockerAdvanced)];
  if (!blockerJson.isNull() && blockerJson.containsKey("sensors")) {
    _logUsermodHourEffect("[CONFIG-LOAD] Found advanced blocker JSON object in dedicated field");
    blockerConfig.clear();
    
    JsonArray sensors = blockerJson["sensors"];
    _logUsermodHourEffect("[CONFIG-LOAD] Blocker sensors array has %d elements", sensors.size());
    
    for (JsonObject sensorObj : sensors) {
      MqttSensor sensor;
      sensor.id = sensorObj["id"].as<String>();
      sensor.topic = sensorObj["topic"].as<String>();
      sensor.path = sensorObj["path"] | "";
      sensor.onValues = sensorObj["on_values"].as<String>();
      
      if (sensor.id.length() > 0 && sensor.topic.length() > 0) {
        blockerConfig.sensors.push_back(sensor);
        _logUsermodHourEffect("[CONFIG-LOAD]   Added blocker sensor: id=%s, topic=%s", 
                              sensor.id.c_str(), sensor.topic.c_str());
      } else {
        _logUsermodHourEffect("[CONFIG-LOAD]   Skipped blocker sensor with missing id or topic");
      }
    }
    
    blockerConfig.logicTrue = blockerJson["logic_true"] | "";
    blockerConfig.logicFalse = blockerJson["logic_false"] | "";
    _logUsermodHourEffect("[CONFIG-LOAD] Blocker logic: true='%s', false='%s'", 
                          blockerConfig.logicTrue.c_str(), blockerConfig.logicFalse.c_str());
    
    if (blockerConfig.sensors.size() > 0) {
      useSimpleBlocker = false;
      _logUsermodHourEffect("[CONFIG-LOAD] Loaded advanced blocker config from dedicated field: %d sensors", 
                            blockerConfig.sensors.size());
    } else {
      _logUsermodHourEffect("[CONFIG-LOAD] No valid blocker sensors found in dedicated field");
    }
  } else {
    _logUsermodHourEffect("[CONFIG-LOAD] No advanced blocker config in dedicated field");
  }
  
  // Read other settings
  configComplete &= getJsonValue(top[FPSTR(_luxThreshold)], luxThreshold);
  configComplete &= getJsonValue(top[FPSTR(_triggerMode)], triggerMode);

  _logUsermodHourEffect("[CONFIG-LOAD] Lux settings: threshold=%d, triggerMode=%d", luxThreshold, triggerMode);

  // Read MQTT Lamps configuration
  String tempMqttLamps;
  if (getJsonValue(top[FPSTR(_MqttLamps)], tempMqttLamps)) {
    if (tempMqttLamps.length() <= 512) {
      MqttLamps = tempMqttLamps;
      _logUsermodHourEffect("[CONFIG-LOAD] MQTT Lamps loaded: %s", MqttLamps.c_str());
      configComplete &= true;
    } else {
      _logUsermodHourEffect("[CONFIG-LOAD] MqttLamps too long (%d chars), keeping default", tempMqttLamps.length());
      configComplete = false;
    }
  } else {
    configComplete &= getJsonValue(top[FPSTR(_MqttLamps)], MqttLamps);
  }
  
  // Read input pin configuration
  int8_t prevPin = inputPin[0]; // remember previous value
  if (!top[FPSTR(_inputPin)].isNull() && top[FPSTR(_inputPin)].size() > 0) {
    // reads pin[0]; default to -1 if not present
    configComplete &= getJsonValue(top[FPSTR(_inputPin)][0], inputPin[0], -1);
    _logUsermodHourEffect("[CONFIG-LOAD] Input pin loaded: %d", inputPin[0]);
  } else {
    // no pin array present -> keep existing value (don't mark as fail)
    configComplete &= true;
  }

  // If the pin changed, deallocate old and allocate new (preserve oldInputPin)
  if (inputPin[0] != prevPin) {
    _logUsermodHourEffect("[CONFIG-LOAD] Input pin changed: %d -> %d", prevPin, inputPin[0]);
    oldInputPin = prevPin;

    if (oldInputPin >= 0 && inputPinAllocated) {
      deallocateInputPin();
    }

    if (inputPin[0] >= 0) {
      allocateInputPin();
    }
  }
  
  configComplete &= getJsonValue(top[FPSTR(_inputActiveLow)], inputActiveLow);
  _logUsermodHourEffect("[CONFIG-LOAD] Input active low: %d", inputActiveLow);

#ifndef WLED_DISABLE_MQTT
  if (WLED_MQTT_CONNECTED && enabledUsermod) {
    _logUsermodHourEffect("[CONFIG-LOAD] MQTT connected, subscribing to topics");
    
    // Subscribe based on mode - PRESENCE
    if (useSimplePresence && MqttPresence.length()) {
      _logUsermodHourEffect("[CONFIG-LOAD] Processing simple presence subscription: %s", MqttPresence.c_str());
      
      // Check if multi-topic
      if (MqttPresence.indexOf(',') >= 0) {
        _logUsermodHourEffect("[CONFIG-LOAD] Detected multi-topic presence configuration");
        std::vector<String> topics = parseTopicList(MqttPresence);
        
        int topicCount = 0;
        for (const auto& topic : topics) {
          topicCount++;
          _logUsermodHourEffect("[CONFIG-LOAD] Subscribing to presence topic %d: %s", topicCount, topic.c_str());
          subscribeAndLog(topic.c_str());
        }
        _logUsermodHourEffect("[CONFIG-LOAD] Subscribed to %d presence topics", topicCount);
      } else {
        // Single topic
        _logUsermodHourEffect("[CONFIG-LOAD] Subscribing to single presence topic: %s", MqttPresence.c_str());
        subscribeAndLog(MqttPresence.c_str());
      }
    } else if (!useSimplePresence && presenceConfig.sensors.size() > 0) {
      _logUsermodHourEffect("[CONFIG-LOAD] Subscribing to %d advanced presence sensors", presenceConfig.sensors.size());
      // Advanced mode - subscribe to all configured sensor topics
      for (const auto& sensor : presenceConfig.sensors) {
        _logUsermodHourEffect("[CONFIG-LOAD]   Subscribing to presence sensor '%s': %s", 
                              sensor.id.c_str(), sensor.topic.c_str());
        subscribeAndLog(sensor.topic.c_str());
      }
    } else {
      _logUsermodHourEffect("[CONFIG-LOAD] No presence topics to subscribe to");
    }
    
    // Subscribe based on mode - BLOCKER
    if (useSimpleBlocker && MqttPresenceBlocker.length()) {
      _logUsermodHourEffect("[CONFIG-LOAD] Subscribing to simple blocker: %s", MqttPresenceBlocker.c_str());
      subscribeAndLog(MqttPresenceBlocker.c_str());
    } else if (!useSimpleBlocker && blockerConfig.sensors.size() > 0) {
      _logUsermodHourEffect("[CONFIG-LOAD] Subscribing to %d advanced blocker sensors", blockerConfig.sensors.size());
      for (const auto& sensor : blockerConfig.sensors) {
        _logUsermodHourEffect("[CONFIG-LOAD]   Subscribing to blocker sensor '%s': %s", 
                              sensor.id.c_str(), sensor.topic.c_str());
        subscribeAndLog(sensor.topic.c_str());
      }
    } else {
      _logUsermodHourEffect("[CONFIG-LOAD] No blocker topics to subscribe to");
    }
    
    // Subscribe based on mode - LUX
    if (useSimpleLux && MqttLux.length()) {
      _logUsermodHourEffect("[CONFIG-LOAD] Processing simple lux subscription: %s", MqttLux.c_str());
      
      // Check if multi-topic
      if (MqttLux.indexOf(',') >= 0) {
        _logUsermodHourEffect("[CONFIG-LOAD] Detected multi-topic lux configuration");
        std::vector<String> topics = parseTopicList(MqttLux);
        
        int topicCount = 0;
        for (const auto& topic : topics) {
          topicCount++;
          _logUsermodHourEffect("[CONFIG-LOAD] Subscribing to lux topic %d: %s", topicCount, topic.c_str());
          subscribeAndLog(topic.c_str());
        }
        _logUsermodHourEffect("[CONFIG-LOAD] Subscribed to %d lux topics", topicCount);
      } else {
        _logUsermodHourEffect("[CONFIG-LOAD] Subscribing to single lux topic: %s", MqttLux.c_str());
        subscribeAndLog(MqttLux.c_str());
      }
    } else if (!useSimpleLux && luxConfig.sensors.size() > 0) {
      _logUsermodHourEffect("[CONFIG-LOAD] Subscribing to %d advanced lux sensors", luxConfig.sensors.size());
      for (const auto& sensor : luxConfig.sensors) {
        _logUsermodHourEffect("[CONFIG-LOAD]   Subscribing to lux sensor '%s': %s", 
                              sensor.id.c_str(), sensor.topic.c_str());
        subscribeAndLog(sensor.topic.c_str());
      }
    } else {
      _logUsermodHourEffect("[CONFIG-LOAD] No lux topics to subscribe to");
    }
    
    _logUsermodHourEffect("[CONFIG-LOAD] MQTT subscription phase completed");
  } else {
    _logUsermodHourEffect("[CONFIG-LOAD] MQTT not connected or usermod disabled, skipping subscriptions: MQTT_CONNECTED=%d, enabledUsermod=%d", 
                          WLED_MQTT_CONNECTED, enabledUsermod);
  }
#else
  _logUsermodHourEffect("[CONFIG-LOAD] MQTT disabled in build, skipping subscriptions");
#endif
  
  _logUsermodHourEffect("[CONFIG-LOAD] Config load completed: configComplete=%d", configComplete);
  return configComplete;
}

void UsermodHourEffect::appendConfigData() {
	// Inject CSS for structured layout with textarea support
	oappend(F("var s=document.createElement('style');s.innerHTML='"
	  "#hour-effect-root{"
		"border-top:2px solid #444;"
		"border-bottom:2px solid #444;"
		"padding:15px 0;"
		"margin:15px auto;"
		"display:flex;"
		"flex-direction:column;"
		"align-items:center"
	  "}"
	  "#hour-effect-root h3{"
		"width:100%;"
		"text-align:center"
	  "}"
	  "#hour-effect-root .he-group{"
		"background:#222;"
		"border:1px solid #444;"
		"border-radius:8px;"
		"padding:15px;"
		"margin:15px 0;"
		"width:fit-content;"
		"min-width:600px"
	  "}"
	  "#hour-effect-root .he-group-title{"
		"font-weight:bold;"
		"font-size:1.1em;"
		"color:#fca;"
		"margin-bottom:12px;"
		"padding-bottom:8px;"
		"border-bottom:1px solid #444;"
		"white-space:nowrap"
	  "}"
	  "#hour-effect-root .he-row{"
		"display:flex;"
		"align-items:center;"
		"margin:8px 0;"
		"min-height:30px"
	  "}"
	  "#hour-effect-root .he-label{"
		"flex:0 0 350px;"
		"text-align:left;"
		"color:#ddd;"
		"padding-right:20px;"
		"white-space:nowrap"
	  "}"
	  "#hour-effect-root .he-input{"
		"display:flex;"
		"align-items:center;"
		"flex:0 0 450px;"
		"text-align:left;"
		"gap:8px;"
	  "}"
	  "#hour-effect-root .he-input > *{"
		"min-width:0;"
	  "}"
	  "#hour-effect-root .he-input input[type=\"text\"],"
	  "#hour-effect-root .he-input input[type=\"number\"],"
	  "#hour-effect-root .he-input input[type=\"search\"],"
	  "#hour-effect-root .he-input input[type=\"email\"],"
	  "#hour-effect-root .he-input input[type=\"url\"],"
	  "#hour-effect-root .he-input input[type=\"tel\"],"
	  "#hour-effect-root .he-input input:not([type]),"
	  "#hour-effect-root .he-input select{"
		"width:100% !important;"
		"max-width:100% !important;"
		"box-sizing:border-box !important;"
		"margin:0 !important;"
		"padding:4px 6px !important;"
	  "}"
	  "#hour-effect-root .he-input textarea,"
	  "#hour-effect-root .he-input .he-textarea{"
		"width:100% !important;"
		"max-width:100% !important;"
		"box-sizing:border-box !important;"
		"margin:0 !important;"
		"padding:4px 6px !important;"
		"min-height:60px !important;"
		"resize:vertical !important;"
		"font-family:inherit !important;"
		"background-color:#333333 !important;"
		"color:#fff !important;"
		"border:1px solid #fff !important;"
		"border-radius:5px !important;"
	  "}"
	  /* Brighter white border on textarea when focused */
	  "#hour-effect-root .he-input textarea:focus,"
	  "#hour-effect-root .he-input .he-textarea:focus{"
		"background-color:#333333 !important;"
		"outline:none !important;"
		"border:2px solid #fff !important;"
		"padding:3px 5px !important;"
	  "}"
	  "#hour-effect-root .he-input input[type=\"checkbox\"]{"
		"margin:0 8px 0 0 !important;"
		"width:auto !important;"
		"vertical-align:middle !important;"
		"transform:none !important;"
	  "}"
	  "#hour-effect-root hr{"
		"display:none !important"
	  "}"
	  "#hour-effect-root br{"
		"display:none"
	  "}"
	"';document.head.appendChild(s);"));

	// Inject Layout Logic
	oappend(F("function setupHourEffect(){"
	  "try{"
		"const headers=document.querySelectorAll('h3');"
		"let header=null;"
		"headers.forEach(h=>{"
		  "if(h.textContent.trim()==='KrX_MQTT_Commander'){"
			"header=h;"
			"h.textContent='KrX MQTT Commander';"
			"h.style.marginTop='20px';"
			"h.style.color='';"
		  "}"
		"});"

		"if(header && !document.getElementById('hour-effect-root')){"
		  "const root=document.createElement('div');"
		  "root.id='hour-effect-root';"
		  "header.parentNode.insertBefore(root,header);"
		  "root.appendChild(header);"
		  "let next=root.nextSibling;"
		  "while(next && next.tagName!=='H3' && next.tagName!=='BUTTON' && !(next.tagName==='DIV' && next.className==='overlay')){"
			"let curr=next;"
			"next=next.nextSibling;"
			"root.appendChild(curr);"
		  "}"
		"}"

		"const f=(k)=>{"
		  "var e=document.getElementsByName('KrX_MQTT_Commander:'+k);"
		  "if(e.length===0)return null;"
		  "for(var i=0;i<e.length;i++){"
			"if(e[i].type!=='hidden')return e[i];"
		  "}"
		  "return e[0];"
		"};"

		"const isJsonContent=(val)=>{"
		  "if(!val || val.length===0)return false;"
		  "val=val.trim();"
		  "return val.startsWith('{') && val.includes('sensors');"
		"};"

		"const createRow=(input,labelText,convertToTextarea)=>{"
		  "if(!input)return null;"
		  "const row=document.createElement('div');"
		  "row.className='he-row';"
		  "const label=document.createElement('div');"
		  "label.className='he-label';"
		  "label.textContent=labelText;"
		  "const inputContainer=document.createElement('div');"
		  "inputContainer.className='he-input';"
		  
		  // Convert text input to textarea if requested
		  "if(convertToTextarea && input.tagName==='INPUT' && input.type==='text'){"
			"const textarea=document.createElement('textarea');"
			"textarea.name=input.name;"
			"textarea.value=input.value;"
			"textarea.rows=typeof convertToTextarea === 'number' ? convertToTextarea : 3;"
			"textarea.className='he-textarea';"
			"textarea.onchange=input.onchange;"
			"textarea.oninput=input.oninput;"
			"input=textarea;"
		  "}"
		  
		  "let sib=input.previousSibling;"
		  "while(sib){"
			"if(sib.type==='hidden' && sib.name===input.name){"
			  "inputContainer.appendChild(sib);"
			  "break;"
			"}"
			"if(sib.tagName==='DIV' || sib.tagName==='H3')break;"
			"sib=sib.previousSibling;"
		  "}"
		  "inputContainer.appendChild(input);"
		  "row.appendChild(label);"
		  "row.appendChild(inputContainer);"
		  "return row;"
		"};"

		"const createGroup=(title)=>{"
		  "const group=document.createElement('div');"
		  "group.className='he-group';"
		  "const groupTitle=document.createElement('div');"
		  "groupTitle.className='he-group-title';"
		  "groupTitle.textContent=title;"
		  "group.appendChild(groupTitle);"
		  "return group;"
		"};"

		"const root=document.getElementById('hour-effect-root');"
		"if(root){"
		  "const g1=createGroup('Main Settings');"
		  "const en=f('Enable Usermod');"
		  "if(en){"
			"const row=createRow(en,'Enable Usermod');"
			"if(row)g1.appendChild(row);"
		  "}"
		  "root.appendChild(g1);"

		  "const g2=createGroup('Effect Settings');"
		  "const blink=f('3d finished blink');"
		  "const hour=f('Effect every Hour');"
		  "if(blink){"
			"const row=createRow(blink,'3D Printer Finished Blink');"
			"if(row)g2.appendChild(row);"
		  "}"
		  "if(hour){"
			"const row=createRow(hour,'Effect Every Hour');"
			"if(row)g2.appendChild(row);"
		  "}"
		  "root.appendChild(g2);"

		  "const g3=createGroup('Night Mode Settings');"
		  "const pwrOff=f('Enable Power off when NightMode starts');"
		  "const pwrOn=f('Enable Power on when NightMode finished');"
		  "const presNight=f('Enable Presence during NightMode');"
		  "const nmOn=f('NightMode On at');"
		  "const nmOff=f('NightMode Off at');"
		  "if(pwrOff){"
			"const row=createRow(pwrOff,'Power Off When NightMode Starts');"
			"if(row)g3.appendChild(row);"
		  "}"
		  "if(pwrOn){"
			"const row=createRow(pwrOn,'Power On When NightMode Ends');"
			"if(row)g3.appendChild(row);"
		  "}"
		  "if(presNight){"
			"const row=createRow(presNight,'Enable Presence During NightMode');"
			"if(row)g3.appendChild(row);"
		  "}"
		  "if(nmOn){"
			"const row=createRow(nmOn,'NightMode On At');"
			"if(row)g3.appendChild(row);"
		  "}"
		  "if(nmOff){"
			"const row=createRow(nmOff,'NightMode Off At');"
			"if(row)g3.appendChild(row);"
		  "}"
		  "root.appendChild(g3);"

		  "const g4=createGroup('MQTT Sensor Settings');"
		  "const mqttPres=f('Mqtt Presence Path');"
		  "const mqttBlock=f('Block Presence On/Off');"
		  "const mqttLux=f('Mqtt Lux/Illuminance Path');"
		  "const luxThresh=f('Lux Threshold');"
		  "const trigMode=f('Trigger Mode');"
		  "const mqttPresAdv=f('MQTT Presence JSON Config');"
		  "const mqttLuxAdv=f('MQTT Lux JSON Config');"
		  "const mqttBlockAdv=f('MQTT Blocker JSON Config');"
		  
		  // Determine if presence is simple or advanced based on content
		  "if(mqttPres){"
			"const val=mqttPres.value||'';"
			"const isJson=isJsonContent(val);"
			"const labelText=isJson?'MQTT Presence Config (Advanced JSON)':'MQTT Presence Topic (Simple)';"
			"const row=createRow(mqttPres,labelText,5);"
			"if(row)g4.appendChild(row);"
		  "}"
		  
		  // Hide the dedicated advanced field if it exists (we're using the main field now)
		  "if(mqttPresAdv){"
			"mqttPresAdv.style.display='none';"
			"const presAdvRow=mqttPresAdv.closest('.he-row');"
			"if(presAdvRow)presAdvRow.style.display='none';"
		  "}"
		  
		  // Determine if blocker is simple or advanced based on content
		  "if(mqttBlock){"
			"const val=mqttBlock.value||'';"
			"const isJson=isJsonContent(val);"
			"const labelText=isJson?'MQTT Blocker Config (Advanced JSON)':'MQTT Presence Blocker Topic (Simple)';"
			"const row=createRow(mqttBlock,labelText,5);"
			"if(row)g4.appendChild(row);"
		  "}"
		  
		  // Hide the dedicated advanced field
		  "if(mqttBlockAdv){"
			"mqttBlockAdv.style.display='none';"
			"const blockAdvRow=mqttBlockAdv.closest('.he-row');"
			"if(blockAdvRow)blockAdvRow.style.display='none';"
		  "}"
		  
		  // Determine if lux is simple or advanced based on content
		  "if(mqttLux){"
			"const val=mqttLux.value||'';"
			"const isJson=isJsonContent(val);"
			"const labelText=isJson?'MQTT Lux Config (Advanced JSON)':'MQTT Lux/Illuminance Topic (Simple)';"
			"const row=createRow(mqttLux,labelText,5);"
			"if(row)g4.appendChild(row);"
		  "}"
		  
		  // Hide the dedicated advanced field
		  "if(mqttLuxAdv){"
			"mqttLuxAdv.style.display='none';"
			"const luxAdvRow=mqttLuxAdv.closest('.he-row');"
			"if(luxAdvRow)luxAdvRow.style.display='none';"
		  "}"
		  
		  "if(luxThresh){"
			"const row=createRow(luxThresh,'Lux Threshold');"
			"if(row)g4.appendChild(row);"
		  "}"
		  "if(trigMode){"
			"const row=createRow(trigMode,'Trigger Mode');"
			"if(row)g4.appendChild(row);"
		  "}"
		  "root.appendChild(g4);"

		  "const g5=createGroup('Physical Sensor Settings');"
		  // Pin dropdowns have [] in the name, so we need to search differently
		  "let sPin=null;"
		  "const allSelects=document.querySelectorAll('select');"		  
		  "allSelects.forEach(sel=>{"
			"if(sel.name && sel.name.includes('KrX_MQTT_Commander:sensor_pin')){"
			  "sPin=sel;"
			"}"
		  "});"
		  "const sActive=f('Sensor Active Low');"
		  "if(sPin){"
			"const row=createRow(sPin,'Sensor Pin');"
			"if(row)g5.appendChild(row);"
		  "}"
		  "if(sActive){"
			"const row=createRow(sActive,'Sensor Active Low');"
			"if(row)g5.appendChild(row);"
		  "}"
		  "root.appendChild(g5);"

		  "const g6=createGroup('MQTT Lamp Control');"
		  "const mqttLamps=f('MQTT Lamps (comma-separated)');"
		  "if(mqttLamps){"
			"const row=createRow(mqttLamps,'MQTT Lamp Topics (comma-separated)',3);"
			"if(row)g6.appendChild(row);"
		  "}"
		  "root.appendChild(g6);"

		  "const children=Array.from(root.children);"
		  "children.forEach(c=>{"
			"if(c.tagName!=='H3' && !c.classList.contains('he-group')){"
			  "c.remove();"
			"}"
		  "});"
		  "let n=root.firstChild;"
		  "while(n){"
			"let next=n.nextSibling;"
			"if(n.nodeType===3)n.remove();"
			"n=next;"
		  "}"
		  "let prev=root.previousElementSibling;"
		  "if(prev && prev.tagName==='HR')prev.remove();"
		"}"
	  "}catch(e){"
		"console.error('Hour Effect GUI Error:',e);"
	  "}"
	"}setTimeout(setupHourEffect,100);"));

  // Dropdown for NightMode On time selection.
  oappend(SET_F("dd=addDropdown('KrX_MQTT_Commander','NightMode On at');"));
  for (int i = 0; i < 24; i++) {
    String hourLabel = (i < 10 ? "0" : "") + String(i);
    oappend(String("addOption(dd,'" + hourLabel + ":00'," + String(i) + (i == NightModeOn ? ", true);" : ");")).c_str());
  }
  
  // Dropdown for NightMode Off time selection.
  oappend(SET_F("dt=addDropdown('KrX_MQTT_Commander','NightMode Off at');"));
  for (int i = 0; i < 24; i++) {
    String hourLabel = (i < 10 ? "0" : "") + String(i);
    oappend(String("addOption(dt,'" + hourLabel + ":00'," + String(i) + (i == NightModeOff ? ", true);" : ");")).c_str());
  }
  
  // Dropdown for Trigger Mode selection.
  oappend(SET_F("tm=addDropdown('KrX_MQTT_Commander','Trigger Mode');"));
  oappend(String("addOption(tm,'None',0" + String(triggerMode == 0 ? ", true);" : ");")).c_str());
  oappend(String("addOption(tm,'Presence Only',1" + String(triggerMode == 1 ? ", true);" : ");")).c_str());
  oappend(String("addOption(tm,'Lux Only (no turn OFF)',2" + String(triggerMode == 2 ? ", true);" : ");")).c_str());
  oappend(String("addOption(tm,'Lux Only (with turn OFF)',3" + String(triggerMode == 3 ? ", true);" : ");")).c_str());
  oappend(String("addOption(tm,'Lux+Presence (OFF when presence false)',4" + String(triggerMode == 4 ? ", true);" : ");")).c_str());
  oappend(String("addOption(tm,'Lux+Presence (OFF when both false)',5" + String(triggerMode == 5 ? ", true);" : ");")).c_str());
}

///////////////////////////////////////////////////////////////////////////////
// Destructor: Clean up dynamically allocated memory.
///////////////////////////////////////////////////////////////////////////////
UsermodHourEffect::~UsermodHourEffect() {
  _logUsermodHourEffect("[DESTRUCTOR] Called: cleaning up dynamically allocated memory");
  
  if (segmentBackup != nullptr) {
    _logUsermodHourEffect("[DESTRUCTOR] Deleting segment backup array");
    delete[] segmentBackup;
    segmentBackup = nullptr;
    _logUsermodHourEffect("[DESTRUCTOR] Segment backup array deleted");
  } else {
    _logUsermodHourEffect("[DESTRUCTOR] No segment backup to delete");
  }
  
  // Deallocate input pin
  if (inputPin[0] >= 0 && inputPinAllocated) {
    _logUsermodHourEffect("[DESTRUCTOR] Deallocating input pin %d", inputPin[0]);
    oldInputPin = inputPin[0];
    deallocateInputPin();
    _logUsermodHourEffect("[DESTRUCTOR] Input pin deallocation complete");
  } else {
    _logUsermodHourEffect("[DESTRUCTOR] No input pin to deallocate (pin=%d, allocated=%d)", inputPin[0], inputPinAllocated);
  }
  
  _logUsermodHourEffect("[DESTRUCTOR] Cleanup completed");
}

///////////////////////////////////////////////////////////////////////////////
// getId: Returns the unique usermod ID.
///////////////////////////////////////////////////////////////////////////////
uint16_t UsermodHourEffect::getId() {
  return USERMOD_ID_HOUR_EFFECT;
}

///////////////////////////////////////////////////////////////////////////////
// Static constant definitions (stored in PROGMEM).
///////////////////////////////////////////////////////////////////////////////
const char UsermodHourEffect::_name[]                      PROGMEM = "KrX_MQTT_Commander";
const char UsermodHourEffect::_enabledUsermod[]            PROGMEM = "Enable Usermod";
const char UsermodHourEffect::_enabled3DBlink[]            PROGMEM = "3d finished blink";
const char UsermodHourEffect::_enabledHourEffect[]         PROGMEM = "Effect every Hour";
const char UsermodHourEffect::_enableNightModePowerOff[]   PROGMEM = "Enable Power off when NightMode starts";
const char UsermodHourEffect::_enabledNightModePowerOn[]   PROGMEM = "Enable Power on when NightMode finished";
const char UsermodHourEffect::_enablePresenceDuringNightMode[] PROGMEM = "Enable Presence during NightMode";
const char UsermodHourEffect::_NightModeOn[]               PROGMEM = "NightMode On at";
const char UsermodHourEffect::_NightModeOff[]              PROGMEM = "NightMode Off at";
const char UsermodHourEffect::_MqttPresence[]              PROGMEM = "Mqtt Presence Path";
const char UsermodHourEffect::_MqttPresenceBlocker[]       PROGMEM = "Block Presence On/Off";
const char UsermodHourEffect::_MqttLux[]                   PROGMEM = "Mqtt Lux/Illuminance Path";
const char UsermodHourEffect::_MqttPresenceAdvanced[]	   PROGMEM = "MQTT Presence JSON Config";
const char UsermodHourEffect::_MqttLuxAdvanced[]      	   PROGMEM = "MQTT Lux JSON Config";
const char UsermodHourEffect::_MqttBlockerAdvanced[]  	   PROGMEM = "MQTT Blocker JSON Config";
const char UsermodHourEffect::_luxThreshold[]              PROGMEM = "Lux Threshold";
const char UsermodHourEffect::_triggerMode[]               PROGMEM = "Trigger Mode";
const char UsermodHourEffect::_inputPin[]                  PROGMEM = "sensor_pin";
const char UsermodHourEffect::_inputActiveLow[]            PROGMEM = "Sensor Active Low";
const char UsermodHourEffect::_MqttLamps[]                 PROGMEM = "MQTT Lamps (comma-separated)";

static UsermodHourEffect hour_effect_v2;
REGISTER_USERMOD(hour_effect_v2);


// ============================================================================
// EXAMPLE JSON CONFIGURATIONS (for documentation/testing)
// ============================================================================

/*

EXAMPLE 1: Presence with OR logic (any sensor triggers presence)
{
  "sensors": [
    {
      "id": "kitchen_wave",
      "topic": "zigbee2mqtt/K�che mWave",
      "path": "presence",
      "on_values": "on,true,1"
    },
    {
      "id": "dinnerroom_wave",
      "topic": "zigbee2mqtt/Esszimmer mWave G24",
      "path": "presence",
      "on_values": "on,true,1"
    }
  ],
  "logic_true": "kitchen_wave",
  "logic_false": "!kitchen_wave AND !dinnerroom_wave"
}

EXAMPLE 2: Presence with complex logic
{
  "sensors": [
    {
      "id": "motion1",
      "topic": "zigbee2mqtt/Motion1",
      "path": "",
      "on_values": "on,true"
    },
    {
      "id": "motion2",
      "topic": "zigbee2mqtt/Motion2",
      "path": "occupancy",
      "on_values": "true"
    },
    {
      "id": "door",
      "topic": "zigbee2mqtt/Door",
      "path": "contact",
      "on_values": "false"
    }
  ],
  "logic_true": "(motion1 OR motion2) AND !door",
  "logic_false": "!motion1 AND !motion2"
}

EXAMPLE 3: Lux sensor
{
  "sensors": [
    {
      "id": "kitchen_lux",
      "topic": "zigbee2mqtt/Kitchen/sensor",
      "path": "illuminance"
    }
  ],
  "logic_true": "kitchen_lux"
}

EXAMPLE 4: Blocker with multiple conditions
{
  "sensors": [
    {
      "id": "manual_switch",
      "topic": "zigbee2mqtt/K�che Licht",
      "path": "state_center",
      "on_values": "on,true,1"
    }
  ],
  "logic_true": "manual_switch",
  "logic_false": "!manual_switch"
}

*/
