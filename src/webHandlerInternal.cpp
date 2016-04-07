#include "webHandlerInternal.h"

#include <stdlib.h>
#include <cassert>
#include <set>

#include <event2/event.h>
#include "server.h"

#include "HT1632.h"
#include "commonTypes.h"
#include "displayTypes.h"
#include "display.h"
#include "ledStrip.h"
#include "ledStripTypes.h"
#include "motionSensor.h"
#include "lightSensor.h"


// Helper web page macro nad string operator
#define ADD_BODY(__STRPRM)  addBody(requestOutput, __STRPRM)
#define RETURN_ERROR(__STRPRM) do { buff = "Error in "; INT2BUFF(__LINE__); buff << ": " << __STRPRM; addBody(requestOutput, buff); \
    setHeaderContentTypeText(requestOutput); return replyInternalError; } while (0)
#define CHAR2INTMAXSIZE 16
#define INT2STR(__STRPRM, __INT) do { char __b[CHAR2INTMAXSIZE]; snprintf(__b, CHAR2INTMAXSIZE, "%d", (int) __INT); __STRPRM << __b; } while (0)
#define INT2BUFF(__INT) INT2STR(buff, __INT)
template<class T> inline std::string& operator<<(std::string& obj, T arg) { obj += arg; return obj; } 
#define FORM_MSG_BOX_LEN 64
#define MAX_POST_BODY_LEN 4096


// FWD
static void parseRequest(RequestInfo& requestInfo);


HandleRequestReply handleRequest(struct evhttp_request* req, worker* workerPtr,
			   struct evkeyvalq* replyHeaders, struct evbuffer* replyBody) {
  RequestInfo requestInfo = {req, workerPtr};  // partial init (http://stackoverflow.com/questions/10828294/c-and-c-partial-initialization-of-automatic-structure)
  RequestOutput requestOutput = {replyHeaders, replyBody};
  
  parseRequest(requestInfo);

  WebHandlerInternal* const webHandlerInternal = WebHandlerInternal::bindIfExists();
  if (webHandlerInternal == 0) {
    return WebHandler::replyServerUnavail;
  }
  return webHandlerInternal->process(requestInfo, requestOutput);
}

static void parseRequest(RequestInfo& requestInfo) {
  const struct evhttp_uri* uri = evhttp_request_get_evhttp_uri(requestInfo.req);
  /*const*/ struct evhttp_connection* conn = evhttp_request_get_connection(requestInfo.req);
  
  requestInfo.method = evhttp_request_get_command(requestInfo.req);
  requestInfo.uriHost = evhttp_uri_get_host(uri);
  requestInfo.uriPath = evhttp_uri_get_path(uri);
  requestInfo.uriQuery = evhttp_uri_get_query(uri);
  requestInfo.uriScheme = evhttp_uri_get_scheme(uri);
  requestInfo.requestHeaders = evhttp_request_get_input_headers(requestInfo.req);
  requestInfo.requestBody = evhttp_request_get_input_buffer(requestInfo.req);
  evhttp_connection_get_peer(conn, &requestInfo.remoteAddress, (ev_uint16_t*) &requestInfo.remotePort);
}

// ======================================================================

/*static*/ const WebHandlerKey WebHandlerKey::uriNotFound("__notFound");
/*static*/ const WebHandlerKey WebHandlerKey::uriHead(EVHTTP_REQ_HEAD, "/");

WebHandlerKey::WebHandlerKey(const std::string& uriPath) :
  method(EVHTTP_REQ_GET), uriPath(uriPath) {
}

WebHandlerKey::WebHandlerKey(enum evhttp_cmd_type method, const std::string& uriPath) :
  method(method), uriPath(uriPath) {
}

WebHandlerKey::WebHandlerKey(const WebHandlerKey& other) :
  method(other.method), uriPath(other.uriPath) {
}

WebHandlerKey::WebHandlerKey() : method( (evhttp_cmd_type) HTTP_INTERNAL ), uriPath() {
}

WebHandlerKey::~WebHandlerKey() {
}

WebHandlerKey& WebHandlerKey::operator=(const WebHandlerKey& other) {
  if (this != &other) {
    method = other.method;
    uriPath = other.uriPath;
  }
  return *this;
}

bool WebHandlerKey::operator<(const WebHandlerKey& other) const {
  if (this == &other) return false;
  if (method != other.method) return method < other.method;
  return uriPath < other.uriPath;
}

