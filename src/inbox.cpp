#include <string.h>

#include "inbox.h"

InboxMsg::InboxMsg() : inboxMsgType(inboxMsgTypeNoop) {
  initPayload();
}

InboxMsg::InboxMsg(const InboxMsg& other) : inboxMsgType(other.inboxMsgType) {
  memcpy(payload, other.payload, sizeof(payload));
}

InboxMsg& InboxMsg::operator=(const InboxMsg& other) {
  inboxMsgType = other.inboxMsgType;
  memcpy(payload, other.payload, sizeof(payload));
  return *this;
}

InboxMsg::~InboxMsg() {
  // empty
}

InboxMsg::InboxMsg(InboxMsgType inboxMsgType) : inboxMsgType(inboxMsgType) {
  initPayload();
}

void InboxMsg::initPayload() {
  const size_t payloadSize = sizeof(payload);
  memset(&payload, 0, payloadSize);
}

// ----------------------------------------------------------------------

/*static*/ const InboxMsg Inbox::timerTickMessage(inboxMsgTypeTimerTickMessage);

Inbox::Inbox() :
  msgs(), msgCount(0), mtx(), cv() {
}

Inbox::~Inbox() {
  // empty
}

void Inbox::debugAddNoMessage() {
  std::unique_lock<std::mutex> lck(mtx);
  cv.notify_all();
}

void Inbox::addMessage(const InboxMsg& msg) {
  std::unique_lock<std::mutex> lck(mtx);
  msgs.push_back(msg);
  ++msgCount;
  cv.notify_all();
}

InboxMsg Inbox::waitForMessage() {
  InboxMsg msg;
  while (true) {
    std::unique_lock<std::mutex> lck(mtx);
    cv.wait(lck);
    if (msgs.empty()) continue;
    msg = msgs.front();
    msgs.pop_front();
    --msgCount;
    break;
  }
  return msg;
}

bool Inbox::getMessage(InboxMsg& msg) {
  std::unique_lock<std::mutex> lck(mtx);
  if (msgs.empty()) return false;
  msg = msgs.front();
  msgs.pop_front();
  --msgCount;
  return true;
}

bool Inbox::empty() const {
  // std::unique_lock<std::mutex> lck(mtx);
  return msgs.empty();
}

void Inbox::clear() {
  std::unique_lock<std::mutex> lck(mtx);
  msgs.clear();
  msgCount = 0;
}

/*static*/ std::recursive_mutex InboxRegistry::instanceMutex;
/*static*/ InboxRegistry* InboxRegistry::instance;

InboxRegistry& InboxRegistry::bind() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == nullptr) instance = new InboxRegistry();
  return *instance;
}

void InboxRegistry::shutdown() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == nullptr) return;

  for (auto& kvp : instance->inboxes) {
    Inbox& inbox = *(kvp.second);
    inbox.clear();
    delete &inbox;
  }
  instance->inboxes.clear();
  
  delete instance;
  instance = nullptr;
}

Inbox& InboxRegistry::getInbox(ThreadId threadId) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  Inboxes::iterator iter = inboxes.find(threadId);
  if (iter == inboxes.end()) {
    Inbox* inboxP = new Inbox();
    std::pair<Inboxes::iterator, bool> result =
      inboxes.insert( std::make_pair((int)threadId, inboxP) );
    if (!result.second) {
      throw std::runtime_error("unable to create inbox for threadId: " + std::to_string(threadId));
    }
    iter = result.first;
  }
  return *(iter->second);
}

void InboxRegistry::broadcast(const InboxMsg& msg, ThreadId threadIdFrom) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  for (auto& kvp : inboxes) {
    auto inbox = kvp.second;
    if (kvp.first != threadIdFrom) inbox->addMessage(msg);
  }
}

InboxRegistry::InboxRegistry() : inboxes() {
}

InboxRegistry::~InboxRegistry() {
  // empty
}
