#include "nixieclock_v2.h"

// Initialize SPI with required settings.
bool UsermodNixieClock::setupSPI() {
	//try {
		//SPI.begin(spi_sclk, -1, spi_mosi); clkPin = 18; mosiPin = 23;
		SPI.setDataMode(SPI_MODE2); // Must be MODE2 for the display to work correctly.
		//SPI.setFrequency(2000000); // 2MHz SPI
		_logUsermodNixieClock("SPI initialized successfully");
		return true;
	//} catch (...) {
		//_logUsermodNixieClock("ERROR: SPI initialization failed!");
		//return false;
	//}
}

UsermodNixieClock::~UsermodNixieClock() {
	// Deallocate latch pin
	deallocateLatchPin();
	_logUsermodNixieClock("Usermod destroyed");
}

void UsermodNixieClock::setup() {
	updateNixieClockSettings();
	initDone = true;
	_logUsermodNixieClock("Setup complete");
}

void UsermodNixieClock::loop() {
	if (!initDone) return;
	
	if (UM_enabled) {
		unsigned long currentMillis = millis();
		
		// --- Periodic State Verification (to detect inconsistencies) ---
		verifyAndFixState();
		
		// --- Check and update segments configuration every 5 seconds if not done ---
		if (!setupSegmentsFinished && (currentMillis - checkSegmentsTime >= 5000)) {
			checkSegmentsTime = currentMillis;
			uint8_t numSegments = strip.getSegmentsNum();
			_logUsermodNixieClock("Current number of segments: %d", numSegments);
			if (numSegments != 3){
				updateSegments();
			} else {
				setupSegmentsFinished = true;
				_logUsermodNixieClock("Segments setup confirmed");
			}
		}
	
		// Only update the display if main power is on and clock display is enabled.
		if (!mainState) {
			if (displayBlanked) return;
			// If main power is off, clear the display.
			powerOffNixieTubes();
			displayBlanked = true;
			return;
		}

		if (nixiePower && UM_ClockEnabled){
			// --- Anti-Poisoning Routine ---
			// Every 2 minutes (120000 ms), run the anti-poisoning routine if not already running
			if (currentMillis - lastAntiPoisoningTime >= 120000 && !antiPoisoningInProgress) {
				lastAntiPoisoningTime = currentMillis;
				startAntiPoisoning();
			}

			// Instead of blocking, call the handler
			handleAntiPoisoning();

			// --- Force NTP Update ---
			// If connected to Wifi and forced NTP updates are enabled, force an update after the set interval.
			if (WLED_CONNECTED && UM_ntpUpdateForce && (currentMillis - lastNtpUpdate >= ntpUpdateInterval)) {
				_logUsermodNixieClock("Force NTP Update triggered");
				ntpLastSyncTime = NTP_NEVER; // Force new NTP query (global variable expected)
				lastNtpUpdate = currentMillis;
			}

			// --- Update the Time Display every second ---
			if (currentMillis - lastCheck >= 1000) {
				lastCheck = currentMillis;
				// Only update display if anti-poisoning is not active
				if (!antiPoisoningInProgress) {
					displayTime();
				}
			}
		
			// --- Recovery (simulate user pressing clock reset) ---
			// If we've had a failure, attempt recovery every 60 seconds
			static unsigned long lastRecovery = 0;
			if (!lastSpiState && (currentMillis - lastRecovery >= 60000)) {
				lastRecovery = currentMillis;
				_logUsermodNixieClock("Triggering automatic recovery after failure");
				performRecovery();
			}
		} else {
			if (displayBlanked) return;
			// If power for Nixie tubes is off, clear the display.
			powerOffNixieTubes();
			displayBlanked = true;
		}
	}
	//delay(10);
}

