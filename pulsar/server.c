/*
 * server.c
 *
 *  Created on: Apr 3, 2012
 *      Author: abhinavsingh
 */

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include <event2/event.h>

#include "server.h"
#include "worker.h"
#include "conf.h"

int
server_setup_socket(const char *ip, short port) {
	int fd, reuse = 1, ret;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memset(&(addr.sin_addr), 0, sizeof(addr.sin_addr));
	if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
		return -1;
	}

	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd == -1) return -1;

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	if (ret == -1) {
		close(fd);
		return -1;
	}

	ret = evutil_make_socket_nonblocking(fd);
	if (ret == -1) {
		close(fd);
		return -1;
	}

	ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1) {
		close(fd);
		return -1;
	}

	ret = listen(fd, SOMAXCONN);
	if (ret == -1) {
		close(fd);
		return -1;
	}

	return fd;
}

server *
server_new(conf *cfg, logger *log) {
	int i;

	server *s;
	s = (server*) calloc(1, sizeof(server));
	if (s == NULL) return NULL;
	s->fd = -1;

	s->t = pthread_self();
	
	/* read cfg file */
	s->cfg = cfg;

	/* log */
	s->log = log;

	/* setup workers */
	s->w = (worker**) calloc(s->cfg->workers, sizeof(worker *));
	if (s->w == NULL) {
		free(s);
		return NULL;
	}
	for(i=0; i<s->cfg->workers; i++) {
		s->w[i] = worker_new(s);
		if (s->w[i] == NULL) {
			while (i > 0) free(s->w[--i]);
			free(s->w);
			free(s);
			return NULL;
		}
	}
	log_it(s->log, PULSAR_DEBUG, "workers initialized ...");

	return s;
}

void
server_free(server *s) {
	int i;
	if (s == NULL) return;

	/* shutdown worker threads */
	for(i=0; i<s->cfg->workers; i++) {
		(void)worker_stop(s->w[i]);
	}
	for(i=0; i<s->cfg->workers; i++) {
		worker_free(s->w[i]);
	}

	/* free */
	if (s->signalInt != NULL) {
		event_del(s->signalInt);
		event_free(s->signalInt);
	}
	if (s->signalHup != NULL) {
		event_del(s->signalHup);
		event_free(s->signalHup);
	}
	if (s->base != NULL) event_base_free(s->base);
	if (s->fd >= 0) close(s->fd);
	free(s->w);
	conf_free(s->cfg);
	log_free(s->log);
	free(s);
}

static void
server_sig_handler(evutil_socket_t /*fd*/, short /*event*/, void *arg) {
	server *s = (server*) arg;
	event_base_loopexit(s->base, NULL);
}

int
server_start(server *s) {
	int i;
	int rc;

	s->base = event_base_new();
	if (s->base == NULL) {
		server_free(s);
		return -1;
	}

	s->signalInt = event_new(s->base, SIGINT, EV_SIGNAL|EV_PERSIST, &server_sig_handler, s);
	if (s->signalInt == NULL || event_add(s->signalInt, NULL) != 0) {
		server_free(s);
		return -1;
	}
	s->signalHup = event_new(s->base, SIGHUP, EV_SIGNAL|EV_PERSIST, &server_sig_handler, s);
	if (s->signalHup == NULL || event_add(s->signalHup, NULL) != 0) {
		server_free(s);
		return -1;
	}

	s->fd = server_setup_socket(s->cfg->ip, s->cfg->port);
	if (s->fd == -1) {
		server_free(s);
		return -1;
	}

	/* start workers */
	for(i=0; i<s->cfg->workers; i++) {
		if (worker_start(s->w[i]) != 0) {
			server_free(s);
			return -1;
		}
	}

	rc = event_base_dispatch(s->base);

	server_free(s);
	return rc < 0 ? -1 : 0;
}

int
server_stop(server *s) {
  if (s == NULL) return -1;
  return pthread_kill(s->t, SIGHUP);
}