std::string WebHandlerKey::operator()() const {
  std::string methosStr;

  // Exception: uri (not found applies to all methods)
  if (uriPath == uriNotFound.getUriPath()) return uriPath;
  
  switch (method) {
  case EVHTTP_REQ_GET:
    methosStr = "get"; break;
  case EVHTTP_REQ_POST:
    methosStr = "post"; break;
  case EVHTTP_REQ_PUT:
    methosStr = "put"; break;
  case EVHTTP_REQ_HEAD:
    methosStr = "head"; break;
  default:
    // check /usr/include/event2/http.h add add what you like here
    methosStr = std::to_string(method); break;
  }
  return uriPath + " (" + methosStr + ")";
}

// ======================================================================

WebHandler::WebHandler() : hits(0) {
  // empty
}

WebHandler::~WebHandler() {
  // empty
}

void WebHandler::addRadioButton(std::string& buff, const char* name, int valAutoInt,
				const char* label, bool selected) {
  char value[CHAR2INTMAXSIZE];
  snprintf(value, CHAR2INTMAXSIZE, "%d", valAutoInt);
  addRadioButton(buff, name, value, label, selected);
}
void WebHandler::addRadioButton(std::string& buff, const char* name, const char* val,
				const char* label, bool selected) {
  addCheckboxOrRadio(buff, "radio", name, val, label, selected);
}

void WebHandler::addCheckBox(std::string& buff, const char* name, const char* val,
			     const char* label, bool selected) {
  addCheckboxOrRadio(buff, "checkbox", name, val, label, selected);
}

void WebHandler::addCheckboxOrRadio(std::string& buff, const char* element,
				    const char* name, const char* val,
				    const char* label, bool selected) {
  buff << "<label><input type='" << element << "' name='" << name
       <<  "' value='" << val << "' ";
  if (selected) buff << "checked ";
  buff << "/> " << label << "</label>";
}

void WebHandler::addFormFont(std::string& buff) {
  buff << "<br/>Font: ";
  addRadioButton(buff, "font", font5x4, "5x4", true);
  addRadioButton(buff, "font", font8x4, "8x4", false);
  addRadioButton(buff, "font", font7x5, "7x5", false);
  addRadioButton(buff, "font", font8x6, "8x6", false);
  addRadioButton(buff, "font", font16x8, "16x8", false);
}

void WebHandler::addFormColor(std::string& buff, bool includeAlternate) {
    buff << "<br/>Color: ";
    addRadioButton(buff, "color", displayColorGreen, "green", !includeAlternate);
    addRadioButton(buff, "color", displayColorRed, "red", false);
    addRadioButton(buff, "color", displayColorYellow, "yellow", false);
    if (includeAlternate)  addRadioButton(buff, "color", displayColorAlternate, "alternate", true);
}

void WebHandler::addXYCoordinatesInput(std::string& buff) {
    buff << "<br/>X (0-";
    INT2BUFF(OUT_SIZE - 1);
    buff << "): <input type='range' size='3' min='0' max='";
    INT2BUFF(OUT_SIZE - 1);
    buff << "' value='0' name='x'>";
    buff << "  Y (0-";
    INT2BUFF(COM_SIZE - 1);
    buff << "): <input type='range' size='2' min='0' max='";
    INT2BUFF(COM_SIZE - 1);
    buff << "' value=0 name='y'>";
}

void WebHandler::addAnimationStepInput(std::string& buff) {
    buff << "<br/>Animation: ";
    addRadioButton(buff, "animationStep", animationStepNone, "none", true);  
    addRadioButton(buff, "animationStep", animationStepFast, "fast", false);
    addRadioButton(buff, "animationStep", animationStep250ms, "250ms", false);
    addRadioButton(buff, "animationStep", animationStep500ms, "500ms", false);
    addRadioButton(buff, "animationStep", animationStep1sec, "1sec", false);
    addRadioButton(buff, "animationStep", animationStep5sec, "5sec", false);
    
    buff << "<br/>phase (0-255): <input type='range' size=3 min=0 max=255 value=2 name='animationPhase'>"
	 << " value (0-254): <input type='range' size=3 min=0 max=254 value=0 name='animationPhaseValue'>";
}

// ======================================================================

/*static*/ std::recursive_mutex WebHandlerInternal::instanceMutex;
/*static*/ WebHandlerInternal* WebHandlerInternal::instance = 0;

