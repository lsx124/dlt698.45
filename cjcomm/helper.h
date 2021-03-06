/*
 * helper.h
 *
 *  Created on: 2017-7-20
 *      Author: zhoulihai
 */

#include "StdDataType.h"

#ifndef HELPER_H_
#define HELPER_H_

int helperCheckIp(INT8U *pppip);
int helperKill(char *name, int timeout);
int helperGetInterFaceIp(char *interface, char *ips);
int helperConnect(char *interface, MASTER_STATION_INFO *info);
int helperCheckConnect(char *interface, int fd);
int helperComOpen1200(int port, int baud, unsigned char par, unsigned char stopb,
		unsigned char bits);
int helperComOpen9600(int port, int baud, unsigned char par, unsigned char stopb,
		unsigned char bits);

int helperReadPositionGet(int length);
void helperPeerStat(int fd, char *info);
void helperChangeNetType();

MASTER_STATION_INFO helperGetNextNetIp();
MASTER_STATION_INFO helperGetNextGPRSIp();
#endif /* HELPER_H_ */
