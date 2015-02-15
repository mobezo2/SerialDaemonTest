#include "SerialLib8051.h"
#include "stdio.h"

#include <signal.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <mqueue.h>
#include <signal.h>
#include <semaphore.h>
#include "tlpi_hdr.h"
#include "tty_functions.h"
#include "typedef.h"


#include "SerialPacket.h"
#include "SerialMsgUtils.h"

#define TEST_INPUT_FILE_NAME "SerialDaemonTestInput.txt"
#define TEST_OUTPUT_FILE_NAME "SerialDaemonTestOutput.txt"


void main(){

	int32_t i = 0, readBytes=0;
	int32_t inputFd;


	uint8_t MsgDataIndex = 48;

	/* Buffer for FILE I/O for test purposes */
	uint8_t FileOutputBuffer[FILE_OUT_BUFF_SIZE]={0};

	printf("SerialLib Test Mode: Executing \n");


	/* Open for Writing */
	inputFd = open(TEST_INPUT_FILE_NAME, O_RDWR| O_CREAT | O_APPEND,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

	if(inputFd == -1)
		errExit("SerialDaemonTest: inputFd Open FAILED");

	/* Fill the Output Buffer */
	for(i = 0 ; i < FILE_OUT_BUFF_SIZE; i++){

		/* Every 6th byte is a char counter */
		if(i % 6 == 1){
			FileOutputBuffer[i] = MsgDataIndex;
			MsgDataIndex++;

			/* Reset to ASCII 0-9 */
			if(MsgDataIndex == 58){
				MsgDataIndex = 48;
			}

		}
		else{
			FileOutputBuffer[i] = 64;
		}

	}

	/* Write out data to file */
	write(inputFd, FileOutputBuffer, FILE_OUT_BUFF_SIZE);

	if(close(inputFd)==-1)
			errExit("close output file failed");

	int32_t Msg1Len=FILE_OUT_BUFF_SIZE/18;
	int32_t Msg2Len=FILE_OUT_BUFF_SIZE/18;
	int32_t Msg3Len=FILE_OUT_BUFF_SIZE/18;
	int32_t Msg4Len=FILE_OUT_BUFF_SIZE/18;


	/* Allocate  TxMsg Buffers */
	uint8_t *TxMsg1 = (uint8_t*)malloc(Msg1Len);
	uint8_t *TxMsg2 = (uint8_t*)malloc(Msg2Len);
	uint8_t *TxMsg3 = (uint8_t*)malloc(Msg3Len);
	uint8_t *TxMsg4 = (uint8_t*)malloc(Msg4Len);

	/* InitializeBuffers to Some known values */
	memset( (void*) TxMsg1, 0xAA, Msg1Len);
	memset( (void*) TxMsg2, 0xAA, Msg2Len);
	memset( (void*) TxMsg3, 0xAA, Msg3Len);
	memset( (void*) TxMsg4, 0xAA, Msg4Len);



	uint8_t Msg1ID=9;
	uint8_t Msg2ID=207;
	uint8_t Msg3ID=13;
	uint8_t Msg4ID=128;

	uint16_t Msg1SeqCnt = 20000;
	uint16_t Msg2SeqCnt = 276;
	uint16_t Msg3SeqCnt = 32 ;
	uint16_t Msg4SeqCnt = 0;


	/* Open for Reading */
	inputFd = open(TEST_INPUT_FILE_NAME, O_RDWR| O_CREAT | O_APPEND,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if(inputFd == -1)
		errExit("Input File Open Failed");
	/* Get data for Msg1 from input file  */
	readBytes = read(inputFd, (void *)TxMsg1, (size_t) Msg1Len );

	if(readBytes < 1)
		printf("Msg1: Error, or EOF encountered\n");

	/* Get data for Msg2 from input file  */
	readBytes = read(inputFd, (void *)TxMsg2, (size_t) Msg2Len );

	if(readBytes < 1)
		printf("Msg2: Error, or EOF encountered\n");


	/* Get data for Msg3 from input file  */
	readBytes = read(inputFd, (void *)TxMsg3, (size_t) Msg3Len );

	if(readBytes < 1)
		printf("Msg3: Error, or EOF encountered\n");

	/* Get data for Msg4 from input file
	 * First, move the file offset (via lseek) to the middle of the file
	 * (negative half the total of number bytes from the end,
	 * as specified by SEEK_END   */
	off_t CurrentOffset = lseek(inputFd, -FILE_OUT_BUFF_SIZE/2, SEEK_END);
	readBytes = read(inputFd, (void *)TxMsg4, (size_t) Msg4Len );


	if(readBytes < 1)
		errMsg("Msg4: Error, or EOF encountered\n");

	/* Open QUEUE for the first time */
	/* Serial8051Open(SERIAL_TX_QUEUE); */
	int32_t ReturnVal = 0;

	/* Send Message 1 */
	ReturnVal = Serial8051Send(TxMsg1, Msg1Len, Msg1ID, Msg1SeqCnt, 37, 0 );

	if(ReturnVal < 0)
		printf("TX Msg1 Return Val = %i\n", ReturnVal);


	/* Send Message 2 */
	ReturnVal = Serial8051Send(TxMsg2, Msg2Len, Msg2SeqCnt, 276, 37, 0  );

	if(ReturnVal < 0)
		printf("TX Msg2 Return Val = %i\n", ReturnVal);



	/* Send Message 3 */
	ReturnVal = Serial8051Send(TxMsg3, Msg3Len, Msg3SeqCnt, 32, 37, 0  );

	if(ReturnVal < 0)
		printf("TX Msg2 Return Val = %i\n", ReturnVal);

	sleep(1);

	/* Send Message 4 */
	ReturnVal = Serial8051Send(TxMsg4, Msg4Len, Msg4SeqCnt, 0, 37, 0  );

	if(ReturnVal < 0)
		printf("TX Msg4 Return Val = %i \n", ReturnVal);

	sleep(1);

	if(close(inputFd)==-1)
			errExit("close input file failed");

	printf("SerialDaemon Test Mode: Cleared Message Sends \n");


	sleep(1);

	/* Allocate  RxMsg Buffers (magic number '2' is for a new line character
	 * and a null byte to be added to facillitate display/debug */
	uint8_t *RxMsg1 = (uint8_t*)malloc((FILE_OUT_BUFF_SIZE));
	uint8_t *RxMsg2 = (uint8_t*)malloc((FILE_OUT_BUFF_SIZE));
	uint8_t *RxMsg3 = (uint8_t*)malloc((FILE_OUT_BUFF_SIZE));
	uint8_t *RxMsg4 = (uint8_t*)malloc((FILE_OUT_BUFF_SIZE));


	RxMsgInfo *Msg1Info = (RxMsgInfo*) malloc (sizeof(RxMsgInfo));
	RxMsgInfo *Msg2Info = (RxMsgInfo*) malloc (sizeof(RxMsgInfo));
	RxMsgInfo *Msg3Info = (RxMsgInfo*) malloc (sizeof(RxMsgInfo));
	RxMsgInfo *Msg4Info = (RxMsgInfo*) malloc (sizeof(RxMsgInfo));

	printf("SerialLib Test Mode: Cleared Receive Queue Mallocs \n");

	/* InitializeBuffers to Some known values */
	memset( (void*) RxMsg1, 0xBB, FILE_OUT_BUFF_SIZE);
	memset( (void*) RxMsg2, 0xBB, FILE_OUT_BUFF_SIZE);
	memset( (void*) RxMsg3, 0xBB, FILE_OUT_BUFF_SIZE);
	memset( (void*) RxMsg4, 0xBB, FILE_OUT_BUFF_SIZE);

	printf("SerialDaemon Test Mode: Cleared Receive Queue Memsets \n");
	/* Receive Msg1  */
	readBytes = Serial8051Receive(RxMsg1, Msg1Info);

	printf(" Serial Daemon RxMsg1: Read Bytes = %i Msg1Len = %u \n", readBytes, Msg1Info->MsgLength );

	if(readBytes < 0)
		errMsg("Rx Msg1 Failed");
	else
	{
			printf("\n RxMsg1: ");
			for(i=0; i<( FILE_OUT_BUFF_SIZE ); i++)
			{
				printf(" %x",RxMsg1[i]);
			}
			printf("\n");
	}


	/* Receive Msg2  */
	readBytes = Serial8051Receive(RxMsg2, Msg2Info);

	printf(" Serial Daemon Test Mode RxMsg2: Read Bytes = %i Msg2Len = %u \n", readBytes, Msg2Info->MsgLength );

	if(readBytes < 0)
		printf("Rx Msg2 Failed");
	else
	{
			printf("\n RxMsg2: ");
			for(i=0; i<( FILE_OUT_BUFF_SIZE ); i++)
			{
				printf(" %x",RxMsg2[i]);
			}
			printf("\n");
	}


	/* Receive Msg3  */
	readBytes = Serial8051Receive(RxMsg3, Msg3Info);

	printf(" Serial Daemon Test Mode RxMsg3: Read Bytes = %i Msg3Len = %i \n", readBytes, Msg3Info->MsgLength );

	if(readBytes < 0)
		printf("Serial Daemon Test Mode Rx Msg3 Failed");
	else
	{
			printf("\n RxMsg3: ");
			for(i=0; i<( FILE_OUT_BUFF_SIZE ); i++)
			{
				printf(" %x",RxMsg3[i]);
			}
			printf("\n");
	}


	/* Receive Msg4  */
	readBytes = Serial8051Receive(RxMsg4, Msg4Info);

	printf("Serial Daemon Test Mode RxMsg4: Read Bytes = %i Msg4Len = %i \n", readBytes, Msg4Info->MsgLength );


	if(readBytes < 0)
		printf("Serial Daemon Test Mode Rx Msg4 Failed");
	else
	{
			printf("\n RxMsg4: ");
			for(i=0; i<( FILE_OUT_BUFF_SIZE ); i++)
			{
				printf(" %x",RxMsg4[i]);
			}
			printf("\n");
	}


	/* Remove TX Queue after we close */

	printf("Exiting Serial Daemon Test \n");
	exit(EXIT_SUCCESS);
}
