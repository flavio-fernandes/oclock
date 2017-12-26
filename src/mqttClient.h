#ifndef __THREAD_MQTT_CLIENT_H
#define __THREAD_MQTT_CLIENT_H

#include <mutex>
#include <thread>
#include <string>

#include "stdTypes.h"

class Inbox; // FWD
class LightSensor; // FWD
struct mosquitto; // FWD
struct mosquitto_message; // FWD

typedef struct mqttClientStatus_t {
  // admin (set via argv)
  const char* mqttBrokerIp;
  int mqttBrokerPort;
  int mqttKeepAlive;
  // operational
  bool mqttBrokerConnected;
  int last_loop_rc;
  Int32U connects;
  Int32U disconnects;
  Int32U publishes;
  Int32U publishesDropped;
  Int32U publishCallbacks;
  Int32U messages;
} MqttClientInfo;

class MqttClient
{
public:
  static MqttClient& bind();
  static void shutdown();

  bool /*brokerConnected*/ getMqttClientInfo(MqttClientInfo* out = nullptr) const;
  static const char* getStrError(int mosq_errno);

  // callbacks from mqtt thread
  static void mqtt_on_connect(struct mosquitto* mosq, void* mqttClientOpaque, int rc);
  static void mqtt_on_disconnect(struct mosquitto* mosq, void* mqttClientOpaque, int rc);
  static void mqtt_on_publish(struct mosquitto* mosq, void* mqttClientOpaque, int message_id);
  static void mqtt_on_message(struct mosquitto* mosq, void* mqttClientOpaque, const struct mosquitto_message* msg);

  static void registerMainThread();  // only needed by one thread
  void runThreadLoop(int argc, char** argv);  // to be ran by main thread only
private:
  static std::thread::id mainThreadId; // http://en.cppreference.com/w/cpp/thread/thread/id

  static std::recursive_mutex instanceMutex;
  static MqttClient* instance;

  MqttClient();
  ~MqttClient();

  MqttClientInfo mqttClientInfo;
  void parseParams(int argc, char** argv);
  void doPeriodicReport(struct mosquitto* mosq);
  void doPublish(struct mosquitto* mosq, const std::string& topic, const char* payload);

  Inbox& inbox;
  LightSensor& lightSensor;
  static const std::string topicPrefix;
  static const std::string topicLightSensor;
  static const std::string topicDisplayIntensity;
  static const std::string topicDisplayMode;
  static const std::string topicMotionDetected;

  // not implemented
  MqttClient(const MqttClient& other) = delete;
  MqttClient& operator=(const MqttClient& other) = delete; 
};

#endif // __THREAD_MQTT_CLIENT_H
