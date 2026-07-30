/*
 * worker.h
 *
 *  Created on: Apr 5, 2012
 *      Author: abhinavsingh
 */

#ifndef WORKER_H_
#define WORKER_H_

#include <pthread.h>
#include "common.h"

struct _worker {
	pthread_t t;
	int started;

	struct event_base *base;
	struct evhttp *http;

	server *s;
};

worker *
worker_new(server *s);

int
worker_start(worker *w);

void
worker_free(worker *w);

#endif /* WORKER_H_ */
