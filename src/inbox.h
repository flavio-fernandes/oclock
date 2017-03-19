#ifndef __THREAD_INBOX_H
#define __THREAD_INBOX_H

#include <condition_variable> // std::condition_variable
#include <list>
#include <mutex>
#include <unordered_map>

#include "stdTypes.h"
#include "threadsMain.h"

typedef enum InboxMsgType_t {
  inboxMsgTypeNoop,
  inboxMsgTypeTerminate,
  inboxMsgTypeMotionOn,
  inboxMsgTypeMotionOff,
  inboxMsgTypeTimerTickMessage,
  inboxMsgTypeCount
} InboxMsgType;

class InboxMsg
{
public:
  InboxMsg();
  InboxMsg(const InboxMsg& other);
  InboxMsg& operator=(const InboxMsg& other);
  ~InboxMsg();

  InboxMsg(InboxMsgType inboxMsgType);
  void initPayload();
  
  InboxMsgType inboxMsgType;
  static const int maxPayloadSize = 64;
  Int8U payload[maxPayloadSize];
};

class Inbox
{
public:
  Inbox();
  ~Inbox();

  void addMessage(const InboxMsg& msg);
  InboxMsg waitForMessage();
  bool getMessage(InboxMsg& msg);
  bool empty() const;
  void clear();
  inline Int32U getMsgCount() const { return msgCount; }
  
  void debugAddNoMessage();
private:
  typedef std::list<InboxMsg> Msgs;
  Msgs msgs;
  Int32U msgCount;

  std::mutex mtx;
  std::condition_variable cv;
  
  Inbox(const Inbox& other) = delete;
  Inbox& operator=(const Inbox& other) = delete;
};

class InboxRegistry
{
public:
  static InboxRegistry& bind();
  static void shutdown();
  
  Inbox& getInbox(ThreadId threadId);
  void broadcast(const InboxMsg& msg, ThreadId threadIdFrom = threadIdIgnore);

private:
  InboxRegistry();
  ~InboxRegistry();

  static std::recursive_mutex instanceMutex;
  static InboxRegistry* instance;

  typedef std::unordered_map<int /*ThreadId*/, Inbox*> Inboxes;
  Inboxes inboxes;
};

#endif // __THREAD_INBOX_H