void UsermodNixieClock::updateSegments() {
	_logUsermodNixieClock("Updating segments configuration...");
	
	// Clear segments but remember their states if they exist
	bool prevLedOn = strip.getSegmentsNum() >= 1 ? strip.getSegment(0).getOption(SEG_OPTION_ON) : true;
	bool prevDotsOn = strip.getSegmentsNum() >= 2 ? strip.getSegment(1).getOption(SEG_OPTION_ON) : true;
	bool prevNixieOn = strip.getSegmentsNum() >= 3 ? strip.getSegment(2).getOption(SEG_OPTION_ON) : true;
	
	strip.resetSegments();  // Clear all segments        
	
	// Recreate segments with proper geometry
	strip.getSegment(0).setGeometry(0, 1);  // Segment 0: RGB LED (Single LED Control)
	strip.getSegment(1).setGeometry(1, 2);  // Segment 1: Dots (Control Only)
	strip.getSegment(2).setGeometry(2, 3);  // Segment 2: Nixie Tubes (Control Only)
	
	// Restore previous states
	strip.getSegment(0).setOption(SEG_OPTION_ON, prevLedOn);
	strip.getSegment(1).setOption(SEG_OPTION_ON, prevDotsOn);
	strip.getSegment(2).setOption(SEG_OPTION_ON, prevNixieOn);
	
	_logUsermodNixieClock("Segments created successfully");
	
	// Verify segment creation
	uint8_t numSegments = strip.getSegmentsNum();
	_logUsermodNixieClock("Verified segments count: %d", numSegments);
	
	// Ensure our internal state variables match the segment states
	ledPower = strip.getSegment(0).getOption(SEG_OPTION_ON);
	dotsPower = strip.getSegment(1).getOption(SEG_OPTION_ON);
	nixiePower = strip.getSegment(2).getOption(SEG_OPTION_ON);
	
	if (numSegments != 3) {
		_logUsermodNixieClock("WARNING: Segments not properly created!");
	}
}

// Update the display with the current time.
// Note: Assumes global variable 'localTime' exists and is updated elsewhere.
void UsermodNixieClock::displayTime() {
	// Get current time digits.
	byte timeDigits[] = {
		static_cast<byte>(hour(localTime) / 10), static_cast<byte>(hour(localTime) % 10),
		static_cast<byte>(minute(localTime) / 10), static_cast<byte>(minute(localTime) % 10),
		static_cast<byte>(second(localTime) / 10), static_cast<byte>(second(localTime) % 10)
	};
	setDigits(timeDigits);
	dotsEnable(second(localTime) % 2 == 0); // Toggle dots every second	
	show();
}

// Set the digit array with a provided six-digit array.
// The order is reversed for display (digit[5] = most significant).
void UsermodNixieClock::setDigits(const byte digitsArray[6]) {
	for (int i = 0; i < 6; i++) {
		// If the digit is valid (0-9) then set it; otherwise use blank.
		digits[5 - i] = (digitsArray[i] < 10) ? digitsArray[i] : DIGIT_BLANK;
	}
}