WebHandlerInternal& WebHandlerInternal::bind() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == 0) {
    instance = new WebHandlerInternal();
  }
  return *instance;
}

WebHandlerInternal* WebHandlerInternal::bindIfExists() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);  
  return instance;
}

void WebHandlerInternal::start() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);  
  _start();
}

void WebHandlerInternal::shutdown() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);  
  if (instance == 0) return; // noop

  // There can be multiple keys for the same webHandler. So
  // we first assemble a set to eliminate the duplicates!
  std::set<WebHandler*> webHandlerPtrs;
  for (auto& kvp : instance->webHandlers) {
    WebHandler* webHandlerPtr = kvp.second;
    webHandlerPtrs.insert(webHandlerPtr);
  }
  for (WebHandler* webHandlerPtr : webHandlerPtrs) {
    delete webHandlerPtr;
  }
  instance->webHandlers.clear();
  
  delete instance;
  instance = 0;
}

WebHandler* WebHandlerInternal::findWebHandler(const WebHandlerKey& webHandlerKey) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);  
  WebHandlers::iterator iter = webHandlers.find(webHandlerKey);
  if (iter == webHandlers.end()) return 0;
  WebHandler* const webHandlerPtr = iter->second;
  webHandlerPtr->bumpHits();
  return webHandlerPtr;
}

HandleRequestReply WebHandlerInternal::process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
  // Note: do not hold any locks, so multiple workers can work simultaneously
  //
  WebHandler* webHandlerPtr = findWebHandler(WebHandlerKey(requestInfo.method, requestInfo.uriPath));
  // URI Exceptions
  if (webHandlerPtr == 0 && requestInfo.method == EVHTTP_REQ_HEAD) webHandlerPtr = findWebHandler(WebHandlerKey::uriHead);
  if (webHandlerPtr == 0) webHandlerPtr = findWebHandler(WebHandlerKey::uriNotFound);
  return webHandlerPtr == 0 ? WebHandler::replyNotFound : webHandlerPtr->process(requestInfo, requestOutput);
}

std::string WebHandlerInternal::getHandlerStats() {
  std::string buff;

  std::lock_guard<std::recursive_mutex> guard(instanceMutex);  
  for (const auto& kvp : webHandlers) {
    const WebHandlerKey& webHandlerKey = kvp.first;  // using operator() trick to get string
    const WebHandler& webHandler = *(kvp.second);
    buff << "hitCount: " << webHandlerKey() << " = "
	 << std::to_string(webHandler.getHits()) << "\n";
  }
  return buff;
}

WebHandlerInternal::WebHandlerInternal() : webHandlers() {
  if (instance != 0) {
    throw std::runtime_error("There can only be one instance of WebHandlerInternal!");
  }
}

WebHandlerInternal::~WebHandlerInternal() {
  // empty
}

// ======================================================================

/*static*/ const std::string WebHandler::contentStart =
    "<html>"
    "<head>"
    "<title>Embedded Office Clock Display Controller</title>"
    "<style type=\"text/css\">"
    "BODY { font-family: sans-serif }"
    "H1 { font-size: 14pt; text-decoration: underline }"
    "P { font-size: 10pt; }"
    "</style>"
    "</head>"
    "<body>"
; // WebHandler::contentStart
/*static*/ const std::string WebHandler::contentStop = "</body></html>";

/*static*/ const HandleRequestReply WebHandler::replyOk = {HTTP_OK, "OK"};
/*static*/ const HandleRequestReply WebHandler::replyNoContent = {HTTP_NOCONTENT, "No Content"};
/*static*/ const HandleRequestReply WebHandler::replyInternalError = {HTTP_INTERNAL, "Internal Error"};
/*static*/ const HandleRequestReply WebHandler::replyNotFound = {HTTP_NOTFOUND, "Page not found"};
/*static*/ const HandleRequestReply WebHandler::replyServerUnavail = {HTTP_SERVUNAVAIL, "Server is not available"};

/*static*/ void WebHandler::setHeader(RequestOutput& requestOutput, const std::string& header, const std::string& value) {
  if (evhttp_find_header(requestOutput.replyHeaders, header.c_str())) {
    evhttp_remove_header(requestOutput.replyHeaders, header.c_str());
  }
  evhttp_add_header(requestOutput.replyHeaders, header.c_str(), value.c_str());
}

