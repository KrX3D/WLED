#pragma once

#include "wled.h"

// logging macro:
#define _logUsermodB_R_T(fmt, ...) \
  DEBUG_PRINTF("[ButtonRelayToggle] " fmt "\n", ##__VA_ARGS__)

// Default configuration values (can be overridden via build/platform defines)
#ifndef BUTTON_RELAY_TOGGLE_ENABLED
  #define BUTTON_RELAY_TOGGLE_ENABLED false
#endif

#ifndef BUTTON_RELAY_TOGGLE_HA_DISCOVERY
  #define BUTTON_RELAY_TOGGLE_HA_DISCOVERY false
#endif

// Group 1
#ifndef BUTTON_1_PIN
  #define BUTTON_1_PIN -1
#endif
#ifndef BUTTON_1_PULLUP
  #define BUTTON_1_PULLUP true
#endif
#ifndef RELAY_1_PIN
  #define RELAY_1_PIN -1
#endif
#ifndef RELAY_1_ACTIVE_LOW
  #define RELAY_1_ACTIVE_LOW false
#endif

// Group 2
#ifndef BUTTON_2_PIN
  #define BUTTON_2_PIN -1
#endif
#ifndef BUTTON_2_PULLUP
  #define BUTTON_2_PULLUP true
#endif
#ifndef RELAY_2_PIN
  #define RELAY_2_PIN -1
#endif
#ifndef RELAY_2_ACTIVE_LOW
  #define RELAY_2_ACTIVE_LOW false
#endif

// Group 3
#ifndef BUTTON_3_PIN
  #define BUTTON_3_PIN -1
#endif
#ifndef BUTTON_3_PULLUP
  #define BUTTON_3_PULLUP true
#endif
#ifndef RELAY_3_PIN
  #define RELAY_3_PIN -1
#endif
#ifndef RELAY_3_ACTIVE_LOW
  #define RELAY_3_ACTIVE_LOW false
#endif

// Group 4
#ifndef BUTTON_4_PIN
  #define BUTTON_4_PIN -1
#endif
#ifndef BUTTON_4_PULLUP
  #define BUTTON_4_PULLUP true
#endif
#ifndef RELAY_4_PIN
  #define RELAY_4_PIN -1
#endif
#ifndef RELAY_4_ACTIVE_LOW
  #define RELAY_4_ACTIVE_LOW false
#endif

//------------------------------------------------------------------------------
// Usermod: Button Relay Toggle with Home Assistant discovery
//
// This usermod allows you to configure up to 4 button/relay groups. A short 
// press (after debounce) toggles the associated relay. Additionally, MQTT is 
// used for publishing button and relay state changes and for incremental 
// Home Assistant discovery.
//------------------------------------------------------------------------------

class UsermodButtonRelayToggle : public Usermod {
  private:
    bool initDone = false;

    // --- Usermod Settings (modifiable via config) ---
    bool enabled     = BUTTON_RELAY_TOGGLE_ENABLED;
    bool haDiscovery = BUTTON_RELAY_TOGGLE_HA_DISCOVERY;

    // --- Constants ---
    #define DEBOUNCE_MS 50             // milliseconds for stable reading
    #define TOGGLE_LOCKOUT_MS 300      // ignore further toggles for this long after a relay toggle

    // --- Button state arrays (one per group) ---
    // Debounce + lockout per-button state
    bool _lastRaw[4] = { false, false, false, false };           // raw last read value (true==pressed)
    unsigned long _lastDebounceTime[4] = { 0, 0, 0, 0 };         // last time raw changed
    bool _stableState[4] = { false, false, false, false };       // debounced stable state (true==pressed)
    unsigned long _lastToggleTime[4] = { 0, 0, 0, 0 };           // last time we toggled this relay

	#define STARTUP_IGNORE_MS 1000   // ignore button events for this many ms after setup; tweak as needed

	// startup suppression marker
	unsigned long _suppressButtonsUntil = 0;

    // --- Consolidated arrays for per-group configuration ---
    const char* _groups[4]  = { "group_1", "group_2", "group_3", "group_4" };
    int  _buttonPins[4]     = { BUTTON_1_PIN, BUTTON_2_PIN, BUTTON_3_PIN, BUTTON_4_PIN };
    bool _pullUps[4]        = { BUTTON_1_PULLUP, BUTTON_2_PULLUP, BUTTON_3_PULLUP, BUTTON_4_PULLUP };
    int  _relayPins[4]      = { RELAY_1_PIN, RELAY_2_PIN, RELAY_3_PIN, RELAY_4_PIN };
    bool _activeLow[4]      = { RELAY_1_ACTIVE_LOW, RELAY_2_ACTIVE_LOW, RELAY_3_ACTIVE_LOW, RELAY_4_ACTIVE_LOW };

    int oldButtonPins[4];
    int oldRelayPins[4];

    // --- Home Assistant discovery state ---
    bool triggerHaDiscovery   		= false;
    int discoveryStep         		= 0;
    unsigned long lastDiscoveryTime = 0;

  public:
    // --- Interface to enable/disable this usermod ---
    inline void enable(bool e) { enabled = e; }
    inline bool isEnabled() { return enabled; }

    //----------------------------------------------------------------------------  
	void deallocateAll() {
		if (!initDone) return;
		for (uint8_t i = 0; i < 4; i++) {
			//Deallocate Buttons
			if(oldButtonPins[i] != -1) {
				if (PinManager::isPinAllocated(oldButtonPins[i], PinOwner::UM_BUTTON_RELAY_TOGGLE)) {
					PinManager::deallocatePin(oldButtonPins[i], PinOwner::UM_BUTTON_RELAY_TOGGLE);

					if (PinManager::isPinOk(oldButtonPins[i])) {
						_logUsermodB_R_T("Button pin deallocated: %d", oldButtonPins[i]);
					} else {
						_logUsermodB_R_T("Failed to deallocate button pin: %d", oldButtonPins[i]);
					}
				} else {
					_logUsermodB_R_T("Button is not allocated but should be: %d", oldButtonPins[i]);
				}
			}

			//Deallocate Relays
			if(oldRelayPins[i] != -1) {
				if (PinManager::isPinAllocated(oldRelayPins[i], PinOwner::UM_BUTTON_RELAY_TOGGLE)) {
					PinManager::deallocatePin(oldRelayPins[i], PinOwner::UM_BUTTON_RELAY_TOGGLE);
					
					if (PinManager::isPinOk(oldRelayPins[i])) {
						_logUsermodB_R_T("Relay pin deallocated: %d", oldRelayPins[i]);
					} else {
						_logUsermodB_R_T("Failed to deallocate relay pin: %d", oldRelayPins[i]);
					}
				} else {
					_logUsermodB_R_T("Relay is not allocated but should be: %d", oldRelayPins[i]);
				}
			}
		}	
		_logUsermodB_R_T("All pins deallocated.");
	}

    ////////////////////////////////////////////////////////////////////////////////
    // Setup: initialize buttons and relays
    ////////////////////////////////////////////////////////////////////////////////
	void setup() override {
	  // Initialize all configured pins
	  _logUsermodB_R_T("Setup starting (enabled=%s, HA discovery=%s)", enabled ? "true" : "false", haDiscovery ? "true" : "false");

	  for (uint8_t i = 0; i < 4; i++) {
	    // ---------------- BUTTON ----------------
	    if (_buttonPins[i] != -1 && PinManager::allocatePin(_buttonPins[i], false, PinOwner::UM_BUTTON_RELAY_TOGGLE)) {
          pinMode(_buttonPins[i], _pullUps[i] ? INPUT_PULLUP : INPUT);
          _logUsermodB_R_T("Initialized %s button pin %d (pullup=%s)", _groups[i], _buttonPins[i], _pullUps[i] ? "Y" : "N");

          // --- INIT debounced state for this button ---
          bool phys = isButtonPhysicallyPressed(i);
	      _lastRaw[i]          = phys;
	      _stableState[i]      = phys;
	      _lastDebounceTime[i] = millis();
	      _lastToggleTime[i]   = 0; // temporary, will be seeded globally below
	      _logUsermodB_R_T("Button %u initial physical=%d, stable=%d", i + 1, phys, _stableState[i]);
	    }

	    // ---------------- RELAY ----------------
	    if (_relayPins[i] != -1 && PinManager::allocatePin(_relayPins[i], true, PinOwner::UM_BUTTON_RELAY_TOGGLE)) {
	      pinMode(_relayPins[i], OUTPUT);

          // set the relay to a known OFF state at startup to avoid powering LEDs unintentionally
          // If activeLow == true and logical OFF -> physical HIGH, else physical LOW.
          digitalWrite(_relayPins[i], _activeLow[i] ? HIGH : LOW);
          _logUsermodB_R_T("Initialized relay %u pin %d -> OFF (activeLow=%d)", i + 1, _relayPins[i], _activeLow[i]);
		}
		// Save the initial configuration for later comparison
		oldButtonPins[i] = _buttonPins[i];
		oldRelayPins[i]  = _relayPins[i];
	  }

	  // ------------------------------------------------------------------
	  // STARTUP SUPPRESSION & LOCKOUT SEED  (ADDED FIX)
	  // ------------------------------------------------------------------
	  unsigned long now = millis();

	  for (uint8_t i = 0; i < 4; i++) {
	    _lastToggleTime[i] = now;  // prevent immediate toggles after boot
	  }

	  _suppressButtonsUntil = now + STARTUP_IGNORE_MS;
	
	  _logUsermodB_R_T("Startup: suppressing button events for %u ms (until %lu)", STARTUP_IGNORE_MS, _suppressButtonsUntil);
	  // ------------------------------------------------------------------

	  initDone = true;
	  _logUsermodB_R_T("Configuration: DEBOUNCE_MS=%d, TOGGLE_LOCKOUT_MS=%d", DEBOUNCE_MS, TOGGLE_LOCKOUT_MS);

	  for (uint8_t i = 0; i < 4; i++) {
	    _logUsermodB_R_T("Group %u: buttonPin=%d pullup=%s relayPin=%d activeLow=%s", i + 1, _buttonPins[i], _pullUps[i] ? "Y" : "N", _relayPins[i], _activeLow[i] ? "Y" : "N");
	  }
	  _logUsermodB_R_T("ButtonRelayToggle usermod setup completed.");
	}

    ////////////////////////////////////////////////////////////////////////////////
    // Loop: check each button and process Home Assistant discovery if needed
    ////////////////////////////////////////////////////////////////////////////////
    void loop() override {
      if(!enabled) return;
      for(uint8_t i = 0; i < 4; i++){
        if(_buttonPins[i]!=-1) handleButtonPress(i);
      }
      #ifndef WLED_DISABLE_MQTT
      handleHomeAssistantDiscovery();
      #endif
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Return raw button reading (true when the button is physically pressed)
    // Respects _pullUps[] configuration: if pullup=true then pressed means LOW, else pressed means HIGH.
    ////////////////////////////////////////////////////////////////////////////////
    bool isButtonPhysicallyPressed(uint8_t index) {
      if (_buttonPins[index] == -1) return false;
      int raw = digitalRead(_buttonPins[index]);
      return _pullUps[index] ? (raw == LOW) : (raw == HIGH);
    }
    
    ////////////////////////////////////////////////////////////////////////////////
    // Button press handling (short press toggles relay)
    ////////////////////////////////////////////////////////////////////////////////
    void handleButtonPress(uint8_t index) {
      if (_buttonPins[index] == -1) return;

	  // ignore startup transients
	  if (millis() < _suppressButtonsUntil) return;

      unsigned long now = millis();
      bool physPressed = isButtonPhysicallyPressed(index); // current raw physical reading

      // If raw changed, reset debounce timer
      if (physPressed != _lastRaw[index]) {
        _lastDebounceTime[index] = now;
        _lastRaw[index] = physPressed;
      }

      // If raw is stable for DEBOUNCE_MS and differs from stable value, accept change
      if ((now - _lastDebounceTime[index]) >= DEBOUNCE_MS && physPressed != _stableState[index]) {
        bool previousStable = _stableState[index];
        _stableState[index] = physPressed; // new stable state accepted

        if (_stableState[index]) {
          // Transition: RELEASED -> PRESSED
          _logUsermodB_R_T("Button %u pressed (stable).", index + 1);
          // (original behavior did not publish on press; keeping it silent to match original)
        } else {
          // Transition: PRESSED -> RELEASED
          _logUsermodB_R_T("Button %u released (stable).", index + 1);

          // Enforce toggle lockout to avoid double toggles from bounce or crosstalk
          if ((now - _lastToggleTime[index]) < TOGGLE_LOCKOUT_MS) {
            _logUsermodB_R_T("Button %u toggle ignored due to lockout (%lums since last toggle).", index+1, now - _lastToggleTime[index]);
            return;
          }

          // On a released (short) press -> toggle relay (matches original behavior)
          #ifndef WLED_DISABLE_MQTT
          publishMqtt("Button", index + 1, true); // publish RELEASED as original code did (state=false)
          #endif
          toggleRelay(index);
          _lastToggleTime[index] = now;
        }
      }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Toggle relay state (taking active low into account) and publish state.
    ////////////////////////////////////////////////////////////////////////////////
	void toggleRelay(uint8_t index) {
	  if (_relayPins[index] == -1) return;

	  unsigned long now = millis();

	  // read current physical state
	  bool phys = digitalRead(_relayPins[index]);
	  bool currentLogical = _activeLow[index] ? !phys : phys;
	  bool newLogical = !currentLogical;

	  // write new state respecting activeLow
	  digitalWrite(_relayPins[index], _activeLow[index] ? !newLogical : newLogical);

	  // record toggle time (important!)
	  _lastToggleTime[index] = now;

	  _logUsermodB_R_T("Relay %u toggled: %d -> %d (pin %d, activeLow=%d) at %lu ms", index + 1, currentLogical, newLogical, _relayPins[index], _activeLow[index], now);

	  #ifndef WLED_DISABLE_MQTT
	  publishMqtt("Relay", index + 1, newLogical);
	  #endif
	}

    ////////////////////////////////////////////////////////////////////////////////
    // Home Assistant discovery handling (incremental steps to avoid overload)
    ////////////////////////////////////////////////////////////////////////////////
    void handleHomeAssistantDiscovery(){
		if(!triggerHaDiscovery) return;
		if(millis() - lastDiscoveryTime < 1000) return;

		lastDiscoveryTime = millis();
		
		if (!WLED_MQTT_CONNECTED) {
			_logUsermodB_R_T("MQTT disconnected; skipping HA discovery step %d", discoveryStep);
			return;
		}

		char topic[64]; //Topic size: 51, Topic buffer size: 64 - for wled/Netzwerkschrank-Fan/Button_Relay_Toggle/config
		snprintf_P(topic, sizeof(topic), "%s/Button_Relay_Toggle/config", mqttDeviceTopic);

		_logUsermodB_R_T("Home Assistant discovery step %d", discoveryStep);

		switch(discoveryStep++){
	        case 0: HomeAssistantDiscoveryOptions(topic);       break;
	        case 1: HomeAssistantDiscoveryButtonSensor(topic);  break;
	        case 2: HomeAssistantDiscoveryRelaySensor(topic);   break;
	        case 3: HomeAssistantDiscoveryButtonSensorState();  break;
	        case 4: HomeAssistantDiscoveryButtonRelaySwitch();  break;
	        default:
	          _logUsermodB_R_T("Home Assistant discovery completed");
			  triggerHaDiscovery = false;
			  discoveryStep = 0;
		}
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Home Assistant Discovery: Publish/Remove options sensors.
    ////////////////////////////////////////////////////////////////////////////////
    void HomeAssistantDiscoveryOptions(const char* topic){
      if(haDiscovery){
        _logUsermodB_R_T("Enabling Home Assistant discovery...");
        mqttCreateHassSensor("Enabled", topic, "enabled", "sensor", "diagnostic");
        mqttCreateHassSensor("HomeAssistant Discovery", topic, "homeassistant_discovery", "sensor", "diagnostic");
      } else {
        _logUsermodB_R_T("Home Assistant discovery disabled. Removing sensors...");
        mqttRemoveHassSensor("Enabled", "sensor");
        mqttRemoveHassSensor("HomeAssistant Discovery", "sensor");
      }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Home Assistant Discovery: Button sensor configuration.
    ////////////////////////////////////////////////////////////////////////////////
    void HomeAssistantDiscoveryButtonSensor(const char* topic){
      for (uint8_t i = 0; i < 4; i++) {
        // Button pin sensor
        processHassSensor(_buttonPins[i] != -1, haDiscovery, i, "Button", "pin", topic, "button_pin", "sensor", "diagnostic");
        // Button pullup sensor
        processHassSensor(_buttonPins[i] != -1, haDiscovery, i, "Button", "pullup", topic, "button_pullup", "sensor", "diagnostic");
      }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Home Assistant Discovery: Relay sensor configuration.
    ////////////////////////////////////////////////////////////////////////////////
    void HomeAssistantDiscoveryRelaySensor(const char* topic){
      for (uint8_t i = 0; i < 4; i++) {
        // Relay pin sensor
        processHassSensor(_relayPins[i] != -1, haDiscovery, i, "Relay", "pin", topic, "relay_pin", "sensor", "diagnostic");
        // Relay active low sensor
        processHassSensor(_relayPins[i] != -1, haDiscovery, i, "Relay", "active low", topic, "relay_active_low", "sensor", "diagnostic");
      }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Home Assistant Discovery: Button state sensor (publishes state topic).
    ////////////////////////////////////////////////////////////////////////////////
    void HomeAssistantDiscoveryButtonSensorState(){
      for (uint8_t i = 0; i < 4; i++) {
        processHassStateSensor(_buttonPins[i] != -1, haDiscovery, i, "Button", "state", "Button", "sensor");
      }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Home Assistant Discovery: Relay switch (allows toggling via HA).
    ////////////////////////////////////////////////////////////////////////////////
    void HomeAssistantDiscoveryButtonRelaySwitch() {
      for (uint8_t i = 0; i < 4; i++) {
        processHassStateSensor(_relayPins[i] != -1, haDiscovery, i, "Relay", "state", "Relay", "switch");
      }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Helper: Process a sensor for discovery with a configuration key.
    ////////////////////////////////////////////////////////////////////////////////
    void processHassSensor(bool validPin, bool haEnabled, uint8_t index, const char* baseName, const char* sensorSuffix,
                           const char* topic, const char* jsonKey, const char* sensorType, const char* entityCategory) {
      String sensorName = String(baseName) + " " + String(index + 1) + " " + sensorSuffix;
      String configKey = String("group_") + String(index + 1) + "." + jsonKey;
      if (validPin && haEnabled) {
        _logUsermodB_R_T("Creating HA sensor: %s", sensorName.c_str());
        mqttCreateHassSensor(sensorName, topic, configKey, sensorType, entityCategory);
      } else {
        _logUsermodB_R_T("Removing HA sensor: %s", sensorName.c_str());
        mqttRemoveHassSensor(sensorName, sensorType);
      }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Helper: Process a state sensor for discovery (no configuration key)
    ////////////////////////////////////////////////////////////////////////////////
    void processHassStateSensor(bool validPin, bool haEnabled, uint8_t index, const char* baseName, const char* stateSuffix,
                                const char* subTopic, const char* sensorType) {
      char topic[128];
      snprintf_P(topic, sizeof(topic), "%s/Button_Relay_Toggle/group_%d/%s", mqttDeviceTopic, index + 1, subTopic);
      String sensorName = String(baseName) + " " + String(index + 1) + " " + stateSuffix;
      if (validPin && haEnabled) {
        _logUsermodB_R_T("Creating HA state sensor: %s", sensorName.c_str());
        mqttCreateHassSensor(sensorName, topic, "", sensorType, "");
      } else {
        _logUsermodB_R_T("Removing HA state sensor: %s", sensorName.c_str());
        mqttRemoveHassSensor(sensorName, sensorType);
      }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Helper: Write one group's configuration into a JSON object.
    ////////////////////////////////////////////////////////////////////////////////
    void writeGroupConfig(JsonObject& group, uint8_t index) {
      group["button_pin"] = _buttonPins[index];
      group["button_pullup"] = _pullUps[index];
      group["relay_pin"] = _relayPins[index];
      group["relay_active_low"] = _activeLow[index];
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Helper: Read one group's configuration from a JSON object.
    ////////////////////////////////////////////////////////////////////////////////
    bool readGroupConfig(JsonObject& group, uint8_t index) {
      bool configComplete = true;
      configComplete &= getJsonValue(group["relay_pin"], _relayPins[index]);
      configComplete &= getJsonValue(group["button_pin"], _buttonPins[index]);
      configComplete &= getJsonValue(group["relay_active_low"], _activeLow[index]);
      configComplete &= getJsonValue(group["button_pullup"], _pullUps[index]);
      return configComplete;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Helper: Publish an MQTT message (with error checking)
    ////////////////////////////////////////////////////////////////////////////////
    void publishMqttMessage(const char* topic, const char* payload) {
      #ifndef WLED_DISABLE_MQTT
      if (WLED_MQTT_CONNECTED) {
        if (!mqtt->publish(topic, 0, true, payload, strlen(payload))) {
          _logUsermodB_R_T("MQTT publish failed for topic=%s", topic);
        } else {
          _logUsermodB_R_T("MQTT publish successful for topic: %s", topic);
        }
      } else {
        _logUsermodB_R_T("MQTT not connected; cannot publish");
      }
      #endif
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Create Home Assistant MQTT sensor configuration message.
    ////////////////////////////////////////////////////////////////////////////////
    void mqttCreateHassSensor(const String &name, const String &topic, const String &jsonKey, const String &sensorType, const String &entityCategory) {
      #ifndef WLED_DISABLE_MQTT
      if(!WLED_MQTT_CONNECTED) return;
      _logUsermodB_R_T("Creating Home Assistant MQTT sensor: %s",name.c_str());

      char configTopic[128];
      snprintf_P(configTopic, sizeof(configTopic), "homeassistant/%s/%s/%s/config",
             sensorType.c_str(), sanitizeMqttClientID(mqttClientID).c_str(), sanitizeName(name.c_str()));
      _logUsermodB_R_T("Config topic: %s", configTopic);

      String sanitizedName = name;
      sanitizedName.replace(' ', '-');

      StaticJsonDocument<1024> doc;
      doc["name"] = name;
      doc["stat_t"] = topic;
      doc["uniq_id"] = String(escapedMac.c_str()) + "_" + sanitizedName;

      if(entityCategory.length()) doc["ent_cat"] = entityCategory;
      if(jsonKey.length()) doc["val_tpl"] = String("{{ value_json.") + jsonKey + " }}";

      if(sensorType == "switch"){
        doc["cmd_t"] = String(topic) + "/set";
        doc["dev_cla"] = sensorType;
        doc["ret"] = true;
      }
      if(entityCategory != "diagnostic") doc["exp_aft"] = 1800;

      JsonObject device = doc.createNestedObject("device");
      device["name"] = serverDescription;

      String identifiers = "wled-sensor-" + sanitizeMqttClientID(mqttClientID);
      identifiers.toLowerCase();
      device["ids"] = identifiers;

      device["mf"] = WLED_BRAND;
      device["mdl"] = WLED_PRODUCT_NAME;
      device["sw"] = versionString;
      #ifdef ESP32
        device["hw"] = "esp32";
      #else
        device["hw"] = "esp8266";
      #endif
      JsonArray connections = device[F("cns")].createNestedArray();
      connections.add("mac");
      connections.add(WiFi.macAddress());

      char payload[1024];
      size_t payload_size = serializeJson(doc, payload, sizeof(payload));
      _logUsermodB_R_T("Payload %s", payload);
      _logUsermodB_R_T("Publishing to config topic: %s", configTopic);

      if (!mqtt->publish(configTopic, 0, true, payload, payload_size)) {
        _logUsermodB_R_T("MQTT publish failed for topic: %s", configTopic);
      } else {
        _logUsermodB_R_T("HA config published");
      }
      #endif
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Remove Home Assistant MQTT sensor configuration.
    ////////////////////////////////////////////////////////////////////////////////
    void mqttRemoveHassSensor(const String &name, const String &sensorType) {
      #ifndef WLED_DISABLE_MQTT
      if(!WLED_MQTT_CONNECTED) return;
      _logUsermodB_R_T("Removing Home Assistant MQTT sensor: %s", name.c_str());

      char removeTopic[128];
      snprintf_P(removeTopic, sizeof(removeTopic), "homeassistant/%s/%s/%s/config",
                 sensorType.c_str(), sanitizeMqttClientID(mqttClientID).c_str(), sanitizeName(name.c_str()));

      if(!mqtt->publish(removeTopic, 0, true, "", 0)){
        _logUsermodB_R_T("Failed to remove Home Assistant sensor: %s", removeTopic);
      } else {
        _logUsermodB_R_T("Home Assistant sensor removed successfully: %s", removeTopic);
      }
      #endif
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Sanitize the MQTT client ID (remove/replace invalid characters)
    ////////////////////////////////////////////////////////////////////////////////
    String sanitizeMqttClientID(const String &clientID){
      String sanitizedID;
      for (unsigned int i = 0; i < clientID.length(); i++) {
        char c = clientID[i];
        if (c == '\xC3' && i + 1 < clientID.length()) {
          if (clientID[i + 1] == '\xBC') { sanitizedID += "u"; i++; }
          else if (clientID[i + 1] == '\x9C') { sanitizedID += "U"; i++; }
          else if (clientID[i + 1] == '\xA4') { sanitizedID += "a"; i++; }
          else if (clientID[i + 1] == '\xC4') { sanitizedID += "A"; i++; }
          else if (clientID[i + 1] == '\xB6') { sanitizedID += "o"; i++; }
          else if (clientID[i + 1] == '\xD6') { sanitizedID += "O"; i++; }
          else if (clientID[i + 1] == '\x9F') { sanitizedID += "s"; i++; }
        }
        else if ((c >= 'a' && c <= 'z') ||
                 (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') ||
                 c == '-' || c == '_')
        {
          sanitizedID += c;
        } else {
          sanitizedID += '_';
        }
      }
      if (sanitizedID.length() > 23) {
        sanitizedID = sanitizedID.substring(0, 23);
      }
      return sanitizedID;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Sanitize a sensor name (replace spaces with dashes)
    ////////////////////////////////////////////////////////////////////////////////
    const char* sanitizeName(const char* name) {
      static char sanitized[64];
      strncpy(sanitized, name, sizeof(sanitized) - 1);
      sanitized[sizeof(sanitized) - 1] = '\0';
      for (size_t i = 0; i < strlen(sanitized); i++) {
        if (sanitized[i] == ' ')
          sanitized[i] = '-';
      }
      return sanitized;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Set up MQTT subscriptions for relays (so Home Assistant can toggle them)
    ////////////////////////////////////////////////////////////////////////////////
    void setupRelaySubscriptions(){
      #ifndef WLED_DISABLE_MQTT
		if(!WLED_MQTT_CONNECTED){
			_logUsermodB_R_T("MQTT disconnected; skipping subscriptions");
			return;
		}
		
		_logUsermodB_R_T("Setting up Relay subscriptions");
		
		char baseTopic[128];
		snprintf_P(baseTopic, sizeof(baseTopic), "%s/Button_Relay_Toggle", mqttDeviceTopic);
        _logUsermodB_R_T("Main topic base: %s", baseTopic);
		
		for (uint8_t i = 0; i < 4; i++) {
			String subscriptionTopic = String(baseTopic) + "/" + _groups[i] + "/Relay/set";
			_logUsermodB_R_T("Processing relay group %d, subscription topic: %s", i + 1, subscriptionTopic.c_str());

			if(_relayPins[i]==-1 || !haDiscovery || !enabled){
				if (_relayPins[i] == -1) {
				  _logUsermodB_R_T("Relay pin for group %d is unused; unsubscribing from topic: %s", i + 1, subscriptionTopic.c_str());
				} else if (!haDiscovery) {
				  _logUsermodB_R_T("Home Assistant discovery is disabled; unsubscribing from topic: %s", subscriptionTopic.c_str());
				} else if (!enabled) {
				  _logUsermodB_R_T("Usermod is disabled; unsubscribing from topic: %s", subscriptionTopic.c_str());
				}

				if(mqtt->unsubscribe(subscriptionTopic.c_str())){
				  _logUsermodB_R_T("Successfully unsubscribed from topic: %s", subscriptionTopic.c_str());
				} else {
				  _logUsermodB_R_T("Failed to unsubscribe from topic: %s", subscriptionTopic.c_str());
				}
			} else {
				bool relayState = _activeLow[i] ? !digitalRead(_relayPins[i]) : digitalRead(_relayPins[i]);
				_logUsermodB_R_T("Relay group %d current state: %s", i + 1, relayState ? "ON" : "OFF");
				publishMqtt("Relay", i + 1, relayState);
				if(mqtt->subscribe(subscriptionTopic.c_str(), 0)){
				  _logUsermodB_R_T("Successfully subscribed to topic: %s", subscriptionTopic.c_str());
				} else {
				  _logUsermodB_R_T("Failed to subscribe to topic: %s", subscriptionTopic.c_str());
				}
			}
		}
      #endif
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Add usermod state to JSON for API display
    ////////////////////////////////////////////////////////////////////////////////
    void addToJsonState(JsonObject& root) override {
      if (!initDone) return;
      JsonObject usermod = root["Button_Relay_Toggle"];
      if (usermod.isNull())
        usermod = root.createNestedObject("Button_Relay_Toggle");
      usermod["enabled"] = enabled;
      #ifndef WLED_DISABLE_MQTT
      usermod["ha_discovery"] = haDiscovery;
      #endif
      for (uint8_t i = 0; i < 4; i++) {
        JsonObject group = usermod.createNestedObject(_groups[i]);
        writeGroupConfig(group, i);
      }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Add usermod configuration to JSON for storage
    ////////////////////////////////////////////////////////////////////////////////
    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject("Button_Relay_Toggle");
      top["enabled"] = enabled;
      #ifndef WLED_DISABLE_MQTT
      top["ha_discovery"] = haDiscovery;
      #endif
      for (uint8_t i = 0; i < 4; i++) {
        JsonObject group = top.createNestedObject(_groups[i]);
        writeGroupConfig(group, i);
	    _logUsermodB_R_T("  Wrote group_%u config (button_pin=%d, pullup=%s, relay_pin=%d, activeLow=%s)",
	                          i+1,
	                          _buttonPins[i],
	                          _pullUps[i]   ? "Y" : "N",
	                          _relayPins[i],
	                          _activeLow[i] ? "Y" : "N");
      }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Pin initialization (for both buttons and relays)
    ////////////////////////////////////////////////////////////////////////////////
    void initializePin(int pin, bool isInput, bool usePullUp, bool activeLow, int group) {
	  if (pin == -1) return;
        if (isInput) {
          pinMode(pin, usePullUp ? INPUT_PULLUP : INPUT);
	    _logUsermodB_R_T("Initialized %s: Button pin %d (PullUp: %s)",
	                         _groups[group],
	                         pin,
	                         usePullUp ? "true" : "false");
	  } else {
          pinMode(pin, OUTPUT);
          digitalWrite(pin, activeLow ? HIGH : LOW);
	    _logUsermodB_R_T("Initialized %s: Relay pin %d (ActiveLow: %s)",
	                         _groups[group],
	                         pin,
	                         activeLow ? "true" : "false");
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Read usermod configuration from JSON
    ////////////////////////////////////////////////////////////////////////////////
    bool readFromConfig(JsonObject& root) override {
	  bool prevEnabled = enabled;  // Store previous state before overwriting
	  
	  JsonObject top = root["Button_Relay_Toggle"];
	  bool configComplete = !top.isNull();
	  configComplete &= getJsonValue(top["enabled"], enabled);
	  
	  #ifndef WLED_DISABLE_MQTT
	  configComplete &= getJsonValue(top["ha_discovery"], haDiscovery);
	  #endif

	  // Backup previous pin assignments for comparison
      memcpy(oldButtonPins, _buttonPins, sizeof(_buttonPins));
      memcpy(oldRelayPins,  _relayPins,  sizeof(_relayPins));

	  // Read new configuration
	  for (uint8_t i = 0; i < 4; i++) {
		JsonObject group = top[_groups[i]];
		configComplete &= readGroupConfig(group, i);
	  }

	  // Handle enabling/disabling and pin changes
	  if(!enabled){
		// Usermod was disabled, deallocate all pins
		if (prevEnabled) {
          _logUsermodB_R_T("Usermod disabled—deallocating all pins");
		  deallocateAll();
		} else {
          _logUsermodB_R_T("Usermod remains disabled");
        }
	  } else {
		// Usermod is enabled
		if (!prevEnabled) {
		  _logUsermodB_R_T("Usermod was enabled, allocating all pins.");
		  // Initialize all pins
		  for (uint8_t i = 0; i < 4; i++) {
			// For buttons
			if (_buttonPins[i] != -1) {
			  if (PinManager::allocatePin(_buttonPins[i], false, PinOwner::UM_BUTTON_RELAY_TOGGLE)) {
				pinMode(_buttonPins[i], _pullUps[i] ? INPUT_PULLUP : INPUT);
				_logUsermodB_R_T("Successfully allocated button pin %d with pull%s.", 
							  _buttonPins[i], _pullUps[i] ? "up" : "down");
			  } else {
				_logUsermodB_R_T("Failed to allocate button pin %d! Pin may be in use.", _buttonPins[i]);
			  }
			} else {
			  _logUsermodB_R_T("Button pin for group %d is disabled (-1).", i+1);
			}
			
			// For relays
			if (_relayPins[i] != -1) {
			  _logUsermodB_R_T("Allocating relay pin %d for group %d...", _relayPins[i], i+1);
			  if (PinManager::allocatePin(_relayPins[i], true, PinOwner::UM_BUTTON_RELAY_TOGGLE)) {
				pinMode(_relayPins[i], OUTPUT);
				digitalWrite(_relayPins[i], _activeLow[i] ? HIGH : LOW);
				_logUsermodB_R_T("Successfully allocated relay pin %d (Active %s).", 
							  _relayPins[i], _activeLow[i] ? "LOW" : "HIGH");
			  } else {
				_logUsermodB_R_T("Failed to allocate relay pin %d! Pin may be in use.", _relayPins[i]);
			  }
			} else {
			  _logUsermodB_R_T("Relay pin for group %d is disabled (-1).", i+1);
			}
		  }
		} else {
		  // Handle pin changes when already enabled
		  _logUsermodB_R_T("Usermod remains enabled. Checking for pin changes...");
		  for (uint8_t i = 0; i < 4; i++) {
			// For buttons:
			_logUsermodB_R_T("Group %d - Old button pin: %d, New button pin: %d", 
						  i+1, oldButtonPins[i], _buttonPins[i]);
						  
			if (oldButtonPins[i] != _buttonPins[i]) {
			  // Deallocate old pin if needed
			  if (oldButtonPins[i] != -1) {
				_logUsermodB_R_T("Deallocating old button pin %d...", oldButtonPins[i]);
				if (PinManager::isPinAllocated(oldButtonPins[i], PinOwner::UM_BUTTON_RELAY_TOGGLE)) {
				  PinManager::deallocatePin(oldButtonPins[i], PinOwner::UM_BUTTON_RELAY_TOGGLE);
				  _logUsermodB_R_T("Successfully deallocated old button pin %d.", oldButtonPins[i]);
				} else {
				  _logUsermodB_R_T("Old button pin %d was not allocated by this usermod.", oldButtonPins[i]);
				}
			  } else {
				_logUsermodB_R_T("Old button pin for group %d was disabled (-1). Nothing to deallocate.", i+1);
			  }
			  
			  // Allocate new pin if needed
			  if (_buttonPins[i] != -1) {
				_logUsermodB_R_T("Allocating new button pin %d...", _buttonPins[i]);
				if (PinManager::allocatePin(_buttonPins[i], false, PinOwner::UM_BUTTON_RELAY_TOGGLE)) {
				  pinMode(_buttonPins[i], _pullUps[i] ? INPUT_PULLUP : INPUT);
				  _logUsermodB_R_T("Successfully allocated new button pin %d with pull%s.", 
								_buttonPins[i], _pullUps[i] ? "up" : "down");
				} else {
				  _logUsermodB_R_T("Failed to allocate new button pin %d! Pin may be in use.", _buttonPins[i]);
				}
			  } else {
				_logUsermodB_R_T("New button pin for group %d is disabled (-1).", i+1);
			  }
			} else if (_buttonPins[i] != -1) {
			  // Same pin but possibly different pullup setting
			  _logUsermodB_R_T("Button pin %d unchanged. Ensuring proper mode with pull%s.", 
							_buttonPins[i], _pullUps[i] ? "up" : "down");
			  pinMode(_buttonPins[i], _pullUps[i] ? INPUT_PULLUP : INPUT);
			}
			
			// For relays:
			_logUsermodB_R_T("Group %d - Old relay pin: %d, New relay pin: %d", 
						  i+1, oldRelayPins[i], _relayPins[i]);
						  
			if (oldRelayPins[i] != _relayPins[i]) {
			  // Deallocate old pin if needed
			  if (oldRelayPins[i] != -1) {
				_logUsermodB_R_T("Deallocating old relay pin %d...", oldRelayPins[i]);
				if (PinManager::isPinAllocated(oldRelayPins[i], PinOwner::UM_BUTTON_RELAY_TOGGLE)) {
				  PinManager::deallocatePin(oldRelayPins[i], PinOwner::UM_BUTTON_RELAY_TOGGLE);
				  _logUsermodB_R_T("Successfully deallocated old relay pin %d.", oldRelayPins[i]);
				} else {
				  _logUsermodB_R_T("Old relay pin %d was not allocated by this usermod.", oldRelayPins[i]);
				}
			  }
			  
			  // Allocate new pin if needed
			  if (_relayPins[i] != -1) {
				_logUsermodB_R_T("Allocating new relay pin %d...", _relayPins[i]);
				if (PinManager::allocatePin(_relayPins[i], true, PinOwner::UM_BUTTON_RELAY_TOGGLE)) {
				  pinMode(_relayPins[i], OUTPUT);
				  digitalWrite(_relayPins[i], _activeLow[i] ? HIGH : LOW);
				  _logUsermodB_R_T("Successfully allocated new relay pin %d (Active %s).", 
								_relayPins[i], _activeLow[i] ? "LOW" : "HIGH");
				} else {
				  _logUsermodB_R_T("Failed to allocate new relay pin %d! Pin may be in use.", _relayPins[i]);
				}
			  } else {
				_logUsermodB_R_T("New relay pin for group %d is disabled (-1).", i+1);
			  }
			} else if (_relayPins[i] != -1) {
			  // Same pin, ensure proper mode and maybe update active low status
			  _logUsermodB_R_T("Relay pin %d unchanged. Ensuring proper mode as OUTPUT and Active %s.", 
							_relayPins[i], _activeLow[i] ? "LOW" : "HIGH");
			  pinMode(_relayPins[i], OUTPUT);
			}
		  }
		}
	  }

	  #ifndef WLED_DISABLE_MQTT
	  if (WLED_MQTT_CONNECTED && enabled) {
		publishConfigToMqtt();
		triggerHaDiscovery = true;
		setupRelaySubscriptions();
	  }
	  #endif

	  return configComplete;
	}

    ////////////////////////////////////////////////////////////////////////////////
    // MQTT connection established handler
    ////////////////////////////////////////////////////////////////////////////////
	void onMqttConnect(bool sessionPresent) override {
	  #ifndef WLED_DISABLE_MQTT
	  if (!WLED_MQTT_CONNECTED) {
		_logUsermodB_R_T("MQTT not connected on onMqttConnect.");
		return;
	  }
	  if (!enabled) {
		_logUsermodB_R_T("Usermod disabled, ignoring onMqttConnect.");
		return;
	  }
      _logUsermodB_R_T("MQTT reconnected (sessionPresent=%s)", sessionPresent?"Y":"N");
	  publishConfigToMqtt();
	  triggerHaDiscovery = true;
	  setupRelaySubscriptions();
	  
	  // Publish current states to avoid "not available" in Home Assistant after reconnect
	  publishAllCurrentStates();
	  #endif
	}

	// New function to publish current states of all sensors and relays
	void publishAllCurrentStates() {
	  #ifndef WLED_DISABLE_MQTT
	  if (!WLED_MQTT_CONNECTED || !enabled) return;

      _logUsermodB_R_T("Publishing current states of all entities...");

	  for (uint8_t i = 0; i < 4; i++) {
		// For buttons, publish the state as "Released" (we can't know if it's pressed when reconnecting)
		if(_buttonPins[i]!=-1) publishMqtt("Button", i+1, false); // false = "Released" state

		// For relays, read and publish the actual current state
		if (_relayPins[i] != -1) {
		  bool relayState = _activeLow[i] ? !digitalRead(_relayPins[i]) : digitalRead(_relayPins[i]);
		  publishMqtt("Relay", i + 1, relayState);
		}
	  }
	  #endif
	}

    ////////////////////////////////////////////////////////////////////////////////
    // MQTT message handler for relay commands from Home Assistant
    ////////////////////////////////////////////////////////////////////////////////
	bool onMqttMessage(char *topic, char *payload) override {
	  #ifndef WLED_DISABLE_MQTT
	  if (!WLED_MQTT_CONNECTED || !enabled) return false;

	  _logUsermodB_R_T("Received MQTT message. Topic: %s, Payload: %s", topic, payload);

	  // Get device topic from the current topic by stripping from the beginning
	  String fullTopic = String(topic);

	  for (uint8_t i = 0; i < 4; i++) {
		// Skip unused relays
		if (_relayPins[i] == -1) continue;
		
		String expectedTopicEnd = String("/Button_Relay_Toggle/group_") + String(i+1) + String("/Relay/set");
		
		if (!fullTopic.endsWith(expectedTopicEnd)) continue;
		
		bool desiredOn  = strcasecmp(payload,"ON") == 0;
		_logUsermodB_R_T("MQTT receiverd relay %u to %s", i+1, desiredOn  ? "ON":"OFF");

		// Current logical state (account for activeLow)
		bool currentOn = _activeLow[i]
		  ? !digitalRead(_relayPins[i])
		  : digitalRead(_relayPins[i]);

		// If it's already in the requested state, ignore to avoid loops
		if (desiredOn == currentOn) {
		  _logUsermodB_R_T("Ignoring /set for group %u because already %s", i+1, desiredOn?"ON":"OFF");
		  return true;
		}

		// Otherwise apply the new state
		_logUsermodB_R_T("Applying /set for group %u → %s", i + 1, desiredOn?"ON":"OFF");
		digitalWrite(_relayPins[i], _activeLow[i] ? !desiredOn : desiredOn);
		
		// And re-publish so HA sees the updated state
		publishMqtt("Relay", i + 1, desiredOn );
		return true;
	  }
      _logUsermodB_R_T("No matching MQTT topic found.");
	  #endif
	  return false;
	}

    ////////////////////////////////////////////////////////////////////////////////
    // Publish an MQTT message for a given group and message type.
    // "Button" messages publish a descriptive string; "Relay" messages publish ON/OFF.
    ////////////////////////////////////////////////////////////////////////////////
    void publishMqtt(const char* message, uint8_t group, bool state) {
      #ifndef WLED_DISABLE_MQTT
        if (!WLED_MQTT_CONNECTED) {
		  _logUsermodB_R_T("MQTT not connected. Cannot publish MQTT message.");
		  return;
        }
		
        _logUsermodB_R_T("Publishing MQTT message. Type: %s, Group: %u, State: %s", message, group, state ? "ON" : "OFF");
		
        char topic[128]; //Topic size: 57, Topic buffer size: 128 - for wled/Netzerkschrank-Fan/Button_Relay_Toggle/group_3/Relay
        strcpy(topic, mqttDeviceTopic);
        snprintf(topic + strlen(topic), sizeof(topic) - strlen(topic), "/Button_Relay_Toggle/group_%u/%s", group, message);
		
        char payload[16];
        if (strcmp(message, "Button") == 0) {
		  // For button state sensors, use "PRESSED" and "RELEASED" states
		  // This works better with Home Assistant binary sensors
		  snprintf(payload, sizeof(payload), "%s", state ? "PRESSED" : "RELEASED");
        } else {
          snprintf(payload, sizeof(payload), "%s", state ? "ON" : "OFF");
        }
        _logUsermodB_R_T("Publishing to topic: %s, Payload: %s", topic, payload);
        publishMqttMessage(topic, payload);

        if(strcmp(message, "Relay") == 0){
          char setT[128];
          strcpy(setT, topic);
          strncat(setT, "/set", sizeof(setT) - strlen(setT) - 1);
		
          publishMqttMessage(setT, payload);
          _logUsermodB_R_T("Mirrored command to topic: %s → %s", setT, payload);
        }
      #endif
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Publish the usermod configuration to MQTT.
    ////////////////////////////////////////////////////////////////////////////////
    void publishConfigToMqtt(){
      #ifndef WLED_DISABLE_MQTT
        if (!WLED_MQTT_CONNECTED) {
		  _logUsermodB_R_T("MQTT not connected. Skipping config publish.");
		  return;
        }
        _logUsermodB_R_T("Publishing usermod config to MQTT...");
        
        StaticJsonDocument<512> doc;
        doc["enabled"] = enabled;
        doc["homeassistant_discovery"] = haDiscovery;
        for (uint8_t i = 0; i < 4; i++) {
          JsonObject group = doc.createNestedObject(_groups[i]);
          group["button_pin"] = _buttonPins[i];
          group["button_pullup"] = _pullUps[i];
          group["relay_pin"] = _relayPins[i];
          group["relay_active_low"] = _activeLow[i];
          _logUsermodB_R_T("Group %s: Button pin=%d, Pullup=%d, Relay pin=%d, Active low=%d",
                       _groups[i], _buttonPins[i], _pullUps[i], _relayPins[i], _activeLow[i]);
        }
        char buffer[512]; //Payload size: 404, Buffer size: 512  //CommentOut for ArduinoJson 7
        size_t payload_size = serializeJson(doc, buffer, sizeof(buffer));
        char topic[64];
        snprintf_P(topic, sizeof(topic), "%s/Button_Relay_Toggle/config", mqttDeviceTopic);

        _logUsermodB_R_T("Publishing config to topic: %s, Payload size: %d", topic, payload_size);
        publishMqttMessage(topic, buffer);
      #endif
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Return a unique ID for this usermod.
    ////////////////////////////////////////////////////////////////////////////////
    uint16_t getId() override {
      return USERMOD_ID_BUTTON_RELAY_TOGGLE;
    }
};

static UsermodButtonRelayToggle button_relay_toggle_v2;
REGISTER_USERMOD(button_relay_toggle_v2);