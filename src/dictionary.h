#ifndef __DICTIONARY_H
#define __DICTIONARY_H

#include <mutex>
#include <thread>
#include <string>
#include <map>

#include "commonTypes.h"

const char* const dictionaryParamOperation = "dictionaryOperation";
const char* const dictionaryParamOperationSet = "set";
const char* const dictionaryParamOperationAdd = "add";
const char* const dictionaryParamOperationDel = "del";
const char* const dictionaryParamKey = "dictionaryKey";
const char* const dictionaryParamData = "dictionaryData";
const char* const dictionaryParamTimeout = "dictionaryTimeout";

const char* const dictionaryTopicTemperature = "/sensor/temperature_outside";
const char* const dictionaryTopicTemperatureDir = "/sensor/temperature_outside/direction";
const char* const dictionaryTopicHumidity = "/garage/humidity";
const char* const dictionaryTopicHumidityDir = "/garage/humidity/direction";
const char* const dictionaryTopicGarageMotion = "/garage/oper_flag/motion";

const char* const dictionaryTopicValueOn = "on";
const char* const dictionaryTopicValueOff = "off";
const char* const dictionaryTopicDirectionUp = "up";
const char* const dictionaryTopicDirectionDown = "down";
const char* const dictionaryTopicDirectionSame = "same";

class DictionaryData;

class Dictionary
{
public:
  static Dictionary& bind();
  static void shutdown();

  static const int noExpiration;
  static const std::string noData;

  bool parsePostRequest(const StringMap& postValues);
  
  bool empty() const;
  size_t size() const;
  bool add(const std::string& key, const std::string& data, int expirationInMilliseconds = noExpiration);
  std::string get(const std::string& key, bool* found = 0);
  bool remove(const std::string& key);
  void clear();

  std::string getFirst(std::string& key, bool* found = 0);
  std::string getNext(std::string& key, bool* found = 0);
  
  static void registerMainThread();  // only needed by one thread
  void runThreadLoop();  // to be ran by mainThread only

private:
  static std::thread::id mainThreadId; // http://en.cppreference.com/w/cpp/thread/thread/id

  static std::recursive_mutex instanceMutex;
  static Dictionary* instance;

  Dictionary();
  ~Dictionary();

  typedef std::map<std::string, DictionaryData*> DictionaryEntries;
  DictionaryEntries dictionaryEntries;

  void purgeExpiredDictionaryEntries();
  void _remove(DictionaryEntries::iterator& iter);
  
  // not implemented
  Dictionary(const Dictionary& other) = delete;
  Dictionary& operator=(const Dictionary& other) = delete;
};

#endif  // __DICTIONARY_H
