#ifndef __WEB_HANDLER_INTERNAL_H
#define __WEB_HANDLER_INTERNAL_H

#include <mutex>
#include <string>
#include <map>

#include "webHandler.h"
#include "commonTypes.h"
#include "stdTypes.h"

typedef struct RequestOutput_t {
  struct evkeyvalq* replyHeaders;
  struct evbuffer* replyBody;
} RequestOutput;

typedef struct RequestInfo_t {
  struct evhttp_request* req;
  const worker* workerPtr;

  // [subset of] parsed stuff from req
  enum evhttp_cmd_type method;
  const char* uriHost;
  const char* uriPath;
  const char* uriQuery;
  const char* uriScheme;
  struct evkeyvalq* requestHeaders;
  struct evbuffer* requestBody;
  char* remoteAddress;
  Int16U remotePort;
} RequestInfo;


class WebHandlerKey {
public:
  WebHandlerKey(const std::string& uriPath); // not explicit
  WebHandlerKey(enum evhttp_cmd_type method, const std::string& uriPath); // not explicit
  WebHandlerKey();
  WebHandlerKey(const WebHandlerKey& other);
  ~WebHandlerKey();
  WebHandlerKey& operator=(const WebHandlerKey& other);
  bool operator<(const WebHandlerKey& other) const;
  std::string operator()() const;
  
  inline enum evhttp_cmd_type getMethod() const { return method; }
  inline const std::string& getUriPath() const { return uriPath; }

  static const WebHandlerKey uriNotFound;
  static const WebHandlerKey uriHead;
private:
  enum evhttp_cmd_type method;
  std::string uriPath;
};

class WebHandler {
public:
  WebHandler();
  virtual ~WebHandler();
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) = 0;

  inline Int32U getHits() const { return hits; }
  
  static const HandleRequestReply replyOk;
  static const HandleRequestReply replyNoContent;
  static const HandleRequestReply replyInternalError;
  static const HandleRequestReply replyNotFound;
  static const HandleRequestReply replyServerUnavail;

protected:
  static void setHeader(RequestOutput& requestOutput, const std::string& header, const std::string& value);
  static void setHeaderContentType(RequestOutput& requestOutput, const std::string& contentType);
  static void setHeaderContentTypeText(RequestOutput& requestOutput);

  HandleRequestReply parsePost(const RequestInfo& requestInfo, RequestOutput& requestOutput, StringMap& postValues);

  // output HTML for a radio button
  static void addRadioButton(std::string& buff, const char* name, int valAutoInt,
			     const char* label, bool selected);
  static void addRadioButton(std::string& buff, const char* name, const char* val,
			  const char* label, bool selected);

  // output HTML for a checkbox
  static void addCheckBox(std::string& buff, const char* name, const char* val,
		       const char* label, bool selected);

  // output common piece of HTML helper functions
  static void addFormFont(std::string& buff);
  static void addFormColor(std::string& buff, bool includeAlternate);
  static void addXYCoordinatesInput(std::string& buff);
  static void addAnimationStepInput(std::string& buff);
  
  static void addBody(RequestOutput& requestOutput, const std::string& buffer);

  static const std::string contentStart;
  static const std::string contentStop;

private:
  static void addCheckboxOrRadio(std::string& buff, const char* element,
				 const char* name, const char* val,
				 const char* label, bool selected);

  inline Int32U bumpHits() { return ++hits; }
  Int32U hits;

  WebHandler(const WebHandler& other) = delete;
  WebHandler& operator=(const WebHandler& other) = delete;

  friend class WebHandlerInternal;
};

class WebHandlerInternal
{
public:
  static WebHandlerInternal& bind();
  static WebHandlerInternal* bindIfExists();
  void start();
  static void shutdown();

  HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput);
  std::string getHandlerStats();
  
private:
  WebHandler* findWebHandler(const WebHandlerKey& webHandlerKey);
  void _start();
  
  WebHandlerInternal();
  ~WebHandlerInternal();

  static std::recursive_mutex instanceMutex;
  static WebHandlerInternal* instance;

  typedef std::map<WebHandlerKey, WebHandler*> WebHandlers;
  WebHandlers webHandlers;
  
  // not implemented
  WebHandlerInternal(const WebHandlerInternal& other) = delete;
  WebHandlerInternal& operator=(const WebHandlerInternal& other) = delete;
};

#endif // ifndef __WEB_HANDLER_INTERNAL_H
