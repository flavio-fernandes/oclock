/*
 * worker.c
 *
 *  Created on: Apr 5, 2012
 *      Author: abhinavsingh
 */

#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include <sys/socket.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>
#include <event2/util.h>

#include "server.h"
#include "worker.h"
#include "webHandler.h"

void
worker_handler(struct evhttp_request *req, void *arg) {
	worker* workerPtr = (worker*) arg;
	struct evkeyvalq* replyHeaders;
	struct evbuffer* replyBody;
	HandleRequestReply reply;
	
	/* prepare out headers */
	replyHeaders = evhttp_request_get_output_headers(req);
	evhttp_add_header(replyHeaders, "Content-Type", "text/html; charset=UTF-8");
	evhttp_add_header(replyHeaders, "Server", SERVER_NAME);
	evhttp_add_header(replyHeaders, "Cache-Control", "private, max-age=0, no-cache, no-store");

	/* prepare out buffer */
	replyBody = evbuffer_new();

	/* small incision to get bulk of changes outside pulsar codebase */
	reply = handleRequest(req, workerPtr, replyHeaders, replyBody);
	
	/* send reply */
	evhttp_send_reply(req, reply.code, reply.reason, replyBody);

	evbuffer_free(replyBody);
}

void
worker_free(worker *w) {
	if (w == NULL) return;
	if (w->started) {
		(void)worker_stop(w);
		(void)pthread_join(w->t, NULL);
	}

	if (w->stopSockets[0] >= 0) evutil_closesocket(w->stopSockets[0]);
	if (w->stopSockets[1] >= 0) evutil_closesocket(w->stopSockets[1]);
	if (w->http != NULL) evhttp_free(w->http);
	if (w->stopEvent != NULL) event_free(w->stopEvent);
	if (w->base != NULL) event_base_free(w->base);
	free(w);
}

static void
worker_stop_handler(evutil_socket_t fd, short /*event*/, void *arg) {
	char drain[32];
	worker *w = (worker*) arg;

	while (recv(fd, drain, sizeof(drain), 0) > 0) {
	}
	event_base_loopexit(w->base, NULL);
}

static void *
worker_main(void *arg) {
	int ret;
	worker *w = (worker*) arg;

	w->base = event_base_new();
	assert(w->base != NULL);

	w->http = evhttp_new(w->base);
	assert(w->http != NULL);

	w->stopEvent = event_new(w->base, w->stopSockets[0],
		EV_READ|EV_PERSIST, &worker_stop_handler, w);
	assert(w->stopEvent != NULL);
	ret = event_add(w->stopEvent, NULL);
	assert(ret == 0);

	ret = evhttp_accept_socket(w->http, w->s->fd);
	assert(ret == 0);

	evhttp_set_gencb(w->http, &worker_handler, w);

	ret = event_base_dispatch(w->base);
	assert(ret == 0);

	evhttp_free(w->http);
	w->http = NULL;
	event_free(w->stopEvent);
	w->stopEvent = NULL;
	event_base_free(w->base);
	w->base = NULL;
	evutil_closesocket(w->stopSockets[0]);
	w->stopSockets[0] = -1;

	return NULL;
}

worker *
worker_new(server *s) {
	worker *w;
	w = (worker*) calloc(1, sizeof(worker));
	if (w == NULL) return NULL;

	w->s = s;
	w->stopSockets[0] = -1;
	w->stopSockets[1] = -1;

	return w;
}

int
worker_start(worker *w) {
	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, w->stopSockets) != 0) {
		return errno != 0 ? errno : -1;
	}
	if (evutil_make_socket_nonblocking(w->stopSockets[0]) != 0 ||
		evutil_make_socket_nonblocking(w->stopSockets[1]) != 0) {
		evutil_closesocket(w->stopSockets[0]);
		evutil_closesocket(w->stopSockets[1]);
		w->stopSockets[0] = -1;
		w->stopSockets[1] = -1;
		return errno != 0 ? errno : -1;
	}

	const int rc = pthread_create(&w->t, NULL, worker_main, w);
	if (rc != 0) {
		evutil_closesocket(w->stopSockets[0]);
		evutil_closesocket(w->stopSockets[1]);
		w->stopSockets[0] = -1;
		w->stopSockets[1] = -1;
	}
	w->started = rc == 0;
	return rc;
}

int
worker_stop(worker *w) {
	const char stop = 1;
	int rc;

	if (w == NULL || !w->started || w->stopRequested) return 0;

	do {
		rc = send(w->stopSockets[1], &stop, sizeof(stop), MSG_NOSIGNAL);
	} while (rc < 0 && errno == EINTR);

	if (rc >= 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
		w->stopRequested = 1;
		return 0;
	}
	return -1;
}
