/*
 * pulsar.c
 *
 *  Created on: Apr 3, 2012
 *      Author: abhinavsingh
 *
 *		Memory Leak Check:
 *		-------------------
 *      valgrind --leak-check=full --show-reachable=yes ./pulsar
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "common.h"
#include "conf.h"
#include "server.h"

void
print_usage(char *exe) {
	printf("Usage: %s [-p port] [-w workers] [-M broker] [-P brokerPort] [-K brokerKeepAlive] [-h]\n\n"
			"Options:\n"
			"  -p port          : tcp port number to listen on (default: %d)\n"
			"  -w workers       : number of worker threads to start for accepting connections (default: %d)\n"
			"  -v verbosity     : 0 <= verbosity <= 4 (default: 4)\n"
			"  -l logfile       : file to log into (default: stderr)\n"
			"  -M MqttSrvAddr   : the hostname or ip address of the broker to connect to (default: %s)\n"
			"  -P MqttSrvPort   : the port of that mqtt server (default: %d)\n"
	                "  -K MqttKeepAlive : mqtt keep alive interval in seconds (default: %d)\n" 
	                "  -h               : display this help message\n",
	       exe, WEBSERVER_DEFAULT_PORT, WEBSERVER_DEFAULT_WORKERS,
	       MQTT_BROKER_DEFAULT_IP, MQTT_BROKER_DEFAULT_PORT, MQTT_BROKER_DEFAULT_KA);
}

static PulsarServerInfo pulsarServerInfo = {0};

void
pulsar_parse_args(int argc, char *argv[]) {
	int opt;

	/* defaults */
	pulsarServerInfo.log = log_new("log/pulsar.log", PULSAR_DEBUG);
	pulsarServerInfo.cfg = conf_new(strdup("0.0.0.0"), WEBSERVER_DEFAULT_PORT, WEBSERVER_DEFAULT_WORKERS);

	/* read input options */
	while((opt = getopt(argc,argv,"p:w:v:l:M:P:K:h")) != -1) {
		switch(opt) {
		case 'p':
			pulsarServerInfo.cfg->port = atoi(optarg);
			break;
		case 'w':
			pulsarServerInfo.cfg->workers = atoi(optarg);
			break;
		case 'v':
			pulsarServerInfo.log->verbosity = (log_level) atoi(optarg);
			break;
		case 'l':
			pulsarServerInfo.log->logfile = optarg;
			break;
		case 'M':
		case 'P':
		case 'K':
   		        // not handled here....
		        break;
		case 'h':
			print_usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}
}

int
pulsar_main() {
	pulsarServerInfo.s = server_new(pulsarServerInfo.cfg, pulsarServerInfo.log);
	server_start(pulsarServerInfo.s);

	return EXIT_SUCCESS;
}

PulsarServerInfo* getPulsarServerInfo() {
  return &pulsarServerInfo;
}