/*static*/ void WebHandler::setHeaderContentType(RequestOutput& requestOutput, const std::string& contentType) {
  setHeader(requestOutput, "Content-Type", contentType);
}

/*static*/ void WebHandler::setHeaderContentTypeText(RequestOutput& requestOutput) {
  setHeaderContentType(requestOutput, "text/plain");
}

/*static*/ void WebHandler::addBody(RequestOutput& requestOutput, const std::string& buffer) {
  if (!buffer.empty()) {
    int rc = evbuffer_add_printf(requestOutput.replyBody, "%s", buffer.c_str());
    assert(rc != -1);
  }
}

class WebHandlerNotFound : public WebHandler {
public:
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    std::string buff =
      "<html><head>"
      "<title>404 Not Found</title>"
      "</head><body>"
      "<h1>Not Found</h1>"
      "<p>The requested URL ";
    buff << requestInfo.uriPath;
    buff << " was not found on this server.</p>"
      "</body></html>";
    ADD_BODY(buff);
    return replyNotFound;
  }
};

class WebHandlerStatus : public WebHandler {
public:
  WebHandlerStatus() : motionSensor(MotionSensor::bind()), lightSensor(LightSensor::bind()),
		       display(Display::bind()), ledStrip(LedStrip::bind()) {}
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    std::string buff("Stats and status\n\n");

    WebHandlerInternal* const webHandlerInternal = WebHandlerInternal::bindIfExists();
    if (webHandlerInternal) {
      buff << webHandlerInternal->getHandlerStats() << "\n";
    }

    MotionInfo motionInfo;
    motionSensor.getMotionValue(&motionInfo);
    buff << "motion: " << (motionInfo.currMotionDetected ? "1" : "0") << "\n";
    buff << "motion_last_change: ";
    INT2BUFF(motionInfo.lastChangedHour); buff << ":";
    INT2BUFF(motionInfo.lastChangedMin); buff << ":";
    INT2BUFF(motionInfo.lastChangedSec); buff << "\n";
    
    buff << "light_sensor: "; INT2BUFF(lightSensor.getLightValue()); buff << "\n";
    buff << "display_mode: " << display.getInternalDisplayMode() << "\n";
    buff << "led_strip_mode: " << ledStrip.getInternalLedStripMode() << "\n";
    
    setHeaderContentTypeText(requestOutput);
    ADD_BODY(buff);
    return replyOk;
  }

private:
  MotionSensor& motionSensor;
  LightSensor& lightSensor;
  Display& display;
  LedStrip& ledStrip;
};

class WebHandlerImgBackground : public WebHandler {
public:
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    std::string buff(contentStart);

    buff << "<form action='/imgBackground' method='post'>"
	 << "<h1>Background Img</h1><p>";

    buff << "<br/>Index (0-";
    INT2BUFF(BACKGROUND_IMG_COUNT - 1);
    buff << "): <input type='range' min='0' max='";
    INT2BUFF(BACKGROUND_IMG_COUNT - 1);
    buff << "' value=0 name='index'>";

#ifdef BG_IMG_ART_SELECT_BY_NUMBER
    buff << "<br/>ImgArt Number (0-"; INT2BUFF(imgArtLast - 1);
    buff << "): <input type='range' min='0' max='"; INT2BUFF(imgArtLast - 1);
    buff << "' value=0 name='imgArt'>";
#else   // ifdef BG_IMG_ART_SELECT_BY_NUMBER
    buff << "<br/>ImgArt Name: ";
    addRadioButton(buff, "imgArt", -1, "default", true);
    for (int i=0; i < imgArtLast; ++i) {
      addRadioButton(buff, "imgArt", i, imgArtStr[i] + imgArtStrUniqueOffset, false);
    }
#endif  // ifdef BG_IMG_ART_SELECT_BY_NUMBER
    
    buff << "<br/>Enabled: ";
    addCheckBox(buff, "enabled", "1", "", true);
    buff << "  Clear all: ";
    addCheckBox(buff, "clearAll", "1", "", false);

    addFormColor(buff, false /*includeAlternate*/);
    addXYCoordinatesInput(buff);
    addAnimationStepInput(buff);
    
    buff << "</p><input type='submit' value='Submit'/></imgBackground>";

    ADD_BODY(buff + contentStop);
    return replyOk;
  }
};

