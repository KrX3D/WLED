#pragma once
#include "wled.h"
#include <SPI.h>

// logging macro:
#define _logUsermodNixieClock(fmt, ...) \
	DEBUG_PRINTF("[NixieClock] " fmt "\n", ##__VA_ARGS__)

#ifndef NIXIECLOCK_ENABLED
    #define NIXIECLOCK_ENABLED false  // Default disabled value
#endif

#ifndef LATCH_PIN
    #define LATCH_PIN 10
#endif

#ifndef NIXIECLOCK_RGB_ENABLED
    #define NIXIECLOCK_RGB_ENABLED false
#endif

#ifndef NIXIECLOCK_DOTS_ENABLED
    #define NIXIECLOCK_DOTS_ENABLED true
#endif

#ifndef NIXIECLOCK_CLOCK_ENABLED
    #define NIXIECLOCK_CLOCK_ENABLED true
#endif

#ifndef NIXIECLOCK_FORCE_NTP_ENABLED
    #define NIXIECLOCK_FORCE_NTP_ENABLED true
#endif

#ifndef NIXIECLOCK_UPDATE_NTP_INTERVALL
    #define NIXIECLOCK_UPDATE_NTP_INTERVALL 30 // minutes
#endif

#define DIGIT_BLANK 10  // Use an index that doesn't exist in 0-9
#define UPPER_DOTS_MASK 0x80000000
#define LOWER_DOTS_MASK 0x40000000

class UsermodNixieClock : public Usermod {
private:
    // --- Internal State Variables ---
    bool initDone = false;
    bool nixiePower = true, dotsPower = true, ledPower = true, mainState = true;
    unsigned long lastNtpUpdate = 0, lastCheck = 0;
    uint16_t digits[6] = {0};
    bool dotsEnabled = false;
    bool lastSpiState = false;  // Track SPI state to detect failures
    unsigned long lastStateCheck = 0;  // Track when we last checked states
	bool displayBlanked = false;
	
    // Member variables for non-blocking anti-poisoning
    bool antiPoisoningInProgress = false;
    unsigned long lastAntiPoisoningTime = 0;
    unsigned long antiPoisoningLastUpdate = 0;
    uint8_t antiPoisoningIteration = 0;
    byte antiPoisoningCurrentDigits[6];
    byte antiPoisoningTargetDigits[6];
	
    bool setupSegmentsFinished = false;
    unsigned long checkSegmentsTime = 0;

    // --- Usermod Settings ---
    bool UM_enabled = NIXIECLOCK_ENABLED;
    uint8_t UM_latchPin = LATCH_PIN;
    bool UM_LedEnabled = NIXIECLOCK_RGB_ENABLED;
    bool UM_DotsEnabled = NIXIECLOCK_DOTS_ENABLED;
    bool UM_ClockEnabled = NIXIECLOCK_CLOCK_ENABLED;
    bool UM_ntpUpdateForce = NIXIECLOCK_FORCE_NTP_ENABLED;   // Flag to enable/disable forced NTP update
    uint16_t UM_ntpUpdateInterval = NIXIECLOCK_UPDATE_NTP_INTERVALL; // Interval in minutes
    uint32_t ntpUpdateInterval = UM_ntpUpdateInterval * 1000UL * 60UL; // Convert to milliseconds
	
    // --- Symbol Array: Mapping of digits to segment bit patterns ---
    // Index 0-9: actual digits, index 10: blank
    unsigned int SymbolArray[11] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 0};

    bool setupSPI();
    void updateSegments();
    void displayTime();
    void setDigits(const byte digitsArray[6]);
    void show();
    void powerOffNixieTubes();
    void startAntiPoisoning();
    void handleAntiPoisoning();
    void deallocateLatchPin();
    void verifyAndFixState();
    void updateNixieClockSettings();
    void performRecovery();

public:
    ~UsermodNixieClock();
    void setup();
    void loop();
    void dotsEnable(bool state);
    void addToConfig(JsonObject &root) override;
    bool readFromConfig(JsonObject &root) override;
    void onStateChange(uint8_t mode) override;
    uint16_t getId() override;
	
	//=====================================================================
	// External API: Control Nixie Power
	//=====================================================================
	// Can be used outside of this usermod to control the power state of Nixie tubes.
	// For compatibility with other usermods, "true" disables the nixie tubes.
	void setNixiePower(bool disable) {
		setNixiePowerEnabled(!disable);
	}

	// Explicit helper for callers that want "true = enabled".
	void setNixiePowerEnabled(bool enabled) {
		if (mainState == enabled && nixiePower == enabled) {
			return;
		}

		mainState = enabled;
		nixiePower = enabled;
		displayBlanked = !enabled;

		if (enabled) {
			if (bri == 0 && briLast > 0) {
				bri = briLast;
				applyFinalBri();
			}
		} else if (bri > 0) {
			briLast = bri;
			bri = 0;
			applyFinalBri();
		}

		if (!enabled) {
			powerOffNixieTubes();
		} else if (UM_ClockEnabled) {
			displayTime();
			displayBlanked = false;
		}

		#ifdef DEBUG_PRINTF
			_logUsermodNixieClock("Nixie power set to: %s", enabled ? "ON" : "OFF");
		#endif
	}

	//=====================================================================
	// External API: Get LED Enabled Status
	//=====================================================================
	// Can be used outside of this usermod to check if the LED is enabled.
	bool getLedEnabled() { return UM_LedEnabled; }
};
