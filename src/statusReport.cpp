#include "statusReport.h"

#include <stdio.h>
#include <string.h>

#include <fstream>
#include <sstream>

// Deliberately dependency free. Everything here is a pure function of a
// StatusSnapshot (or of a file path), which is what lets tests/status_tests.cpp
// link this translation unit on its own, without libevent, mosquitto, or any of
// the application's threads.

namespace {

// The same string-append shorthand webHandlerInternal.cpp uses, kept local.
template <class T>
inline std::string& operator<<(std::string& obj, T arg) {
  obj += arg;
  return obj;
}

const size_t maxIntTextSize = 24;

// The legacy page printed every counter through "%d" on an (int) cast, wrapping
// included. Rendering them any other way would change bytes that clients read.
void appendInt(std::string& buff, int value) {
  char text[maxIntTextSize];
  snprintf(text, sizeof(text), "%d", value);
  buff += text;
}

void appendUInt64(std::string& buff, Int64U value) {
  char text[maxIntTextSize];
  snprintf(text, sizeof(text), "%llu", value);
  buff += text;
}

// hundredths 14 -> "0.14", 1234 -> "12.34". No float, so no locale.
void appendLoad(std::string& buff, Int32U hundredths) {
  char text[maxIntTextSize];
  snprintf(text, sizeof(text), "%lu.%02lu", (unsigned long) (hundredths / 100),
           (unsigned long) (hundredths % 100));
  buff += text;
}

void appendJsonString(std::string& buff, const std::string& value) {
  buff << "\"" << jsonEscape(value) << "\"";
}

void appendJsonBool(std::string& buff, bool value) {
  buff << (value ? "true" : "false");
}

// "0.14" -> 14. Rejects anything that is not a bare unsigned decimal with at
// most two fraction digits, which is all /proc/loadavg ever produces.
bool parseHundredths(const std::string& token, Int32U& hundredths) {
  if (token.empty()) return false;

  // Largest whole part that still leaves room for two fraction digits in an
  // Int32U: (2^32 - 1 - 99) / 100.
  const Int32U maxWhole = 42949671UL;

  Int32U whole = 0;
  size_t i = 0;
  for (; i < token.size() && token[i] >= '0' && token[i] <= '9'; ++i) {
    const Int32U digit = (Int32U) (token[i] - '0');
    if (whole > (maxWhole - digit) / 10) return false;
    whole = whole * 10 + digit;
  }
  if (i == 0) return false;  // no leading digit

  Int32U fraction = 0;
  if (i < token.size()) {
    if (token[i] != '.') return false;
    ++i;
    size_t fractionDigits = 0;
    for (; i < token.size() && token[i] >= '0' && token[i] <= '9'; ++i) {
      if (++fractionDigits > 2) return false;
      fraction = fraction * 10 + (Int32U) (token[i] - '0');
    }
    if (fractionDigits == 0) return false;  // a trailing bare '.'
    if (fractionDigits == 1) fraction *= 10;
    if (i != token.size()) return false;  // trailing junk
  }

  hundredths = whole * 100 + fraction;
  return true;
}

bool readLoadAverages(SystemStatus& systemStatus, const char* loadavgPath) {
  std::ifstream input(loadavgPath);
  std::string line;
  if (!input || !std::getline(input, line)) return false;

  std::istringstream tokens(line);
  std::string oneMinute, fiveMinute, fifteenMinute;
  if (!(tokens >> oneMinute >> fiveMinute >> fifteenMinute)) return false;

  return parseHundredths(oneMinute, systemStatus.loadAvg1Hundredths) &&
         parseHundredths(fiveMinute, systemStatus.loadAvg5Hundredths) &&
         parseHundredths(fifteenMinute, systemStatus.loadAvg15Hundredths);
}

// "MemFree:          123456 kB". The name is compared whole rather than by
// prefix, so MemFree is never answered with MemFreeSomethingElse.
bool parseMeminfoLine(const std::string& line, const char* name, Int64U& value) {
  const size_t colon = line.find(':');
  if (colon == std::string::npos) return false;
  if (line.compare(0, colon, name) != 0) return false;

  std::istringstream rest(line.substr(colon + 1));
  Int64U parsed;
  if (!(rest >> parsed)) return false;

  value = parsed;
  return true;
}

bool readMemInfo(SystemStatus& systemStatus, const char* meminfoPath) {
  std::ifstream input(meminfoPath);
  if (!input) return false;

  bool haveFree = false;
  bool haveAvailable = false;
  bool haveTotal = false;

  std::string line;
  while (std::getline(input, line)) {
    if (!haveTotal && parseMeminfoLine(line, "MemTotal", systemStatus.memTotalKb)) {
      haveTotal = true;
    } else if (!haveFree && parseMeminfoLine(line, "MemFree", systemStatus.memFreeKb)) {
      haveFree = true;
    } else if (!haveAvailable &&
               parseMeminfoLine(line, "MemAvailable", systemStatus.memAvailableKb)) {
      haveAvailable = true;
    }
  }

  // MemAvailable is a bonus; MemFree is the value that was asked for. Kernels
  // older than 3.14 have no MemAvailable, and reporting nothing at all because
  // of that would be the wrong trade.
  return haveFree && haveTotal;
}

void renderTextSystem(std::string& buff, const SystemStatus& systemStatus) {
  buff << "cpu_load: ";
  if (systemStatus.loadValid) {
    appendLoad(buff, systemStatus.loadAvg1Hundredths); buff << " ";
    appendLoad(buff, systemStatus.loadAvg5Hundredths); buff << " ";
    appendLoad(buff, systemStatus.loadAvg15Hundredths);
  } else {
    buff << "unavailable";
  }
  buff << "\n";

  const char* const memNames[] = {"mem_free_kb", "mem_available_kb", "mem_total_kb"};
  const Int64U memValues[] = {systemStatus.memFreeKb, systemStatus.memAvailableKb,
                              systemStatus.memTotalKb};
  for (size_t i = 0; i < sizeof(memNames) / sizeof(memNames[0]); ++i) {
    buff << memNames[i] << ": ";
    if (systemStatus.memValid) {
      appendUInt64(buff, memValues[i]);
    } else {
      buff << "unavailable";
    }
    buff << "\n";
  }
}

void renderJsonSystem(std::string& buff, const SystemStatus& systemStatus) {
  buff << "  \"system\": {\n";

  buff << "    \"cpu_load\": ";
  if (systemStatus.loadValid) {
    buff << "{\"1min\": ";
    appendLoad(buff, systemStatus.loadAvg1Hundredths);
    buff << ", \"5min\": ";
    appendLoad(buff, systemStatus.loadAvg5Hundredths);
    buff << ", \"15min\": ";
    appendLoad(buff, systemStatus.loadAvg15Hundredths);
    buff << "}";
  } else {
    buff << "null";
  }
  buff << ",\n";

  const char* const memNames[] = {"mem_free_kb", "mem_available_kb", "mem_total_kb"};
  const Int64U memValues[] = {systemStatus.memFreeKb, systemStatus.memAvailableKb,
                              systemStatus.memTotalKb};
  for (size_t i = 0; i < sizeof(memNames) / sizeof(memNames[0]); ++i) {
    buff << "    \"" << memNames[i] << "\": ";
    if (systemStatus.memValid) {
      appendUInt64(buff, memValues[i]);
    } else {
      buff << "null";
    }
    buff << (i + 1 < sizeof(memNames) / sizeof(memNames[0]) ? ",\n" : "\n");
  }

  buff << "  },\n";
}

void renderJsonMqtt(std::string& buff, const StatusSnapshot& snapshot) {
  buff << "  \"mqtt\": {\n";
  buff << "    \"broker_connected\": ";
  appendJsonBool(buff, snapshot.mqtt.mqttBrokerConnected);
  buff << ",\n    \"broker_ip\": ";
  appendJsonString(buff, snapshot.mqttBrokerIp);
  buff << ",\n    \"broker_port\": ";
  appendInt(buff, snapshot.mqtt.mqttBrokerPort);
  buff << ",\n    \"keep_alive_secs\": ";
  appendInt(buff, snapshot.mqtt.mqttKeepAlive);
  buff << ",\n    \"last_loop_rc\": ";
  appendInt(buff, snapshot.mqtt.last_loop_rc);
  buff << ",\n    \"last_loop_rc_str\": ";
  appendJsonString(buff, snapshot.mqttLastLoopRcStr);

  const char* const counterNames[] = {
      "connect_attempts", "connects",          "disconnects",       "publishes",
      "published_motions", "publishes_dropped", "publish_callbacks", "messages",
      "ticks"};
  const Int32U counterValues[] = {
      snapshot.mqtt.connectAttempts, snapshot.mqtt.connects,
      snapshot.mqtt.disconnects,     snapshot.mqtt.publishes,
      snapshot.mqtt.publishedMotions, snapshot.mqtt.publishesDropped,
      snapshot.mqtt.publishCallbacks, snapshot.mqtt.messages,
      snapshot.mqtt.ticks};
  for (size_t i = 0; i < sizeof(counterNames) / sizeof(counterNames[0]); ++i) {
    buff << ",\n    \"" << counterNames[i] << "\": ";
    appendInt(buff, (int) counterValues[i]);
  }

  buff << "\n  },\n";
}

void renderJsonPairs(std::string& buff,
                     const std::vector<std::pair<std::string, std::string> >& entries,
                     const char* indent) {
  for (size_t i = 0; i < entries.size(); ++i) {
    buff << indent;
    appendJsonString(buff, entries[i].first);
    buff << ": ";
    appendJsonString(buff, entries[i].second);
    buff << (i + 1 < entries.size() ? ",\n" : "\n");
  }
}

}  // namespace

