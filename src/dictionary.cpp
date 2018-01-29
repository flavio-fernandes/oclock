#include <cassert>
#include <string.h>

#include "threadsMain.h"
#include "timerTick.h"
#include "dictionary.h"
#include "inbox.h"
#include "commonUtils.h"

// ======================================================================

class DictionaryData
{
public:
  DictionaryData(const std::string& data, int expirationInMilliseconds);

  const std::string data;
  bool getIsExpired() const;

private:
  ~DictionaryData();

  TimerTickServiceBool timerTickServiceBool;

  DictionaryData() = delete;
  DictionaryData(const DictionaryData& other) = delete;
  DictionaryData& operator=(const DictionaryData& other) = delete;
  
  friend class Dictionary;
};

DictionaryData::DictionaryData(const std::string& data, int expirationInMilliseconds) :
  data(data), timerTickServiceBool(expirationInMilliseconds, false /*periodic*/) {
  if (expirationInMilliseconds >= 0) {
    TimerTick::bind().registerTimerTickService(timerTickServiceBool);
  }
}

bool DictionaryData::getIsExpired() const {
  if (timerTickServiceBool.interval == Dictionary::noExpiration) return false;
  return timerTickServiceBool.getIsExpired();
}

DictionaryData::~DictionaryData() {
  // idem-potent unregistration
  TimerTick::bind().unregisterTimerTickService(timerTickServiceBool.getCookie());
}

// ======================================================================

/*static*/ std::recursive_mutex Dictionary::instanceMutex;
/*static*/ Dictionary* Dictionary::instance = nullptr;
/*static*/ const int Dictionary::noExpiration = -1;
/*static*/ const std::string Dictionary::noData = std::string();
/*static*/ std::thread::id Dictionary::mainThreadId;  // default 'invalid' value

bool Dictionary::parsePostRequest(const StringMap& postValues) {
  std::string key;
  std::string data(noData);
  std::string operation(dictionaryParamOperationSet);
  std::string intervalStr( std::to_string(noExpiration) );

  getParamValue(postValues, dictionaryParamTimeout, intervalStr);
  getParamValue(postValues, dictionaryParamOperation, operation);

  if (!getParamValue(postValues, dictionaryParamKey, key)) {
    if (operation != dictionaryParamOperationDel) return false;
  }

  const bool removeAll = operation == dictionaryParamOperationDel && key.empty();

  if (!getParamValue(postValues, dictionaryParamData, data) &&
      operation != dictionaryParamOperationDel) return false;

  if (operation != dictionaryParamOperationDel &&
      operation != dictionaryParamOperationSet &&
      operation != dictionaryParamOperationAdd) return false;

  if (operation == dictionaryParamOperationDel || operation == dictionaryParamOperationSet) {
    if (removeAll) clear();
    else remove(key);  // mask out false, so remove of non-existing entry is not a failure
  }
  if (operation != dictionaryParamOperationDel) {
    return add(key, data, std::stoi(intervalStr));
  }

  return true;
}

bool Dictionary::empty() const {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  return dictionaryEntries.empty();
}

size_t Dictionary::size() const {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  return (size_t) dictionaryEntries.size();
}

bool Dictionary::add(const std::string& key, const std::string& data, int expirationInMilliseconds) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  if (key.empty()) return false;  // bad key
  
  bool found;
  auto _data = get(key, &found);
  if (found) return false;  // key already in use

  DictionaryData* dictionaryData = new DictionaryData(data, expirationInMilliseconds);
  std::pair<DictionaryEntries::iterator, bool> result =
    dictionaryEntries.insert( std::make_pair(key, dictionaryData) );
  if (!result.second) {
    throw std::runtime_error( "could not insert DictionaryEntry for: " + key );
    delete dictionaryData;
  }

  {
    std::lock_guard<std::recursive_mutex> guard(dictionaryStatusMutex);
    ++dictionaryStatus.entriesAdded;
  }
  return true;
}

std::string Dictionary::get(const std::string& key, bool* found) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  DictionaryEntries::iterator iter = dictionaryEntries.find(key);
  if (iter == dictionaryEntries.end()) {
    if (found != nullptr) *found = false;
    return noData;
  }

  // if data is expired, pretend none was found
  DictionaryData* dictionaryData = iter->second;
  if (dictionaryData == nullptr) {
    throw std::runtime_error( "bug: Dictionary::get for: " + key );
    if (found != nullptr) *found = false;
    return noData;
  }

  if (dictionaryData->getIsExpired()) {
    {
      std::lock_guard<std::recursive_mutex> guard(dictionaryStatusMutex);
      ++dictionaryStatus.entriesExpired;
    }

    _remove(iter);
    if (found != nullptr) *found = false;
    return noData;
  }

  if (found != nullptr) *found = true;
  return dictionaryData->data;
}