class WebHandlerImgBackgroundPost : public WebHandler {
public:
  WebHandlerImgBackgroundPost() : WebHandler(), display(Display::bind()) { }
  virtual ~WebHandlerImgBackgroundPost() {}
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    StringMap postValues;
    const HandleRequestReply handleRequestReply = parsePost(requestInfo, requestOutput, postValues);
    if (handleRequestReply.code != HTTP_OK) return handleRequestReply;
    display.enqueueImgBackgroundPost(postValues);
    return replyNoContent;
  }
private:
  Display& display;
};

class WebHandlerMsgBackground : public WebHandler {
public:
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    std::string buff(contentStart);

    buff << "<form action='/msgBackground' method='post'>"
	 << "<h1>Background Message</h1><p>";

    buff << "<br/>Index (0-";
    INT2BUFF(BACKGROUND_MESSAGE_COUNT - 1);
    buff << "): <input type='range' min='0' max='";
    INT2BUFF(BACKGROUND_MESSAGE_COUNT - 1);
    buff << "' value=0 name='index'>";

    buff << "<br/>Msg: <input type='text' size='16' name='msg'>";

    buff << "<br/>Enabled: ";
    addCheckBox(buff, "enabled", "1", "", true);
    buff << "  Clear all: ";
    addCheckBox(buff, "clearAll", "1", "", false);

    addFormFont(buff);
    addFormColor(buff, false /*includeAlternate*/);
    addXYCoordinatesInput(buff);
    addAnimationStepInput(buff);
    
    buff << "</p><input type='submit' value='Submit'/></msgBackground>";

    ADD_BODY(buff + contentStop);
    return replyOk;
  }
};

class WebHandlerMsgBackgroundPost : public WebHandler {
public:
  WebHandlerMsgBackgroundPost() : WebHandler(), display(Display::bind()) { }
  virtual ~WebHandlerMsgBackgroundPost() {}
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    StringMap postValues;
    const HandleRequestReply handleRequestReply = parsePost(requestInfo, requestOutput, postValues);
    if (handleRequestReply.code != HTTP_OK) return handleRequestReply;
    display.enqueueMsgBackgroundPost(postValues);
    return replyNoContent;
  }
private:
  Display& display;
};

class WebHandlerMsgMode : public WebHandler {
public:
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    std::string buff(contentStart);

    buff << "<form action='/msgMode' method='post'>"
	 << "<h1>Display Message</h1><p>"
	 << "<input type='text' size='";
    INT2BUFF(FORM_MSG_BOX_LEN);  // not the max len of content, just size of form
    buff << "' name='msg'><br/>";
    addFormFont(buff);
    buff << "<br/>Alternate font: ";
    addCheckBox(buff, "alternateFont", "1", "", false);

    buff << "<br/>Confetti: ";
    addCheckBox(buff, "confetti", "15", "", false);

    buff << "<br/>Bounce: ";
    addCheckBox(buff, "bounce", "1", "", false);

    buff << "<br/>NoScroll: ";
    addCheckBox(buff, "noScroll", "1", "", false);

    buff << "<br/>Blink: ";
    addCheckBox(buff, "blink", "1", "", false);

    addFormColor(buff, true /*includeAlternate*/);
    
    buff << "<br/>Scroll Repeat: ";
    addRadioButton(buff, "repeats", "0", "none", false);
    addRadioButton(buff, "repeats", "5", "5", false);
    addRadioButton(buff, "repeats", "10", "10", false);
    addRadioButton(buff, "repeats", "100", "100", true);

    buff << "<br/>Timeout (sec): ";
    addRadioButton(buff, "timeout", "3", "3", false);
    addRadioButton(buff, "timeout", "10", "10", false);
    addRadioButton(buff, "timeout", "60", "60", true);
    addRadioButton(buff, "timeout", "300", "300", false);
    
    addXYCoordinatesInput(buff);

    buff << "</p><input type='submit' value='Submit'/></msgMode>";

    ADD_BODY(buff + contentStop);
    return replyOk;
  }
};