bool readSystemStatus(SystemStatus& systemStatus, const char* loadavgPath,
                      const char* meminfoPath) {
  memset(&systemStatus, 0, sizeof(systemStatus));

  systemStatus.loadValid = readLoadAverages(systemStatus, loadavgPath);
  systemStatus.memValid = readMemInfo(systemStatus, meminfoPath);

  // A partial read must not leave half-parsed numbers behind for a caller that
  // ignores the valid flags.
  if (!systemStatus.loadValid) {
    systemStatus.loadAvg1Hundredths = 0;
    systemStatus.loadAvg5Hundredths = 0;
    systemStatus.loadAvg15Hundredths = 0;
  }
  if (!systemStatus.memValid) {
    systemStatus.memFreeKb = 0;
    systemStatus.memAvailableKb = 0;
    systemStatus.memTotalKb = 0;
  }

  return systemStatus.loadValid && systemStatus.memValid;
}

std::string jsonEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());

  for (size_t i = 0; i < value.size(); ++i) {
    const unsigned char c = (unsigned char) value[i];
    switch (c) {
      case '"':  escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (c < 0x20) {
          // Includes NUL, which is why the rendered document can safely be
          // handed to an API that measures it with strlen().
          char unicode[7];
          snprintf(unicode, sizeof(unicode), "\\u%04x", (unsigned int) c);
          escaped += unicode;
        } else {
          // Anything at or above 0x20 needs no escape, UTF-8 bytes included.
          escaped += (char) c;
        }
        break;
    }
  }
  return escaped;
}

