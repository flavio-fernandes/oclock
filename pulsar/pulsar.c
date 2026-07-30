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
#include <errno.h>
#include <limits.h>

#include "common.h"
#include "conf.h"
#include "server.h"

void
print_usage(char *exe) {
		printf("Usage: %s [-b address] [-p port] [-w workers] [-M broker] [-P brokerPort] [-K brokerKeepAlive] [-h]\n\n"
				"Options:\n"
				"  -b address       : IPv4 address to bind (default: %s)\n"
				"  -p port          : tcp port number to listen on (default: %d)\n"
			"  -w workers       : number of worker threads to start for accepting connections (default: %d)\n"
			"  -v verbosity     : 0 <= verbosity <= 4 (default: 4)\n"
			"  -l logfile       : file to log into (default: stderr)\n"
			"  -M MqttSrvAddr   : the hostname or ip address of the broker to connect to (default: %s)\n"
			"  -P MqttSrvPort   : the port of that mqtt server (default: %d)\n"
	                "  -K MqttKeepAlive : mqtt keep alive interval in seconds (default: %d)\n" 
	                "  -h               : display this help message\n",
		       exe, WEBSERVER_DEFAULT_IP, WEBSERVER_DEFAULT_PORT, WEBSERVER_DEFAULT_WORKERS,
		       MQTT_BROKER_DEFAULT_IP, MQTT_BROKER_DEFAULT_PORT, MQTT_BROKER_DEFAULT_KA);
}

static PulsarServerInfo pulsarServerInfo = {0};

static long
parse_number(const char *name, const char *value, long minimum, long maximum) {
	char *end = NULL;
	errno = 0;
	const long parsed = strtol(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' || parsed < minimum || parsed > maximum) {
		fprintf(stderr, "Invalid %s: %s\n", name, value);
		exit(EXIT_FAILURE);
	}
	return parsed;
}

void
pulsar_parse_args(int argc, char *argv[]) {
	int opt;

	/* defaults */
	pulsarServerInfo.log = log_new("log/pulsar.log", PULSAR_DEBUG);
	pulsarServerInfo.cfg = conf_new(strdup(WEBSERVER_DEFAULT_IP), WEBSERVER_DEFAULT_PORT, WEBSERVER_DEFAULT_WORKERS);
	if (pulsarServerInfo.log == NULL || pulsarServerInfo.cfg == NULL) {
		fprintf(stderr, "Unable to allocate server configuration\n");
		exit(EXIT_FAILURE);
	}

	/* read input options */
	while((opt = getopt(argc,argv,"b:p:w:v:l:M:P:K:h")) != -1) {
		switch(opt) {
			case 'b':
				free(pulsarServerInfo.cfg->ip);
				pulsarServerInfo.cfg->ip = strdup(optarg);
				if (pulsarServerInfo.cfg->ip == NULL) {
					fprintf(stderr, "Unable to allocate bind address\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'p':
				pulsarServerInfo.cfg->port =
					(unsigned short) parse_number("port", optarg, 1, 65535);
				break;
			case 'w':
				pulsarServerInfo.cfg->workers =
					(int) parse_number("worker count", optarg, 1, 64);
				break;
			case 'v':
				pulsarServerInfo.log->verbosity =
					(log_level) parse_number("verbosity", optarg, 0, 4);
			break;
		case 'l':
			if (log_set_file(pulsarServerInfo.log, optarg) != 0) {
				fprintf(stderr, "Unable to open log file: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'M':
		case 'P':
		case 'K':
   		        // not handled here....
		        break;
			case 'h':
				print_usage(argv[0]);
				/* Preserve the original command-line contract. */
				exit(EXIT_FAILURE);
			default:
				print_usage(argv[0]);
				exit(EXIT_FAILURE);
			}
	}
}

int
pulsar_main() {
	pulsarServerInfo.s = server_new(pulsarServerInfo.cfg, pulsarServerInfo.log);
	if (pulsarServerInfo.s == NULL) {
		conf_free(pulsarServerInfo.cfg);
		log_free(pulsarServerInfo.log);
		pulsarServerInfo.cfg = NULL;
		pulsarServerInfo.log = NULL;
		return EXIT_FAILURE;
	}

	const int rc = server_start(pulsarServerInfo.s);
	pulsarServerInfo.s = NULL;
	pulsarServerInfo.cfg = NULL;
	pulsarServerInfo.log = NULL;
	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

PulsarServerInfo* getPulsarServerInfo() {
  return &pulsarServerInfo;
}
