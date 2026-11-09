#ifndef WLED_SYSLOG_H
#define WLED_SYSLOG_H
#include <WString.h>
#include <WiFiUdp.h>
#ifdef ARDUINO_ARCH_ESP32
  #include <freertos/FreeRTOS.h>
  #include <freertos/semphr.h>
#endif

// Buffer management
#ifndef SYSLOG_BUFFER_SIZE
  #define SYSLOG_BUFFER_SIZE 128
#endif

#define SYSLOG_LOCAL0   	16 // local use 0
#define SYSLOG_DEBUG    7  // Debug: debug-level messages

// Syslog protocol formats - commented out but preserved
#define SYSLOG_PROTO_BSD       0  // Legacy BSD format (RFC 3164)

// Minimum time between blocking hostByName() retries while resolution keeps
// failing, so a misconfigured/unreachable syslog host doesn't stall the main
// loop on every single debug line.
#ifndef SYSLOG_RESOLVE_RETRY_MS
  #define SYSLOG_RESOLVE_RETRY_MS 10000UL
#endif

// How long a task waits for another task's log output before dropping its own.
#ifndef SYSLOG_LOCK_TIMEOUT_MS
  #define SYSLOG_LOCK_TIMEOUT_MS 50
#endif

class SyslogPrinter : public Print {
  private:
    WiFiUDP syslogUdp; // needs to be here otherwise UDP messages get truncated upon destruction
    IPAddress syslogHostIP;
    unsigned long _lastResolveAttempt = 0; // backoff so a failing DNS lookup isn't retried on every log line
    bool _hasAttemptedResolve = false;     // ensures the very first attempt isn't delayed by the backoff above
    bool resolveHostname();
    bool _lastOperationSucceeded;
    String _lastErrorMessage;

    // Syslog configuration - using globals from wled.h
    uint8_t _facility;  // Internal copy of syslogFacility (from wled.h), fixed to SYSLOG_LOCAL0
    uint8_t _severity;  // Internal copy of syslogSeverity (from wled.h), fixed to SYSLOG_DEBUG
    uint8_t _protocol;  // Internal copy of syslogProtocol (from wled.h), fixed to SYSLOG_PROTO_BSD
    String _appName;

    // Cache of the space-to-underscore-cleaned serverDescription, so every
    // single packet send doesn't need a fresh heap allocation - only
    // recomputed when serverDescription actually changes.
    String _cachedHostname;
    String _cachedSourceDescription;
    const String& cleanedHostname();

    char _buffer[SYSLOG_BUFFER_SIZE]; // Buffer for collecting characters

    size_t _bufferIndex;
    void flushBuffer();
    void appendChar(uint8_t c);
    size_t sendPacket(const uint8_t *buf, size_t size, uint8_t severity);

    // Debug output comes from the main loop and from async network callbacks,
    // which share _buffer and syslogUdp.
    bool lock();
    void unlock();
    #ifdef ARDUINO_ARCH_ESP32
      StaticSemaphore_t _lockStorage;
      SemaphoreHandle_t _lock;
    #endif

  public:
    SyslogPrinter();
	void begin(const char* host, uint16_t port,
               uint8_t facility = SYSLOG_LOCAL0,
               uint8_t severity = SYSLOG_DEBUG,
               uint8_t protocol = SYSLOG_PROTO_BSD);
    void setAppName(const String &appName);

    // Print interface implementation
    virtual size_t write(uint8_t c);
    virtual size_t write(const uint8_t *buf, size_t size);

    // Severity override for specific messages
    size_t write(const uint8_t *buf, size_t size, uint8_t severity);

    // Error handling
    bool lastOperationSucceeded() const { return _lastOperationSucceeded; }
    String getLastErrorMessage() const { return _lastErrorMessage; }
};

// Default instance
extern SyslogPrinter Syslog;

#endif