// Send the current digit and dot states to the display via SPI.
void UsermodNixieClock::show() {
	// Safety check
	if (!UM_enabled) {
		_logUsermodNixieClock("Show called but usermod disabled!");
		return;
	}

	// Safety check for SPI
	if (!lastSpiState) {
		_logUsermodNixieClock("Attempting SPI recovery before show()");
		if (!setupSPI()) {
			return; // Cannot continue without SPI
		}
	}
	
	// Begin data transfer: set latch pin LOW.
	digitalWrite(UM_latchPin, LOW);
	bool success = true;
	
	for (int i = 0; i < 2; i++) {
		// Construct 32-bit value containing the three digit symbols.
		// Also add dot masks if dots are enabled.
		unsigned long Var32 =
			((unsigned long)SymbolArray[digits[i * 3]] << 20) |
			((unsigned long)SymbolArray[digits[i * 3 + 1]] << 10) |
			(unsigned long)SymbolArray[digits[i * 3 + 2]] |
			(mainState && dotsPower && dotsEnabled && UM_DotsEnabled ? (UPPER_DOTS_MASK | LOWER_DOTS_MASK) : 0);

		// Log the data being sent (occasionally)
		//if (random(0, 100) < 5) { // 5% chance to log to avoid flooding
			//_logUsermodNixieClock("SPI data chunk %d: 0x%08X", i, Var32);
		//}
		
		// Transmit the 32-bit value as four bytes over SPI.
		//try {
			SPI.transfer(Var32 >> 24);
			SPI.transfer(Var32 >> 16);
			SPI.transfer(Var32 >> 8);
			SPI.transfer(Var32);
		//} catch (...) {
			//_logUsermodNixieClock("ERROR: SPI transfer failed!");
			//success = false;
		//}
	}
	// End data transfer: set latch pin HIGH to latch the data.
	digitalWrite(UM_latchPin, HIGH);
	
	// Update SPI state tracking
	if (lastSpiState != success) {
		if (!success) {
			_logUsermodNixieClock("WARNING: SPI state changed to failed!");
		} else {
			_logUsermodNixieClock("SPI state recovered");
		}
		lastSpiState = success;
	}
}

// Turn off the Nixie tubes by setting all digits to blank.
void UsermodNixieClock::powerOffNixieTubes(){
	byte blankDigits[6] = {DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK};
	setDigits(blankDigits);
	dotsEnable(false);
	show();
	_logUsermodNixieClock("Nixie tubes powered off (display blank)");
}

// Call this function to start anti-poisoning
void UsermodNixieClock::startAntiPoisoning() {
  antiPoisoningInProgress = true;
  antiPoisoningIteration = 0;
  antiPoisoningLastUpdate = millis();
  // Initialize current digits (e.g., to 0)
  for (int i = 0; i < 6; i++) {
	antiPoisoningCurrentDigits[i] = 0;
	antiPoisoningTargetDigits[i] = random(0, 10);
  }
  _logUsermodNixieClock("Starting non-blocking Anti-Poisoning routine");
}

// This should be called in your main loop (or as part of loop())
void UsermodNixieClock::handleAntiPoisoning() {
  if (!antiPoisoningInProgress) return;  // Nothing to do

  unsigned long currentMillis = millis();
  // Run one iteration every 50ms
  if (currentMillis - antiPoisoningLastUpdate >= 50) {
	antiPoisoningLastUpdate = currentMillis;
		
	bool anyDigitChanging = false;
		
	// Update each digit: increment until it matches target, then choose a new target.
	for (int i = 0; i < 6; i++) {
		if (antiPoisoningCurrentDigits[i] != antiPoisoningTargetDigits[i]) {
			antiPoisoningCurrentDigits[i] = (antiPoisoningCurrentDigits[i] + 1) % 10;
			anyDigitChanging = true;
		} else if (antiPoisoningIteration < 50) { // Only change targets before final iterations
			antiPoisoningTargetDigits[i] = random(0, 10);
			anyDigitChanging = true;
		}
	}

	// Update display only if something changed (saves SPI bandwidth)
	if (anyDigitChanging) {
		setDigits(antiPoisoningCurrentDigits);
		show();
		// Toggle dots for visual effect
		dotsEnable(!dotsEnabled);
	}

	antiPoisoningIteration++;
	// After 60 iterations (~3000ms), finish the routine
	if (antiPoisoningIteration >= 60 || !anyDigitChanging) {
		antiPoisoningInProgress = false;
		_logUsermodNixieClock("Non-blocking Anti-Poisoning routine complete");
			
		// Restore time display
		displayTime();
	}
  }
}

// Deallocate the latch pin if it was previously allocated.
void UsermodNixieClock::deallocateLatchPin() {
	if (PinManager::isPinAllocated(UM_latchPin, PinOwner::UM_NIXIECLOCK)) {
		_logUsermodNixieClock("Releasing latch pin %d", UM_latchPin);
		PinManager::deallocatePin(UM_latchPin, PinOwner::UM_NIXIECLOCK);
	} else {
		_logUsermodNixieClock("Latch pin %d was not allocated", UM_latchPin);
	}
}

