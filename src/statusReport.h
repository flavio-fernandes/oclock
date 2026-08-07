#ifndef __STATUS_REPORT_H
#define __STATUS_REPORT_H

#include <string>
#include <utility>
#include <vector>

#include "stdTypes.h"
#include "dictionary.h"
#include "motionSensor.h"
#include "mqttClient.h"

// The status of the machine the clock runs on, read from /proc.
//
// Load averages are kept as integer hundredths rather than as a double on
// purpose: both strtod() and "%.2f" honour the C locale's decimal separator,
// and a JSON number containing a comma is not JSON. Integers cannot go wrong
// that way.
typedef struct systemStatus_t {
  bool loadValid;              // false when /proc/loadavg could not be read
  Int32U loadAvg1Hundredths;   // "0.14" -> 14
  Int32U loadAvg5Hundredths;
  Int32U loadAvg15Hundredths;

  bool memValid;               // false when /proc/meminfo could not be read
  Int64U memFreeKb;            // MemFree
  Int64U memAvailableKb;       // MemAvailable
  Int64U memTotalKb;           // MemTotal
} SystemStatus;

// Everything /status, /status.json and the periodic MQTT report are built
// from. Gathered once, rendered twice, so the three cannot disagree.
typedef struct statusSnapshot_t {
  // False only if the web server is not up yet, which the legacy page
  // rendered by omitting the hit counts entirely.
  bool webHandlerAvailable;
  std::vector<std::pair<std::string, Int32U> > handlerHits;

  MotionInfo motion;
  Int32U lightValue;

  std::string displayMode;
  bool displayDimmed;
  std::string ledStripMode;

  MqttClientInfo mqtt;
  std::string mqttBrokerIp;       // owned copy; MqttClientInfo points at argv
  std::string mqttLastLoopRcStr;  // resolved by the gatherer, so that the
                                  // renderers need no mosquitto

  DictionaryStatus dictionaryStatus;
  size_t dictionarySize;
  std::vector<std::pair<std::string, std::string> > dictionaryEntries;

  SystemStatus system;
} StatusSnapshot;

// Reads the singletons. Lives in statusReportGather.cpp so that everything
// below can be tested without linking the application's threads.
void gatherStatusSnapshot(StatusSnapshot& snapshot);

// The paths are parameters so tests can point them at fixtures. Returns false
// if either file was unusable; the corresponding valid flag says which.
bool readSystemStatus(SystemStatus& systemStatus,
                      const char* loadavgPath = "/proc/loadavg",
                      const char* meminfoPath = "/proc/meminfo");

// Escapes value for use inside a JSON string literal. Dictionary keys and
// values arrive from HTTP POSTs, so this is load bearing rather than tidy.
std::string jsonEscape(const std::string& value);

// The legacy text/plain page. Its existing lines are a compatibility contract;
// tests/status_tests.cpp pins them to an exact expected buffer.
std::string renderStatusText(const StatusSnapshot& snapshot);

// The application/json representation served at /status.json and published to
// /officeClock/status. See docs/status-api.md for the schema and its
// compatibility rules.
std::string renderStatusJson(const StatusSnapshot& snapshot);

const int statusJsonVersion = 1;

#endif  // __STATUS_REPORT_H