HandleRequestReply WebHandler::parsePost(const RequestInfo& requestInfo,
					 RequestOutput& requestOutput,
					 StringMap& postValues) {
    std::string buff;

    if (requestInfo.requestBody == 0) RETURN_ERROR("no body in post request");

    const size_t bodyLen = evbuffer_get_length(requestInfo.requestBody);
    if (bodyLen >= MAX_POST_BODY_LEN || (int) bodyLen < 1) {
      std::string errMsg = "body len in post is bad (got:"; INT2STR(errMsg, bodyLen);
      errMsg << " max:"; INT2STR(errMsg, MAX_POST_BODY_LEN); errMsg << ")";
      RETURN_ERROR(errMsg);
    }

    char data[bodyLen + 1];
    evbuffer_copyout(requestInfo.requestBody, data, bodyLen);
 
#if 0
    // DEBUG!!!
    data[bodyLen] = 0;
    printf("%s: %s\n", requestInfo.uriPath, data);
#endif
    
    bool gettingValuePortion = false;  // state of what is being assembled (key vs value)
    char tmpBuffer[bodyLen + 1] = {0};
    int tmpBufferOffset = 0;
    const char* const currKey = tmpBuffer;
    const char* currValue = "";

    for (int i=0; i < (int) bodyLen; ++i) {
      char ch = data[i];

      // translate plus into space
      if (ch == '+') ch = ' ';

      // check for name delimiter
      if (ch == '=') {
	tmpBuffer[tmpBufferOffset] = 0;  // terminate key c str (key)
	++tmpBufferOffset;
	currValue = &tmpBuffer[tmpBufferOffset];
	tmpBuffer[tmpBufferOffset] = 0;  // terminate key c str (value)
	gettingValuePortion = true;
	continue;
      }

      // check for value delimiter
      if (ch == '&') {
	if (gettingValuePortion && *currKey != 0) {
	  tmpBuffer[tmpBufferOffset] = 0;  // terminate value c str
	  postValues[ currKey ] = currValue;  // add key/value to map
	}
	currValue = "";
	tmpBufferOffset = 0;  // reset usage of tmpBuffer
	tmpBuffer[tmpBufferOffset] = 0;  // terminate value c str
	gettingValuePortion = false;
	continue;
      }
      
      // convert encoded value to character
      if (ch == '%') {
	// protect against buffer being too small to hold 2 extra characters
	if (i >= (int) (bodyLen - 2)) {
	  tmpBuffer[tmpBufferOffset] = 0;  // terminate key c str
	  break;
	}
	const char hex[] = { data[i+1], data[i+2], 0 };
	ch = strtoul(hex, NULL, 16);
	i += 2;
      }

      tmpBuffer[tmpBufferOffset] = ch;
      tmpBufferOffset++;
    }

    // add final value from body of post
    if (gettingValuePortion && *currKey != 0) {
      tmpBuffer[tmpBufferOffset] = 0;  // terminate value c str
      postValues[ currKey ] = currValue;
    }
    
    return replyOk;
}

class WebHandlerMsgModePost : public WebHandler {
public:
  WebHandlerMsgModePost() : WebHandler(), display(Display::bind()) { }
  virtual ~WebHandlerMsgModePost() {}
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    StringMap postValues;
    const HandleRequestReply handleRequestReply = parsePost(requestInfo, requestOutput, postValues);
    if (handleRequestReply.code != HTTP_OK) return handleRequestReply;
    display.enqueueMsgModePost(postValues);
    return replyNoContent;
  }
private:
  Display& display;
};


class WebHandlerLedStrip : public WebHandler {
public:
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    std::string buff(contentStart);

    buff << "<form action='/ledStrip' method='post'>"
	 << "<h1>Led Strip Config</h1><p>";

    buff << "Pixels (0 to "; INT2BUFF(LedStrip::numberOfLeds - 1);
    buff << ") formatting:" << "<input type='text' size='";
    INT2BUFF(FORM_MSG_BOX_LEN);  // not the max len of content, just size of form
    buff << "' name='rawFormat'> e.g: pixels 3,4,5 with color R,G,B and pixel 9 with a 21bit color ==> 3-5:127,45,0 ; 9:762";

    buff << "<br/>Extra Param: <input type='text' size='5' name='" << ledStripParamExtra << "'>";

    buff << "<br/>Clear all Pixels: ";
    addCheckBox(buff, ledStripParamClearAllPixels, ledStripParamEnabled, "", false);

    buff << "<br/> Mode: ";
    LedStrip& ledStrip = LedStrip::bind();
    for (int i=0; i < ledStripModeCount; ++i) {
      addRadioButton(buff, ledStripParamLedStripMode, i, ledStrip.getLedStripModeStr( (LedStripMode)i ), i == ledStripModeManual);
    }
    