// Verify state consistency and fix if needed
void UsermodNixieClock::verifyAndFixState() {
	unsigned long now = millis();
	
	// Only run this check every 5 seconds
	if (now - lastStateCheck < 5000) return;
	lastStateCheck = now;
	
	// Safety check for segments
	if (strip.getSegmentsNum() < 3) {
		_logUsermodNixieClock("Segment count too low, recreating segments");
		updateSegments();
		return;
	}
	
	// Check if segment states match internal variables
	bool segLedPower = strip.getSegment(0).getOption(SEG_OPTION_ON);
	bool segDotsPower = strip.getSegment(1).getOption(SEG_OPTION_ON); 
	bool segNixiePower = strip.getSegment(2).getOption(SEG_OPTION_ON);
	
	// Check for inconsistencies
	if (ledPower != segLedPower || dotsPower != segDotsPower || nixiePower != segNixiePower) {
		_logUsermodNixieClock("State inconsistency detected!");
		_logUsermodNixieClock("Internal states - LED: %d, Dots: %d, Nixie: %d", 
					ledPower, dotsPower, nixiePower);
		_logUsermodNixieClock("Segment states - LED: %d, Dots: %d, Nixie: %d", 
					segLedPower, segDotsPower, segNixiePower);
		
		// Update internal state to match segments
		ledPower = segLedPower;
		dotsPower = segDotsPower;
		nixiePower = segNixiePower;
		
		_logUsermodNixieClock("Internal states synchronized with segments");
	}
	
	// Check main state consistency
	bool expectedMainState = (bri > 0);
	if (mainState != expectedMainState) {
		_logUsermodNixieClock("Main state inconsistency: actual=%d, expected=%d (bri=%d)", 
					mainState, expectedMainState, bri);
		mainState = expectedMainState;
		_logUsermodNixieClock("Main state synchronized");
	}
	
	// Validate SPI state
	if (!lastSpiState) {
		_logUsermodNixieClock("SPI in failed state during verification, attempting reset");
		setupSPI();
	}
}

// Update settings related to the NixieClock.
void UsermodNixieClock::updateNixieClockSettings() {
	// Check for valid SPI pins and NTP status.
	// (spi_sclk, spi_mosi, and ntpEnabled are assumed to be defined by WLED.)
	if (spi_sclk<0 || spi_mosi<0 || !UM_enabled || !ntpEnabled) { 
		UM_enabled = false;			
		powerOffNixieTubes();
		strip.resetSegments();  // Clear all segments
		_logUsermodNixieClock("Disabled due to invalid SPI pins or NTP not enabled");
		
		// Additional logging
		_logUsermodNixieClock("SPI_SCLK: %d, SPI_MOSI: %d, ntpEnabled: %d", 
					spi_sclk, spi_mosi, ntpEnabled);
		return; 
	}
	
	_logUsermodNixieClock("Updating settings...");
	_logUsermodNixieClock("Using SPI SCLK: %d", spi_sclk);
	_logUsermodNixieClock("Using SPI MOSI: %d", spi_mosi);
	
	// Deallocate the latch pin if needed.
	deallocateLatchPin();

	// Allocate the latch pin using PinManager.
	if (!PinManager::allocatePin(UM_latchPin, true, PinOwner::UM_NIXIECLOCK)) {
		_logUsermodNixieClock("Latch pin allocation failed! Disabling usermod.");
		UM_enabled = false; // Disable usermod if allocation fails.
		powerOffNixieTubes();
		strip.resetSegments();  // Clear all segments
		return;
	}
	
	// Set latch pin as OUTPUT.
	pinMode(UM_latchPin, OUTPUT);
	digitalWrite(UM_latchPin, HIGH); // Ensure initial state is HIGH
	_logUsermodNixieClock("Latch Pin allocated: %d", UM_latchPin);
	
	// Initialize SPI and segment configuration.
	if (!setupSPI()) {
		_logUsermodNixieClock("SPI setup failed, disabling usermod");
		UM_enabled = false;
		return;
	}
	setupSegmentsFinished = false;
	
	_logUsermodNixieClock("LED Enabled: %s", UM_LedEnabled ? "true" : "false");
	static unsigned long lastBriUpdate = 0;
	
	// Update brightness (ON/OFF) for the RGB LED segment if required.
	if (millis() - lastBriUpdate > 1000) { // update at most every 1000ms (prevents bootloop)
		lastBriUpdate = millis();
		if (UM_LedEnabled) {
			if (bri == 0 && briLast > 0) {  // Prevent restoring 0 brightness
				bri = briLast;
				applyFinalBri();
				mainState = true;
				_logUsermodNixieClock("Restored brightness for LED: %d", bri);
			}
		} else {
			if (bri > 0) {  // Save brightness then set to zero.
				briLast = bri;
				bri = 0;
				applyFinalBri();
				mainState = false;
				_logUsermodNixieClock("LED brightness set to 0");
			}
		}
	}
}

