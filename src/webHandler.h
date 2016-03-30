#ifndef __WEB_HANDLER_H
#define __WEB_HANDLER_H

#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>

#include "worker.h"

typedef struct HandleRequestReply_t {
  int code;            // e.g. HTTP_OK
  const char* reason;  // e.g. "OK"
} HandleRequestReply;

HandleRequestReply handleRequest(struct evhttp_request* req, worker* workerPtr,
				 struct evkeyvalq* outHeaders, struct evbuffer* replyBody);

#endif // ifndef __WEB_HANDLER_H
