#include "mqttClient.h"

#include "commonUtils.h"
#include "conf.h"
#include "threadsMain.h"
#include "timerTick.h"
#include "inbox.h"
#include "lightSensor.h"

#include <getopt.h>
#include <mosquitto.h>
#include <stdlib.h>
#include <string.h>

std::thread::id MqttClient::mainThreadId;  // default 'invalid' value 
std::recursive_mutex MqttClient::instanceMutex;
MqttClient* MqttClient::instance = 0;

/*static*/ const std::string MqttClient::topicPrefix("/officeClock/");
/*static*/ const std::string MqttClient::topicLightSensor(MqttClient::topicPrefix + "light");
/*static*/ const std::string MqttClient::topicDisplayIntensity(MqttClient::topicPrefix + "display_intensity");
/*static*/ const std::string MqttClient::topicDisplayMode(MqttClient::topicPrefix + "display_mode");
/*static*/ const std::string MqttClient::topicMotionDetected(MqttClient::topicPrefix + "last_motion");

MqttClient::MqttClient() : inbox(InboxRegistry::bind().getInbox(threadIdMqttClient)),
                           lightSensor(LightSensor::bind())
{
  // set defaults in mqttClientInfo
  memset(&mqttClientInfo, 0, sizeof(mqttClientInfo));
  mqttClientInfo.mqttBrokerIp = MQTT_BROKER_DEFAULT_IP;
  mqttClientInfo.mqttBrokerPort = MQTT_BROKER_DEFAULT_PORT;
  mqttClientInfo.mqttKeepAlive = MQTT_BROKER_DEFAULT_KA;
  mqttClientInfo.mqttBrokerConnected = false;
}

MqttClient::~MqttClient() {
}

MqttClient& MqttClient::bind() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == 0) {
    instance = new MqttClient();
  }
  return *instance;
}

void MqttClient::shutdown() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  delete instance;
  instance = 0;
}

bool MqttClient::getMqttClientInfo(MqttClientInfo* out) const {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (out != nullptr) *out = mqttClientInfo;
  return mqttClientInfo.mqttBrokerConnected;
}

/*static*/ const char* MqttClient::getStrError(int mosq_errno) {
  return mosquitto_strerror(mosq_errno);
}

/*static*/ void
MqttClient::mqtt_on_connect(struct mosquitto* mosq, void* mqttClientOpaque, int rc) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (mqttClientOpaque != instance) return;
  ++instance->mqttClientInfo.connects;
  /*
   * * 0 - success
   * * 1 - connection refused (unacceptable protocol version)
   * * 2 - connection refused (identifier rejected)
   * * 3 - connection refused (broker unavailable)
   * * 4-255 - reserved for future use
   */
  instance->mqttClientInfo.mqttBrokerConnected = rc == 0 /*MOSQ_ERR_SUCCESS*/;
}
/* static */ void
MqttClient::mqtt_on_disconnect(struct mosquitto* mosq, void* mqttClientOpaque, int /*rc*/) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (mqttClientOpaque != instance) return;
  ++instance->mqttClientInfo.disconnects;
  instance->mqttClientInfo.mqttBrokerConnected = false;
}
/* static */ void
MqttClient::mqtt_on_publish(struct mosquitto* mosq, void* mqttClientOpaque, int /*message_id*/) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (mqttClientOpaque != instance) return;
  ++instance->mqttClientInfo.publishCallbacks;
}
/* static */ void
MqttClient::mqtt_on_message(struct mosquitto* mosq, void* mqttClientOpaque, const struct mosquitto_message* msg) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (mqttClientOpaque != instance) return;
  ++instance->mqttClientInfo.messages;
}

void MqttClient::registerMainThread() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  const std::thread::id expectedInitialValue;
  const std::thread::id caller(std::this_thread::get_id());
  
  if (mainThreadId != expectedInitialValue && mainThreadId != caller) {
    throw std::runtime_error( "double register or invalid main timer thread" );
    return;
  }

  mainThreadId = caller;
}