std::string renderStatusText(const StatusSnapshot& snapshot) {
  std::string buff("Stats and status\n\n");

  if (snapshot.webHandlerAvailable) {
    for (size_t i = 0; i < snapshot.handlerHits.size(); ++i) {
      buff << "hitCount: " << snapshot.handlerHits[i].first << " = "
           << std::to_string(snapshot.handlerHits[i].second) << "\n";
    }
    buff << "\n";
  }

  buff << "motion: " << (snapshot.motion.currMotionDetected ? "y" : "n") << "\n";
  buff << "motion_last_change: ";
  appendInt(buff, snapshot.motion.lastChangedHour); buff << ":";
  appendInt(buff, snapshot.motion.lastChangedMin); buff << ":";
  appendInt(buff, snapshot.motion.lastChangedSec); buff << "\n";

  buff << "light_sensor: "; appendInt(buff, (int) snapshot.lightValue); buff << "\n";
  buff << "display_mode: " << snapshot.displayMode << "\n";
  buff << "display_dimmed: " << (snapshot.displayDimmed ? "yes" : "no") << "\n";
  buff << "led_strip_mode: " << snapshot.ledStripMode << "\n";

  buff << "\n";
  renderTextSystem(buff, snapshot.system);

  buff << "\n";
  buff << "mqttBrokerConnected: " << (snapshot.mqtt.mqttBrokerConnected ? "yes" : "no") << "\n";
  buff << "mqttLastLoopRc: "; appendInt(buff, snapshot.mqtt.last_loop_rc);
  buff << " (" << snapshot.mqttLastLoopRcStr << ")" << "\n";
  buff << "mqttBrokerIp: " << snapshot.mqttBrokerIp << "\n";
  buff << "mqttBrokerPort: "; appendInt(buff, snapshot.mqtt.mqttBrokerPort); buff << "\n";
  buff << "mqttKeepAlive (secs): "; appendInt(buff, snapshot.mqtt.mqttKeepAlive); buff << "\n";
  buff << "mqttConnectAttempts: "; appendInt(buff, (int) snapshot.mqtt.connectAttempts); buff << "\n";
  buff << "mqttConnects: "; appendInt(buff, (int) snapshot.mqtt.connects); buff << "\n";
  buff << "mqttDisconnects: "; appendInt(buff, (int) snapshot.mqtt.disconnects); buff << "\n";
  buff << "mqttPublishes: "; appendInt(buff, (int) snapshot.mqtt.publishes); buff << "\n";
  buff << "mqttPublishedMotions: "; appendInt(buff, (int) snapshot.mqtt.publishedMotions); buff << "\n";
  buff << "mqttPublishesDropped: "; appendInt(buff, (int) snapshot.mqtt.publishesDropped); buff << "\n";
  buff << "mqttPublishCallbacks: "; appendInt(buff, (int) snapshot.mqtt.publishCallbacks); buff << "\n";
  buff << "mqttMessages: "; appendInt(buff, (int) snapshot.mqtt.messages); buff << "\n";
  buff << "mqttTicks: "; appendInt(buff, (int) snapshot.mqtt.ticks); buff << "\n";

  buff << "\n";
  buff << "dictSize: "; appendInt(buff, (int) snapshot.dictionarySize); buff << "\n";
  buff << "dictTicks: "; appendInt(buff, (int) snapshot.dictionaryStatus.ticks); buff << "\n";
  buff << "dictAdds: "; appendInt(buff, (int) snapshot.dictionaryStatus.entriesAdded); buff << "\n";
  buff << "dictRemoves: "; appendInt(buff, (int) snapshot.dictionaryStatus.entriesRemoved); buff << "\n";
  buff << "dictExpires: "; appendInt(buff, (int) snapshot.dictionaryStatus.entriesExpired); buff << "\n";

  if (!snapshot.dictionaryEntries.empty()) {
    buff << "\n";
    for (size_t i = 0; i < snapshot.dictionaryEntries.size(); ++i) {
      buff << "dict: " << snapshot.dictionaryEntries[i].first << " => "
           << snapshot.dictionaryEntries[i].second << "\n";
    }
  }

  return buff;
}

