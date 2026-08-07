#include "statusReport.h"

#include "display.h"
#include "ledStrip.h"
#include "lightSensor.h"
#include "webHandlerInternal.h"

// The half of the status report that reaches into the running application.
// Split from statusReport.cpp so that the renderers, which is where the
// interesting logic lives, can be unit tested without libevent, mosquitto or
// any of the threads.
//
// Every read below is already performed from a thread that does not own the
// data, and each singleton guards its own state, so this introduces no new
// lock ordering. It is called from the web worker threads (for /status and
// /status.json) and from the MQTT thread (for the periodic report).

void gatherStatusSnapshot(StatusSnapshot& snapshot) {
  WebHandlerInternal* const webHandlerInternal = WebHandlerInternal::bindIfExists();
  snapshot.webHandlerAvailable = webHandlerInternal != nullptr;
  snapshot.handlerHits.clear();
  if (webHandlerInternal != nullptr) {
    webHandlerInternal->getHandlerHits(snapshot.handlerHits);
  }

  MotionSensor::bind().getMotionValue(&snapshot.motion);
  snapshot.lightValue = LightSensor::bind().getLightValue();

  Display& display = Display::bind();
  snapshot.displayMode = display.getInternalDisplayMode();
  snapshot.displayDimmed = display.getInternalDisplayDimmed();
  snapshot.ledStripMode = LedStrip::bind().getInternalLedStripMode();

  MqttClient& mqttClient = MqttClient::bind();
  mqttClient.getMqttClientInfo(&snapshot.mqtt);
  snapshot.mqttBrokerIp =
      snapshot.mqtt.mqttBrokerIp == nullptr ? "" : snapshot.mqtt.mqttBrokerIp;
  snapshot.mqttLastLoopRcStr = MqttClient::getStrError(snapshot.mqtt.last_loop_rc);

  Dictionary& dictionary = Dictionary::bind();
  dictionary.getDictionaryStatus(snapshot.dictionaryStatus);
  snapshot.dictionarySize = dictionary.size();
  snapshot.dictionaryEntries.clear();
  {
    bool found;
    std::string currKey;
    std::string currData = dictionary.getFirst(currKey, &found);
    while (found) {
      snapshot.dictionaryEntries.push_back(std::make_pair(currKey, currData));
      currData = dictionary.getNext(currKey, &found);
    }
  }

  readSystemStatus(snapshot.system);
}