void MqttClient::runThreadLoop(int argc, char** argv) {
  parseParams(argc, argv);
  
  // http://mosquitto.org/api/files/mosquitto-h.html#mosquitto_threaded_set
  mosquitto_lib_init();

  struct mosquitto *mosq = mosquitto_new("officeClock_RPI", true /*clean_session*/, this);
  if (mosq == nullptr) throw std::runtime_error( "unable to allocate mosq" );

  // Note: mosquitto_threaded_set not needed anymore?
  // mosquitto_threaded_set(mosq, true /*threaded*/);
  mosquitto_connect_callback_set(mosq, mqtt_on_connect);
  mosquitto_disconnect_callback_set(mosq, mqtt_on_disconnect);
  mosquitto_publish_callback_set(mosq, mqtt_on_publish);
  mosquitto_message_callback_set(mosq, mqtt_on_message);

  TimerTick& timerTick = TimerTick::bind();

  const int mqttLoopInterval = 333;  // in milliseconds. How often we will call main mqtt_loop api
  TimerTickServiceMessage timerTickServiceMqttLoop(mqttLoopInterval, inbox);
  timerTick.registerTimerTickService(timerTickServiceMqttLoop);

  const int periodicReportInterval = (5 * 60 + 13) * 1000;  // 5 minutes, 13 seconds, in milliseconds
  TimerTickServiceBool timerTickPeriodicReport(periodicReportInterval);
  timerTick.registerTimerTickService(timerTickPeriodicReport);

  const int reconnectDamperInterval = 11 * 1000; // 11 seconds, in milliseconds
  TimerTickServiceBool timerTickReconnectDamper(reconnectDamperInterval, true /*periodic*/, true /*expired*/);
  timerTick.registerTimerTickService(timerTickReconnectDamper);

  const int motionDamperInterval = 10 * 60 * 1000; // 10 minutes, in milliseconds
  TimerTickServiceBool timerTickMotionDamper(motionDamperInterval, false /*periodic*/, true /*expired*/);
  timerTick.registerTimerTickService(timerTickMotionDamper, false /*start*/);

  InboxMsg msg;
  int mosquitto_loop_rc;
  while (true) {
    msg = inbox.waitForMessage();
    if (msg.inboxMsgType == inboxMsgTypeTerminate) break; // while

    switch (msg.inboxMsgType) {
      case inboxMsgTypeTimerTickMessage:
        // we get here because of timerTickServiceMqttLoop
        mosquitto_loop_rc = mosquitto_loop(mosq, 0 /*timeout*/, 1 /*max_packets*/);
        if (mosquitto_loop_rc == MOSQ_ERR_SUCCESS && timerTickPeriodicReport.getAndResetExpired()) {
          doPeriodicReport(mosq);
        } else if (mosquitto_loop_rc == MOSQ_ERR_NO_CONN &&
                   timerTickReconnectDamper.getAndResetExpired()) {
          mosquitto_loop_rc = mosquitto_connect(mosq, mqttClientInfo.mqttBrokerIp,
                                                mqttClientInfo.mqttBrokerPort,
                                                mqttClientInfo.mqttKeepAlive);
        }
        // update last rc, if needed
        if (mosquitto_loop_rc != mqttClientInfo.last_loop_rc) {
          std::lock_guard<std::recursive_mutex> guard(instanceMutex);
          mqttClientInfo.last_loop_rc = mosquitto_loop_rc;
        }
        break;
      case inboxMsgTypeMotionOn:
        // use damper to keep us from doing this too often
        if (timerTickMotionDamper.getAndResetExpired()) {
          doPublish(mosq, topicMotionDetected, currTimestamp().c_str());
          // 'manually' start damper timer
          timerTick.startTimerTickService(timerTickMotionDamper.getCookie());
        }
        break;
      case inboxMsgTypeDisplayBrightHigh:
        doPublish(mosq, topicDisplayIntensity, "high");
        break;
      case inboxMsgTypeDisplayBrightLow:
        doPublish(mosq, topicDisplayIntensity, "low");
        break;
      case inboxMsgTypeDisplayModeNothing:
        doPublish(mosq, topicDisplayMode, "screen_saver");
        break;
      case inboxMsgTypeDisplayModeBasicClock:
        doPublish(mosq, topicDisplayMode, "clock");
        break;
      case inboxMsgTypeDisplayModeMessage:
        doPublish(mosq, topicDisplayMode, "message");
        break;
      default:
        break;
    }
  }
  mosquitto_disconnect(mosq);
  mosquitto_destroy(mosq);
  mosquitto_lib_cleanup();

  timerTick.unregisterTimerTickService(timerTickMotionDamper.getCookie());
  timerTick.unregisterTimerTickService(timerTickReconnectDamper.getCookie());
  timerTick.unregisterTimerTickService(timerTickPeriodicReport.getCookie());
  timerTick.unregisterTimerTickService(timerTickServiceMqttLoop.getCookie());
  inbox.clear();
}

void MqttClient::parseParams(int argc, char** argv) {
  int opt;

  /* read input options -- AGAIN */
  // https://stackoverflow.com/questions/19940100/is-there-a-way-to-reset-getopt-for-non-global-use#19940311
  optind = 1;
  while ((opt = getopt(argc,argv,"p:w:v:l:M:P:K:h")) != -1) {
    switch(opt) {
        case 'M':
          mqttClientInfo.mqttBrokerIp = optarg;
          break;
        case 'P':
          mqttClientInfo.mqttBrokerPort = atoi(optarg);
          break;
        case 'K':
          mqttClientInfo.mqttKeepAlive = atoi(optarg);
          break;
        default:
          // not handled here....
          break;
    }
  }
}

void MqttClient::doPeriodicReport(struct mosquitto* mosq) {
  char lightValueBuffer[6];
  snprintf(lightValueBuffer, sizeof(lightValueBuffer), "%lu", lightSensor.getLightValue());
  doPublish(mosq, topicLightSensor, lightValueBuffer);
}

void MqttClient::doPublish(struct mosquitto* mosq, const std::string& topic, const char* payload) {
  const int rc = mqttClientInfo.mqttBrokerConnected ?
    mosquitto_publish(mosq, nullptr /*messageId*/, topic.c_str(), strlen(payload), payload,
                      0 /*qos*/, false /*retain*/) : MOSQ_ERR_NO_CONN;

  // lock and update counters
  {
    std::lock_guard<std::recursive_mutex> guard(instanceMutex);
    if (rc != MOSQ_ERR_SUCCESS) ++mqttClientInfo.publishesDropped;
    ++mqttClientInfo.publishes;
  }
}

void mqttClientMain(const ThreadParam& threadParam) {
  MqttClient::registerMainThread();
  MqttClient& mqttClient = MqttClient::bind();
  mqttClient.runThreadLoop(threadParam.argc, threadParam.argv);
}
