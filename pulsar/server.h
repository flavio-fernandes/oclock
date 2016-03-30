/*
 * server.h
 *
 *  Created on: Apr 3, 2012
 *      Author: abhinavsingh
 */

#ifndef SERVER_H_
#define SERVER_H_

#define SERVER_NAME "pulsar"
#define SERVER_VERSION "0.1"

#include "common.h"

struct _server {
	pthread_t t;

	/* config */
	conf *cfg;

	/* socket */
	int fd;
	struct event_base *base;
	struct event *signalInt;
	struct event *signalHup;

	/* workers */
	worker **w;

	/* log */
	logger *log;
};

server *
server_new(conf *cfg, logger *log);

void
server_start(server *s);

int
server_stop(server *s);

#endif /* SERVER_H_ */
