#include "statusReport.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

// ======================================================================
// Fixture helpers, following tests/iio_analog_input_tests.cpp.

void writeFile(const std::string& path, const std::string& value) {
  std::ofstream output(path.c_str());
  assert(output);
  output << value;
  output.close();
  assert(output);
}

class Fixture {
public:
  Fixture() : root_() {
    char pattern[] = "/tmp/oclock-status-test-XXXXXXXX";
    char* root = mkdtemp(pattern);
    assert(root != NULL);
    root_ = root;
  }

  ~Fixture() {
    for (size_t i = 0; i < files_.size(); ++i) unlink(files_[i].c_str());
    rmdir(root_.c_str());
  }

  std::string put(const std::string& name, const std::string& contents) {
    const std::string path = root_ + "/" + name;
    writeFile(path, contents);
    files_.push_back(path);
    return path;
  }

  std::string missing(const std::string& name) const { return root_ + "/" + name; }

private:
  std::string root_;
  std::vector<std::string> files_;
};

const char realMeminfo[] =
    "MemTotal:         493832 kB\n"
    "MemFree:          123456 kB\n"
    "MemAvailable:     234567 kB\n"
    "Buffers:            8192 kB\n"
    "Cached:           100000 kB\n";

// ======================================================================
// A minimal JSON validator. Deliberately not `python3 -m json.tool`: the
// Trixie container the suite runs in has no python3, and shelling out of a
// C++ test to prove the C++ output parses would be the wrong shape anyway.
//
// It is strict where it matters for this code: a raw byte below 0x20 inside a
// string literal is rejected, which is exactly the bug jsonEscape() exists to
// prevent.

class JsonChecker {
public:
  explicit JsonChecker(const std::string& text) : text_(text), pos_(0) {}

  bool valid() {
    skipWhitespace();
    if (!parseValue()) return false;
    skipWhitespace();
    return pos_ == text_.size();
  }

private:
  bool atEnd() const { return pos_ >= text_.size(); }
  char peek() const { return text_[pos_]; }

  void skipWhitespace() {
    while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) {
      ++pos_;
    }
  }

  bool literal(const char* expected) {
    const size_t length = strlen(expected);
    if (text_.compare(pos_, length, expected) != 0) return false;
    pos_ += length;
    return true;
  }

  bool parseHexDigit() {
    if (atEnd()) return false;
    const char c = peek();
    const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!isHex) return false;
    ++pos_;
    return true;
  }

  bool parseString() {
    if (atEnd() || peek() != '"') return false;
    ++pos_;
    while (true) {
      if (atEnd()) return false;
      const unsigned char c = (unsigned char) text_[pos_];
      if (c == '"') { ++pos_; return true; }
      if (c < 0x20) return false;  // an unescaped control character
      if (c == '\\') {
        ++pos_;
        if (atEnd()) return false;
        const char escape = text_[pos_++];
        switch (escape) {
          case '"': case '\\': case '/': case 'b':
          case 'f': case 'n': case 'r': case 't':
            break;
          case 'u':
            for (int i = 0; i < 4; ++i) {
              if (!parseHexDigit()) return false;
            }
            break;
          default:
            return false;
        }
        continue;
      }
      ++pos_;
    }
  }

  bool parseDigits() {
    const size_t started = pos_;
    while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
    return pos_ > started;
  }

  bool parseNumber() {
    if (!atEnd() && peek() == '-') ++pos_;
    if (!parseDigits()) return false;
    if (!atEnd() && peek() == '.') {
      ++pos_;
      if (!parseDigits()) return false;
    }
    if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
      ++pos_;
      if (!atEnd() && (peek() == '+' || peek() == '-')) ++pos_;
      if (!parseDigits()) return false;
    }
    return true;
  }

  bool parseObject() {
    ++pos_;  // '{'
    skipWhitespace();
    if (!atEnd() && peek() == '}') { ++pos_; return true; }
    while (true) {
      skipWhitespace();
      if (!parseString()) return false;
      skipWhitespace();
      if (atEnd() || peek() != ':') return false;
      ++pos_;
      skipWhitespace();
      if (!parseValue()) return false;
      skipWhitespace();
      if (atEnd()) return false;
      if (peek() == ',') { ++pos_; continue; }
      if (peek() == '}') { ++pos_; return true; }
      return false;
    }
  }

  bool parseArray() {
    ++pos_;  // '['
    skipWhitespace();
    if (!atEnd() && peek() == ']') { ++pos_; return true; }
    while (true) {
      skipWhitespace();
      if (!parseValue()) return false;
      skipWhitespace();
      if (atEnd()) return false;
      if (peek() == ',') { ++pos_; continue; }
      if (peek() == ']') { ++pos_; return true; }
      return false;
    }
  }

  bool parseValue() {
    if (atEnd()) return false;
    switch (peek()) {
      case '{': return parseObject();
      case '[': return parseArray();
      case '"': return parseString();
      case 't': return literal("true");
      case 'f': return literal("false");
      case 'n': return literal("null");
      default:  return parseNumber();
    }
  }

  const std::string& text_;
  size_t pos_;
};