    buff << "<p>Default Color (used whan it was not specified in pixel format):"
	 << "<br/>"
	 << "Red (0-127): <input type='range' size=3 min=0 max=127 value=0 name='red'>"
	 << "  Green (0-127): <input type='range' size=3 min=0 max=127 value=0 name='green'>"
	 << "  Blue (0-127): <input type='range' size=3 min=0 max=127 value=0 name='blue'>";
    
    buff << "<br/>Timeout (sec): ";
    addRadioButton(buff, ledStripParamTimeout, "3", "3", true);
    addRadioButton(buff, ledStripParamTimeout, "10", "10", false);
    addRadioButton(buff, ledStripParamTimeout, "60", "60", false);
    addRadioButton(buff, ledStripParamTimeout, "300", "300", false);
    addRadioButton(buff, ledStripParamTimeout, "-1", "never", false);

    buff << "</p><input type='submit' value='Submit'/></ledStrip>";
    
    ADD_BODY(buff + contentStop);
    return replyOk;
  }
};


class WebHandlerLedStripPost : public WebHandler {
public:
  WebHandlerLedStripPost() : WebHandler(), ledStrip(LedStrip::bind()) { }
  virtual ~WebHandlerLedStripPost() {}
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    StringMap postValues;
    const HandleRequestReply handleRequestReply = parsePost(requestInfo, requestOutput, postValues);
    if (handleRequestReply.code != HTTP_OK) return handleRequestReply;
    ledStrip.enqueueMsgModePost(postValues);
    return replyNoContent;
  }
private:
  LedStrip& ledStrip;
};


class WebHandlerStop : public WebHandler {
public:
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    // TODO: if you are ever worried about non-intentional 'stops', consider adding logic here that checks things
    //       like requestInfo.remoteAddress or requestInfo.requestHeaders
    int rc = server_stop(requestInfo.workerPtr->s);
    return rc == 0 ? replyNoContent : replyInternalError;
  }
};

class WebHandlerHeadRoot : public WebHandler {
public:
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    return replyNoContent; // so boring!  :)
  }
};

class WebHandlerRoot : public WebHandler {
public:
  virtual HandleRequestReply process(const RequestInfo& requestInfo, RequestOutput& requestOutput) {
    std::string buff(contentStart);

    buff << "<h1>Office Clock main page</h1><p>";

    buff << "<br/><a href='status'>status</a>"
	 << "<br/><a href='msgMode'>msg mode</a>"
	 << "<br/><a href='imgBackground'>image background</a>"
	 << "<br/><a href='msgBackground'>message background</a>"
	 << "<br/><a href='ledStrip'>led strip</a>"
         << "<br/><a href='sound'>sound</a>"
	 << "<br/><a href='stop'>stop</a> (careful!)"
      ; // buff
    
    ADD_BODY(buff + contentStop);
    return replyOk;
  }
};

void WebHandlerInternal::_start() {
  webHandlers[ WebHandlerKey::uriNotFound ] = new WebHandlerNotFound;
  webHandlers[ WebHandlerKey::uriHead ] = new WebHandlerHeadRoot;

  WebHandlerRoot* webHandlerRoot = new WebHandlerRoot();
  webHandlers[ WebHandlerKey("/") ] = webHandlerRoot;
  webHandlers[ WebHandlerKey("/index.html") ] = webHandlerRoot;

  webHandlers[ WebHandlerKey("/status") ] = new WebHandlerStatus;

  webHandlers[ WebHandlerKey("/msgMode") ] = new WebHandlerMsgMode;
  webHandlers[ WebHandlerKey(EVHTTP_REQ_POST, "/msgMode") ] = new WebHandlerMsgModePost;

  webHandlers[ WebHandlerKey("/imgBackground") ] = new WebHandlerImgBackground;
  webHandlers[ WebHandlerKey(EVHTTP_REQ_POST, "/imgBackground") ] = new WebHandlerImgBackgroundPost;
  
  webHandlers[ WebHandlerKey("/msgBackground") ] = new WebHandlerMsgBackground;
  webHandlers[ WebHandlerKey(EVHTTP_REQ_POST, "/msgBackground") ] = new WebHandlerMsgBackgroundPost;

  webHandlers[ WebHandlerKey("/ledStrip") ] = new WebHandlerLedStrip;
  webHandlers[ WebHandlerKey(EVHTTP_REQ_POST, "/ledStrip") ] = new WebHandlerLedStripPost;

  webHandlers[ WebHandlerKey("/stop") ] = new WebHandlerStop;

  
}
