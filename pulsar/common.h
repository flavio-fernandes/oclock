/*
 * common.h
 *
 *  Created on: Apr 22, 2012
 *      Author: abhinavsingh
 */

#ifndef COMMON_H_
#define COMMON_H_


typedef struct _server server;
typedef struct _worker worker;
typedef struct _logger logger;
typedef struct _conf conf;

typedef struct PulsarServerInfo_t {
  server* s;
  conf* cfg;
  logger* log;
} PulsarServerInfo;

#endif /* COMMON_H_ */
