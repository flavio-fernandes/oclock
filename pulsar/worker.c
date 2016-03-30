/*
 * worker.c
 *
 *  Created on: Apr 5, 2012
 *      Author: abhinavsingh
 */

#include <stdlib.h>
#include <assert.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>

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
	int i;
	void *res;

	i = pthread_cancel(w->t);
	assert(i == 0);

	i = pthread_join(w->t, &res);
	assert(i == 0);
	assert(res == PTHREAD_CANCELED);

	evhttp_free(w->http);
	event_base_free(w->base);
	free(w);
}

static void *
worker_main(void *arg) {
	int ret;
	worker *w = (worker*) arg;

	w->base = event_base_new();
	assert(w->base != NULL);

	w->http = evhttp_new(w->base);
	assert(w->http != NULL);

	ret = evhttp_accept_socket(w->http, w->s->fd);
	assert(ret == 0);

	evhttp_set_gencb(w->http, &worker_handler, w);

	ret = event_base_dispatch(w->base);
	assert(ret == 0);

	evhttp_free(w->http);
	event_base_free(w->base);

	return NULL;
}

worker *
worker_new(server *s) {
	worker *w;
	w = (worker*) calloc(1, sizeof(worker));

	w->s = s;

	return w;
}

void
worker_start(worker *w) {
	pthread_create(&w->t, NULL, worker_main, w);
}