// Reset all state and restart SPI - used for recovery
void UsermodNixieClock::performRecovery() {
	_logUsermodNixieClock("Performing full recovery procedure");
	
	// First try just resetting SPI mode
	SPI.setDataMode(SPI_MODE2);
	
	// If that didn't work, do a more thorough reset
	if (!lastSpiState) {
		// Reset SPI
		SPI.end();
		delay(50);
		
		// Reinitialize SPI with proper settings
		#ifdef ESP32
		SPI.begin(spi_sclk, -1, spi_mosi);
		#else
		SPI.begin();
		#endif
		
		setupSPI();
	}
	
	// Reset segments with state preservation
	updateSegments();
	
	// Reset all state
	verifyAndFixState();
	
	// Reset display
	if (mainState && nixiePower && UM_ClockEnabled) {
		displayTime();
	} else {
		powerOffNixieTubes();
	}
	
	// Force successful SPI state to trigger show() to work
	lastSpiState = true;
	
	_logUsermodNixieClock("Recovery procedure complete");
}

// Enable or Disable Dots on the Display.
void UsermodNixieClock::dotsEnable(bool state) { 
	if (dotsEnabled != state) {
		dotsEnabled = state; 
		//_logUsermodNixieClock("Dots display set to: %s", state ? "ON" : "OFF");
	}
}

void UsermodNixieClock::addToConfig(JsonObject &root) {
	JsonObject top = root.createNestedObject(F("NixieClock"));
	top["Usermod_enabled"] = UM_enabled;
	top["latch_pin"] = UM_latchPin;
	top["Enable_LED"] = UM_LedEnabled; 
	top["Enable_Dots"] = UM_DotsEnabled;
	top["Enable_NixieTubes"] = UM_ClockEnabled;
	top["NTP_force_update"] = UM_ntpUpdateForce;
	top["NTP_update_interval_in_min"] = ntpUpdateInterval / (1000 * 60);  // Convert ms to minutes
}

