/*
 * SerialDaemon.h
 *
 *  Created on: Sep 23, 2014
 *      Author: mbezold
 */

#ifndef SERIALDAEMON_H_
#define SERIALDAEMON_H_

#define SERIAL_DAEMON_SEM "/SerialDaemonSem"

#define SERIAL_RX_LOG_FILENAME "SerialRXLog.txt"
#define SERIAL_TX_LOG_FILENAME "SerialTXLog.txt"


#define SERIAL_FILEPATH "/dev/ttyO4"
//#define FILE_OUT_BUFF_SIZE_DAEMON	60
#define MAX_READ_BYTES 		255
#define MAX_RX_BUFF_SIZE 	1020


#define SEM_OPEN_FAIL -1
#define SERIAL_TX_WRITE_FAIL -2
#define SERIAL_TX_ZERO_BYTES -3


#endif /* SERIALDAEMON_H_ */