bool Dictionary::remove(const std::string& key) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  DictionaryEntries::iterator iter = dictionaryEntries.find(key);
  if (iter == dictionaryEntries.end()) return false;
  _remove(iter);
  return true;
}

void Dictionary::clear() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  const auto entriesRemoved = dictionaryEntries.size();
  for (auto& kvp : dictionaryEntries) {
    DictionaryData* dictionaryData = kvp.second;
    delete dictionaryData;
  }
  dictionaryEntries.clear();

  {
    std::lock_guard<std::recursive_mutex> guard(dictionaryStatusMutex);
    dictionaryStatus.entriesRemoved += (Int32U) entriesRemoved;
  }
}

void Dictionary::_remove(DictionaryEntries::iterator& iter) {
  DictionaryData* const dictionaryData = iter->second;
  dictionaryEntries.erase(iter);
  delete dictionaryData;

  {
    std::lock_guard<std::recursive_mutex> guard(dictionaryStatusMutex);
    ++dictionaryStatus.entriesRemoved;
  }
}

std::string Dictionary::getFirst(std::string& key, bool* found) {
  key = "";
  return getNext(key, found);
}

std::string Dictionary::getNext(std::string& key, bool* found) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  DictionaryEntries::iterator iter = dictionaryEntries.upper_bound(key);
  if (iter == dictionaryEntries.end()) {
    if (found != nullptr) *found = false;
    return noData;
  }

  // do a get to ensure entry found is not expired...
  key = iter->first;
  std::string result = get(key, found);
  return result == noData ? getNext(key, found) : result;
}

void Dictionary::getDictionaryStatus(DictionaryStatus& status) {
  std::lock_guard<std::recursive_mutex> guard(dictionaryStatusMutex);
  status = dictionaryStatus;
}

Dictionary& Dictionary::bind() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == nullptr) {
    instance = new Dictionary();
  }
  return *instance;
}

void Dictionary::shutdown() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == nullptr) return; // noop
  instance->clear();
  delete instance;
  instance = nullptr;
}

Dictionary::Dictionary() : dictionaryEntries(), /*dictionaryStatus(),*/ dictionaryStatusMutex() {
  if (instance != nullptr) {
    throw std::runtime_error("There can only be one instance of Dictionary!");
  }
  memset(&dictionaryStatus, 0, sizeof(dictionaryStatus));
}

Dictionary::~Dictionary() {
  // empty
  assert(dictionaryEntries.empty());
}

void Dictionary::registerMainThread() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  const std::thread::id expectedInitialValue;
  const std::thread::id caller(std::this_thread::get_id());
  
  if (mainThreadId != expectedInitialValue && mainThreadId != caller) {
    throw std::runtime_error( "double register or invalid main timer thread" );
    return;
  }

  mainThreadId = caller;
}

void Dictionary::purgeExpiredDictionaryEntries() {
  // simply visit all dictionary entries. By doing so, get member function will
  // remove the entries that have been expired
  bool found;
  std::string currKey;

  getFirst(currKey, &found);
  while (found) getNext(currKey, &found);
}

void Dictionary::runThreadLoop() {
  InboxRegistry& inboxRegistry = InboxRegistry::bind();
  Inbox& inbox = inboxRegistry.getInbox(threadIdDictionary);
  InboxMsg msg;

  TimerTickServiceMessage timerTickServiceDictionaryLoop(12345, inbox); // auto tick every 12.345 seconds
  TimerTick& timerTick = TimerTick::bind();
  timerTick.registerTimerTickService(timerTickServiceDictionaryLoop);

  while (true) {
    msg = inbox.waitForMessage();
    if (msg.inboxMsgType == inboxMsgTypeTerminate) break;
    else if (msg.inboxMsgType == inboxMsgTypeTimerTickMessage) {
      purgeExpiredDictionaryEntries();

      {
	std::lock_guard<std::recursive_mutex> guard(dictionaryStatusMutex);
	++dictionaryStatus.ticks;
      }
    }
  }

  timerTick.unregisterTimerTickService(timerTickServiceDictionaryLoop.getCookie());
  inbox.clear();
}

void dictionaryMain(const ThreadParam& threadParam) {
  Dictionary::registerMainThread();
  Dictionary& dictionary = Dictionary::bind();
  dictionary.runThreadLoop();
}