bool UsermodNixieClock::readFromConfig(JsonObject &root) {
	JsonObject top = root["NixieClock"];
	_logUsermodNixieClock("Reading config...");

	bool configComplete = !top.isNull(); // Check if the configuration exists

	// Read and update configuration values.
	configComplete &= getJsonValue(top["Usermod_enabled"], UM_enabled);
	configComplete &= getJsonValue(top["latch_pin"], UM_latchPin);
	configComplete &= getJsonValue(top["Enable_LED"], UM_LedEnabled, true);
	configComplete &= getJsonValue(top["Enable_Dots"], UM_DotsEnabled, true);
	configComplete &= getJsonValue(top["Enable_NixieTubes"], UM_ClockEnabled, true);
	configComplete &= getJsonValue(top["NTP_force_update"], UM_ntpUpdateForce, true);		

	// Read update interval (in minutes) then convert to milliseconds.
	if (getJsonValue(top[F("NTP_update_interval_in_min")], ntpUpdateInterval)) {
		if (ntpUpdateInterval >= 1 && ntpUpdateInterval <= 60) // Valid range: 1 - 60 minutes
			ntpUpdateInterval *= 1000UL * 60UL;
		else
			ntpUpdateInterval = UM_ntpUpdateInterval * 1000UL * 60UL; // Fallback to default value if out-of-range.
	} else {
		configComplete = false; // Configuration is incomplete
	}
	
	_logUsermodNixieClock("Config read complete: %s", configComplete ? "OK" : "INCOMPLETE");
	_logUsermodNixieClock("Enabled: %d, Latch Pin: %d", UM_enabled, UM_latchPin);
	_logUsermodNixieClock("LED: %d, Dots: %d, Clock: %d", UM_LedEnabled, UM_DotsEnabled, UM_ClockEnabled);
	_logUsermodNixieClock("Force NTP: %d, NTP Interval: %d min", UM_ntpUpdateForce, 
				ntpUpdateInterval / (1000 * 60));
	
	updateNixieClockSettings();
	
	return configComplete; // Return whether the configuration was complete
}

void UsermodNixieClock::onStateChange(uint8_t mode) {
	if (!initDone) return;
	
	// Retrieve current power states for each segment.
	bool prevLedPower = ledPower;
	bool prevDotsPower = dotsPower;
	bool prevNixiePower = nixiePower;
	bool prevMainState = mainState;
	
	ledPower = strip.getSegmentsNum() >= 1 ? strip.getSegment(0).getOption(SEG_OPTION_ON) : false;
	dotsPower = strip.getSegmentsNum() >= 2 ? strip.getSegment(1).getOption(SEG_OPTION_ON) : false;
	nixiePower = strip.getSegmentsNum() >= 3 ? strip.getSegment(2).getOption(SEG_OPTION_ON) : false;
	
	_logUsermodNixieClock("State change detected. Segment states:");
	_logUsermodNixieClock("RGB LED segment is %s", ledPower ? "ON" : "OFF");
	_logUsermodNixieClock("Dots segment is %s", dotsPower ? "ON" : "OFF");
	_logUsermodNixieClock("Nixie Tubes segment is %s", nixiePower ? "ON" : "OFF");
	
	// Determine main power status using the brightness variable.
	// When 'bri' is 0, main power is off; otherwise it is on.
	mainState = (bri > 0);
	_logUsermodNixieClock("Main Power (based on bri=%d) is %s", bri, mainState ? "ON" : "OFF");
	
	// Log changes
	if (prevLedPower != ledPower) {
		_logUsermodNixieClock("LED power changed: %d -> %d", prevLedPower, ledPower);
	}
	if (prevDotsPower != dotsPower) {
		_logUsermodNixieClock("Dots power changed: %d -> %d", prevDotsPower, dotsPower);
	}
	if (prevNixiePower != nixiePower) {
		_logUsermodNixieClock("Nixie power changed: %d -> %d", prevNixiePower, nixiePower);
	}
	if (prevMainState != mainState) {
		_logUsermodNixieClock("Main state changed: %d -> %d", prevMainState, mainState);
	}
	
	// Apply changes if nixie power or main state changed
	if (prevNixiePower != nixiePower || prevMainState != mainState) {
		if (mainState && nixiePower && UM_ClockEnabled) {
			_logUsermodNixieClock("Nixie tubes ON, updating display");
			displayTime();
		} else {
			_logUsermodNixieClock("Nixie tubes OFF, blanking display");
			powerOffNixieTubes();
		}
	}
}

uint16_t UsermodNixieClock::getId() { return USERMOD_ID_NIXIECLOCK; }

static UsermodNixieClock nixieclock_v2;
REGISTER_USERMOD(nixieclock_v2);
