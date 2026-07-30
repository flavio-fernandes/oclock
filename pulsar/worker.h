/*
 * worker.h
 *
 *  Created on: Apr 5, 2012
 *      Author: abhinavsingh
 */

#ifndef WORKER_H_
#define WORKER_H_

#include <pthread.h>
#include <event2/util.h>
#include "common.h"

struct _worker {
	pthread_t t;
	int started;
	int stopRequested;
	evutil_socket_t stopSockets[2];

	struct event_base *base;
	struct event *stopEvent;
	struct evhttp *http;

	server *s;
};

worker *
worker_new(server *s);

int
worker_start(worker *w);

int
worker_stop(worker *w);

void
worker_free(worker *w);

#endif /* WORKER_H_ */
