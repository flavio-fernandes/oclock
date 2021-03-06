/*
 * conf.h
 *
 *  Created on: Apr 3, 2012
 *      Author: abhinavsingh
 */

#ifndef CONF_H_
#define CONF_H_

#include "logger.h"
#include "common.h"

#define WEBSERVER_DEFAULT_PORT     80
#define WEBSERVER_DEFAULT_WORKERS  4

#define MQTT_BROKER_DEFAULT_IP    "192.168.10.238"
#define MQTT_BROKER_DEFAULT_PORT  1883
#define MQTT_BROKER_DEFAULT_KA    182

struct _conf {
	char *ip;
	unsigned short port;
	int workers;
};

void
conf_free(conf *cfg);

conf *
conf_new(char *ip, unsigned short port, int workers);

#endif /* CONF_H_ */