std::string renderStatusJson(const StatusSnapshot& snapshot) {
  std::string buff("{\n");

  buff << "  \"version\": ";
  appendInt(buff, statusJsonVersion);
  buff << ",\n";

  renderJsonSystem(buff, snapshot.system);

  buff << "  \"motion\": {\n";
  buff << "    \"detected\": ";
  appendJsonBool(buff, snapshot.motion.currMotionDetected);
  buff << ",\n    \"last_change_hour\": ";
  appendInt(buff, snapshot.motion.lastChangedHour);
  buff << ",\n    \"last_change_min\": ";
  appendInt(buff, snapshot.motion.lastChangedMin);
  buff << ",\n    \"last_change_sec\": ";
  appendInt(buff, snapshot.motion.lastChangedSec);
  buff << "\n  },\n";

  buff << "  \"light_sensor\": ";
  appendInt(buff, (int) snapshot.lightValue);
  buff << ",\n";

  buff << "  \"display\": {\"mode\": ";
  appendJsonString(buff, snapshot.displayMode);
  buff << ", \"dimmed\": ";
  appendJsonBool(buff, snapshot.displayDimmed);
  buff << "},\n";

  buff << "  \"led_strip\": {\"mode\": ";
  appendJsonString(buff, snapshot.ledStripMode);
  buff << "},\n";

  renderJsonMqtt(buff, snapshot);

  buff << "  \"dictionary\": {\n";
  buff << "    \"size\": "; appendInt(buff, (int) snapshot.dictionarySize);
  buff << ",\n    \"ticks\": "; appendInt(buff, (int) snapshot.dictionaryStatus.ticks);
  buff << ",\n    \"adds\": "; appendInt(buff, (int) snapshot.dictionaryStatus.entriesAdded);
  buff << ",\n    \"removes\": "; appendInt(buff, (int) snapshot.dictionaryStatus.entriesRemoved);
  buff << ",\n    \"expires\": "; appendInt(buff, (int) snapshot.dictionaryStatus.entriesExpired);
  buff << ",\n    \"entries\": {";
  if (snapshot.dictionaryEntries.empty()) {
    buff << "}\n";
  } else {
    buff << "\n";
    renderJsonPairs(buff, snapshot.dictionaryEntries, "      ");
    buff << "    }\n";
  }
  buff << "  },\n";

  buff << "  \"handlers\": {\"hits\": {";
  if (!snapshot.webHandlerAvailable || snapshot.handlerHits.empty()) {
    buff << "}}\n";
  } else {
    buff << "\n";
    for (size_t i = 0; i < snapshot.handlerHits.size(); ++i) {
      buff << "    ";
      appendJsonString(buff, snapshot.handlerHits[i].first);
      buff << ": ";
      appendInt(buff, (int) snapshot.handlerHits[i].second);
      buff << (i + 1 < snapshot.handlerHits.size() ? ",\n" : "\n");
    }
    buff << "  }}\n";
  }

  buff << "}\n";
  return buff;
}