bool isValidJson(const std::string& text) {
  JsonChecker checker(text);
  return checker.valid();
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// ======================================================================
// A snapshot with every field set to something recognisable.

StatusSnapshot makeSnapshot() {
  StatusSnapshot snapshot;

  snapshot.webHandlerAvailable = true;
  snapshot.handlerHits.push_back(std::make_pair(std::string("/ (get)"), (Int32U) 3));
  snapshot.handlerHits.push_back(std::make_pair(std::string("/status (get)"), (Int32U) 7));

  memset(&snapshot.motion, 0, sizeof(snapshot.motion));
  snapshot.motion.currMotionDetected = true;
  snapshot.motion.lastChangedHour = 1;
  snapshot.motion.lastChangedMin = 2;
  snapshot.motion.lastChangedSec = 3;

  snapshot.lightValue = 512;
  snapshot.displayMode = "clock";
  snapshot.displayDimmed = false;
  snapshot.ledStripMode = "off";

  memset(&snapshot.mqtt, 0, sizeof(snapshot.mqtt));
  snapshot.mqtt.mqttBrokerPort = 1883;
  snapshot.mqtt.mqttKeepAlive = 182;
  snapshot.mqtt.mqttBrokerConnected = true;
  snapshot.mqtt.last_loop_rc = 0;
  snapshot.mqtt.connectAttempts = 1;
  snapshot.mqtt.connects = 1;
  snapshot.mqtt.disconnects = 0;
  snapshot.mqtt.publishes = 12;
  snapshot.mqtt.publishedMotions = 2;
  snapshot.mqtt.publishesDropped = 0;
  snapshot.mqtt.publishCallbacks = 12;
  snapshot.mqtt.messages = 0;
  snapshot.mqtt.ticks = 400;
  snapshot.mqttBrokerIp = "192.168.10.238";
  snapshot.mqttLastLoopRcStr = "No error.";

  memset(&snapshot.dictionaryStatus, 0, sizeof(snapshot.dictionaryStatus));
  snapshot.dictionaryStatus.ticks = 5;
  snapshot.dictionaryStatus.entriesAdded = 3;
  snapshot.dictionaryStatus.entriesRemoved = 1;
  snapshot.dictionaryStatus.entriesExpired = 0;
  snapshot.dictionarySize = 2;
  snapshot.dictionaryEntries.push_back(
      std::make_pair(std::string("/sensor/temperature_outside"), std::string("71")));
  snapshot.dictionaryEntries.push_back(
      std::make_pair(std::string("/garage/humidity"), std::string("38")));

  memset(&snapshot.system, 0, sizeof(snapshot.system));
  snapshot.system.loadValid = true;
  snapshot.system.loadAvg1Hundredths = 14;
  snapshot.system.loadAvg5Hundredths = 9;
  snapshot.system.loadAvg15Hundredths = 5;
  snapshot.system.memValid = true;
  snapshot.system.memFreeKb = 123456;
  snapshot.system.memAvailableKb = 234567;
  snapshot.system.memTotalKb = 493832;

  return snapshot;
}

// ======================================================================

void testSystemStatusFromRealisticProcFiles() {
  Fixture fixture;
  const std::string loadavg = fixture.put("loadavg", "0.14 0.09 0.05 1/234 5678\n");
  const std::string meminfo = fixture.put("meminfo", realMeminfo);

  SystemStatus systemStatus;
  assert(readSystemStatus(systemStatus, loadavg.c_str(), meminfo.c_str()));

  assert(systemStatus.loadValid);
  assert(systemStatus.loadAvg1Hundredths == 14);
  assert(systemStatus.loadAvg5Hundredths == 9);
  assert(systemStatus.loadAvg15Hundredths == 5);

  assert(systemStatus.memValid);
  assert(systemStatus.memFreeKb == 123456);
  assert(systemStatus.memAvailableKb == 234567);
  assert(systemStatus.memTotalKb == 493832);
}

void testLoadAverageParsing() {
  Fixture fixture;
  const std::string meminfo = fixture.put("meminfo", realMeminfo);

  struct {
    const char* line;
    bool valid;
    Int32U first;
  } const cases[] = {
      {"1.00 2.50 12.34 1/2 3\n", true, 100},
      {"0.00 0.00 0.00 1/2 3\n", true, 0},
      {"7 8 9 1/2 3\n", true, 700},          // no fraction at all
      {"0.5 0.5 0.5 1/2 3\n", true, 50},     // one fraction digit
      {"1.234 0.09 0.05\n", false, 0},       // three fraction digits
      {"-1.00 0.09 0.05\n", false, 0},       // negative
      {"1.00 0.09\n", false, 0},             // truncated
      {"nan nan nan\n", false, 0},
      {"1. 0.09 0.05\n", false, 0},
      {"1.00x 0.09 0.05\n", false, 0},
      {"99999999999.00 0.09 0.05\n", false, 0},
      {"\n", false, 0},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    const std::string loadavg = fixture.put("loadavg-case", cases[i].line);
    SystemStatus systemStatus;
    readSystemStatus(systemStatus, loadavg.c_str(), meminfo.c_str());
    assert(systemStatus.loadValid == cases[i].valid);
    if (cases[i].valid) {
      assert(systemStatus.loadAvg1Hundredths == cases[i].first);
    } else {
      // A rejected read must not leave a half-parsed number behind.
      assert(systemStatus.loadAvg1Hundredths == 0);
      assert(systemStatus.loadAvg5Hundredths == 0);
      assert(systemStatus.loadAvg15Hundredths == 0);
    }
  }
}

void testMemInfoParsing() {
  Fixture fixture;
  const std::string loadavg = fixture.put("loadavg", "0.14 0.09 0.05 1/234 5678\n");

  // A key that merely starts with MemFree must not answer for MemFree, and the
  // real value that follows it must still be found.
  const std::string decoy = fixture.put(
      "meminfo-decoy",
      "MemFreeSwap:       999999 kB\n"
      "MemTotal:         493832 kB\n"
      "MemFree:          123456 kB\n");
  SystemStatus systemStatus;
  assert(readSystemStatus(systemStatus, loadavg.c_str(), decoy.c_str()));
  assert(systemStatus.memValid);
  assert(systemStatus.memFreeKb == 123456);
  assert(systemStatus.memAvailableKb == 0);  // absent, and that is not fatal

  // MemFree is the value that was asked for; without it there is no report.
  const std::string noFree = fixture.put("meminfo-nofree", "MemTotal:  493832 kB\n");
  assert(!readSystemStatus(systemStatus, loadavg.c_str(), noFree.c_str()));
  assert(!systemStatus.memValid);
  assert(systemStatus.memFreeKb == 0);
  assert(systemStatus.loadValid);  // the load half is independent

  const std::string junk = fixture.put("meminfo-junk", "MemFree: not-a-number\nMemTotal: x\n");
  assert(!readSystemStatus(systemStatus, loadavg.c_str(), junk.c_str()));
  assert(!systemStatus.memValid);
}

void testMissingProcFilesAreNotFatal() {
  Fixture fixture;
  SystemStatus systemStatus;

  assert(!readSystemStatus(systemStatus, fixture.missing("nope").c_str(),
                           fixture.missing("also-nope").c_str()));
  assert(!systemStatus.loadValid);
  assert(!systemStatus.memValid);
  assert(systemStatus.loadAvg1Hundredths == 0);
  assert(systemStatus.memFreeKb == 0);
}

void testJsonEscape() {
  assert(jsonEscape("plain") == "plain");
  assert(jsonEscape("say \"hi\"") == "say \\\"hi\\\"");
  assert(jsonEscape("back\\slash") == "back\\\\slash");
  assert(jsonEscape("a\nb") == "a\\nb");
  assert(jsonEscape("a\tb") == "a\\tb");
  assert(jsonEscape("a\rb") == "a\\rb");
  assert(jsonEscape("a\bb") == "a\\bb");
  assert(jsonEscape("a\fb") == "a\\fb");
  assert(jsonEscape(std::string("a\x01" "b")) == "a\\u0001b");

  // NUL is the one that makes the MQTT publish path safe, because doPublish()
  // measures the payload with strlen().
  const std::string withNul("a\0b", 3);
  assert(withNul.size() == 3);
  assert(jsonEscape(withNul) == "a\\u0000b");

  // UTF-8 passes through untouched; escaping it would corrupt it.
  assert(jsonEscape("caf\xc3\xa9") == "caf\xc3\xa9");
  // DEL is 0x7f, which JSON does not require escaping.
  assert(jsonEscape("a\x7f" "b") == "a\x7f" "b");
}

// The legacy text page, pinned byte for byte. Everything in this string was
// already served by /status before the system block and display_dimmed were
// added; if a change here is intentional, it is an API change.
void testRenderStatusTextIsTheLegacyPage() {
  const StatusSnapshot snapshot = makeSnapshot();

  const std::string expected =
      "Stats and status\n"
      "\n"
      "hitCount: / (get) = 3\n"
      "hitCount: /status (get) = 7\n"
      "\n"
      "motion: y\n"
      "motion_last_change: 1:2:3\n"
      "light_sensor: 512\n"
      "display_mode: clock\n"
      "display_dimmed: no\n"
      "led_strip_mode: off\n"
      "\n"
      "cpu_load: 0.14 0.09 0.05\n"
      "mem_free_kb: 123456\n"
      "mem_available_kb: 234567\n"
      "mem_total_kb: 493832\n"
      "\n"
      "mqttBrokerConnected: yes\n"
      "mqttLastLoopRc: 0 (No error.)\n"
      "mqttBrokerIp: 192.168.10.238\n"
      "mqttBrokerPort: 1883\n"
      "mqttKeepAlive (secs): 182\n"
      "mqttConnectAttempts: 1\n"
      "mqttConnects: 1\n"
      "mqttDisconnects: 0\n"
      "mqttPublishes: 12\n"
      "mqttPublishedMotions: 2\n"
      "mqttPublishesDropped: 0\n"
      "mqttPublishCallbacks: 12\n"
      "mqttMessages: 0\n"
      "mqttTicks: 400\n"
      "\n"
      "dictSize: 2\n"
      "dictTicks: 5\n"
      "dictAdds: 3\n"
      "dictRemoves: 1\n"
      "dictExpires: 0\n"
      "\n"
      "dict: /sensor/temperature_outside => 71\n"
      "dict: /garage/humidity => 38\n";

  const std::string rendered = renderStatusText(snapshot);
  if (rendered != expected) {
    std::cerr << "--- rendered ---\n" << rendered << "--- expected ---\n" << expected;
  }
  assert(rendered == expected);
}

void testRenderStatusTextEdgeCases() {
  StatusSnapshot snapshot = makeSnapshot();

  // Before the web server is up the page carried no hit counts at all, not an
  // empty block.
  snapshot.webHandlerAvailable = false;
  snapshot.handlerHits.clear();
  assert(renderStatusText(snapshot).find("hitCount:") == std::string::npos);
  assert(renderStatusText(snapshot).find("Stats and status\n\nmotion: y\n") == 0);

  // An empty dictionary suppresses the trailing block, as it always did.
  snapshot.dictionaryEntries.clear();
  snapshot.dictionarySize = 0;
  const std::string empty = renderStatusText(snapshot);
  assert(empty.find("dict: ") == std::string::npos);
  assert(contains(empty, "dictExpires: 0\n"));
  assert(empty[empty.size() - 1] == '\n');

  // An unreadable /proc says so rather than reporting a zero.
  snapshot.system.loadValid = false;
  snapshot.system.memValid = false;
  const std::string unavailable = renderStatusText(snapshot);
  assert(contains(unavailable, "cpu_load: unavailable\n"));
  assert(contains(unavailable, "mem_free_kb: unavailable\n"));
  assert(contains(unavailable, "mem_available_kb: unavailable\n"));
  assert(contains(unavailable, "mem_total_kb: unavailable\n"));

  snapshot.displayDimmed = true;
  assert(contains(renderStatusText(snapshot), "display_dimmed: yes\n"));
}

void testRenderStatusJson() {
  const StatusSnapshot snapshot = makeSnapshot();
  const std::string json = renderStatusJson(snapshot);

  if (!isValidJson(json)) std::cerr << "--- invalid json ---\n" << json;
  assert(isValidJson(json));

  assert(contains(json, "\"version\": 1"));
  assert(contains(json, "\"cpu_load\": {\"1min\": 0.14, \"5min\": 0.09, \"15min\": 0.05}"));
  assert(contains(json, "\"mem_free_kb\": 123456"));
  assert(contains(json, "\"mem_available_kb\": 234567"));
  assert(contains(json, "\"mem_total_kb\": 493832"));
  assert(contains(json, "\"dimmed\": false"));
  assert(contains(json, "\"mode\": \"clock\""));
  assert(contains(json, "\"light_sensor\": 512"));
  assert(contains(json, "\"detected\": true"));
  assert(contains(json, "\"broker_connected\": true"));
  assert(contains(json, "\"broker_ip\": \"192.168.10.238\""));
  assert(contains(json, "\"last_loop_rc_str\": \"No error.\""));
  assert(contains(json, "\"published_motions\": 2"));
  assert(contains(json, "\"/sensor/temperature_outside\": \"71\""));
  assert(contains(json, "\"/status (get)\": 7"));

  // The property doPublish() depends on: no embedded NUL, so strlen() and
  // size() agree on how much payload there is.
  assert(json.size() == strlen(json.c_str()));
}

void testRenderStatusJsonEscapesHostileDictionaryEntries() {
  StatusSnapshot snapshot = makeSnapshot();

  // Dictionary keys and values arrive from HTTP POSTs, so they are attacker
  // shaped by construction.
  snapshot.dictionaryEntries.clear();
  snapshot.dictionaryEntries.push_back(
      std::make_pair(std::string("quote\"key"), std::string("back\\slash\nnewline")));
  snapshot.dictionaryEntries.push_back(
      std::make_pair(std::string("ctrl\x01key"), std::string("tab\there")));
  snapshot.dictionaryEntries.push_back(
      std::make_pair(std::string("brace}\"], \"injected\": \"x"), std::string("}")));
  snapshot.dictionarySize = snapshot.dictionaryEntries.size();
  snapshot.displayMode = "still \"quoted\"";
  snapshot.mqttBrokerIp = "back\\slash";

  const std::string json = renderStatusJson(snapshot);
  if (!isValidJson(json)) std::cerr << "--- invalid json ---\n" << json;
  assert(isValidJson(json));

  assert(contains(json, "\"quote\\\"key\""));
  assert(contains(json, "\"back\\\\slash\\nnewline\""));
  assert(contains(json, "\"ctrl\\u0001key\""));
  assert(!contains(json, "\"injected\": \"x\""));
  assert(json.size() == strlen(json.c_str()));
}

void testRenderStatusJsonWithUnreadableProc() {
  StatusSnapshot snapshot = makeSnapshot();
  snapshot.system.loadValid = false;
  snapshot.system.memValid = false;

  const std::string json = renderStatusJson(snapshot);
  assert(isValidJson(json));
  assert(contains(json, "\"cpu_load\": null"));
  assert(contains(json, "\"mem_free_kb\": null"));
  assert(contains(json, "\"mem_available_kb\": null"));
  assert(contains(json, "\"mem_total_kb\": null"));
}

void testRenderStatusJsonWithEmptyCollections() {
  StatusSnapshot snapshot = makeSnapshot();
  snapshot.dictionaryEntries.clear();
  snapshot.dictionarySize = 0;
  snapshot.handlerHits.clear();
  snapshot.webHandlerAvailable = false;

  const std::string json = renderStatusJson(snapshot);
  if (!isValidJson(json)) std::cerr << "--- invalid json ---\n" << json;
  assert(isValidJson(json));
  assert(contains(json, "\"entries\": {}"));
  assert(contains(json, "\"hits\": {}"));
}

void testJsonCheckerRejectsBadInput() {
  // The validator is only worth anything if it can fail.
  assert(isValidJson("{\"a\": 1}"));
  assert(!isValidJson("{\"a\": 1,}"));
  assert(!isValidJson("{\"a\" 1}"));
  assert(!isValidJson("{\"a\": }"));
  assert(!isValidJson("{\"a\": 1} trailing"));
  assert(!isValidJson("{\"a\": \"unterminated}"));
  assert(!isValidJson(std::string("{\"a\": \"raw\ncontrol\"}")));
  assert(!isValidJson("{\"a\": 01x}"));
  assert(!isValidJson(""));
}

}  // namespace

int main() {
  testSystemStatusFromRealisticProcFiles();
  testLoadAverageParsing();
  testMemInfoParsing();
  testMissingProcFilesAreNotFatal();
  testJsonEscape();
  testRenderStatusTextIsTheLegacyPage();
  testRenderStatusTextEdgeCases();
  testJsonCheckerRejectsBadInput();
  testRenderStatusJson();
  testRenderStatusJsonEscapesHostileDictionaryEntries();
  testRenderStatusJsonWithUnreadableProc();
  testRenderStatusJsonWithEmptyCollections();
  std::cout << "status tests passed\n";
  return 0;
}
