/*
 * demo_sigio.c
 *
 *  Created on: Jul 14, 2014
 *      Author: mbezold
 */

#include <signal.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "tlpi_hdr.h"
#include "tty_functions.h"

#include "SerialPacket.h"


#define FILEPATH "/dev/ttyO4"
#define MAX_READ_BYTES 255
#define MAX_RX_BUFF_SIZE 1020

#define DEBUG_VERBOSITY 0

static volatile sig_atomic_t gotSigio = 0;

static char
CheckForHeader(char *ReceivedByte)
{

	static volatile char MsgStatus = 0;
	const char StartB=1, StartA=2, StartD=4, StartC=8, StartA2=16, StartF=32, StartE=64;
		/* Keep track of what bytes we have received and in what order
			through ORing the status corresponding to each byte and checking
			the static sum 'MsgStatus' each iteration...
			If we don't receive one of these hex values in the correct order
			then our default condition sets resets the sum
			MOB 8-18-14--This could use a rewrite to ensure our most common cases
			get hit first and result in a function return immediately. Keeping
			the same for now to ensure code reliability.
		*/
		printf("DEBUG:Checking for header/new message \n");
		if((MsgStatus == 0) && (*ReceivedByte == 0x0B)){
			MsgStatus |= StartB;
			return MsgStatus;
		}
		else if ((MsgStatus == 1) && (*ReceivedByte == 0x0A)){
			MsgStatus |= StartA;
			return MsgStatus;
		}
		else if ((MsgStatus == 3) && (*ReceivedByte == 0x0D)){
			MsgStatus |= StartD;
			return MsgStatus;
		}
		else if ((MsgStatus == 7) && (*ReceivedByte == 0x0C)){
			MsgStatus |= StartC;
			return MsgStatus;
		}
		else if ((MsgStatus == 15) && (*ReceivedByte == 0x0A)){
			MsgStatus |= StartA2;
			return MsgStatus;
		}
		else if ((MsgStatus == 31) && (*ReceivedByte == 0x0A)){
			MsgStatus |= StartF;
			return MsgStatus;
		}
		else if ((MsgStatus == 63) && (*ReceivedByte == 0x0F)){
			MsgStatus |= StartE;
			return MsgStatus;
		}
		else {
			MsgStatus = 0;
			return MsgStatus;
		}

}

static void
sigioHandler(int sig)
{
	 gotSigio = 1;

}

int
main(int argc, char *argv[])
{
	int flags, j, cnt =0, sleepReturn, numWritten, strngLngth, i;
	struct timeval CurrentTime;
	struct termios OrigTermios, ModifiedTermios;

	char RxBuffer[MAX_RX_BUFF_SIZE];
	char ReadBuff[MAX_READ_BYTES];
	char RxByte[30];
	char FileOutputBuffer[60]={0};
	char * CurrentArrayPosition;

	int TotalRxBytes = 0, AccumulatedRxByteCount = 0, MsgLength = 0;


	int outputFd, ttyFd;
	// Flags to represent Header Bytes, we can OR them together to confirm that we have received a header

	char *CurrentTimeString;
	struct sigaction sa;
	Boolean done;

	const char *FileName="SerialLog.txt";

	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigioHandler;
	if (sigaction(SIGIO, &sa, NULL) == -1 )
		errExit("sigaction");

	SerialPacket * CurrentSerialPacket=malloc(sizeof(SerialPacket));

	/* Open Options Described in: https://www.cmrr.umn.edu/~strupp/serial.html */
/*	The O_NOCTTY flag tells UNIX that this program doesn't want to
    be the "controlling terminal" for that port. If you don't specify this then any
    input (such as keyboard abort signals and so forth) will affect your process.

	The O_NDELAY flag tells UNIX that this program doesn't care what state the
    DCD signal line is in - whether the other end of the port is up and running.
    If you do not specify this flag, your process will be put to sleep until
    the DCD signal line is the space voltage. */
	ttyFd= open(FILEPATH, O_RDWR | O_NOCTTY | O_NDELAY );


    if (ttyFd == -1)
    	errExit("Serial Terminal Open Failed");

    /* Acquire Current Serial Terminal settings so that we can go ahead and
      change and later restore them */
    if(tcgetattr(ttyFd, &OrigTermios)==-1)
    	errExit("TCGETATTR failed ");

    ModifiedTermios = OrigTermios;

    //Prevent Line clear conversion to new line character
    ModifiedTermios.c_iflag &= ~ICRNL;

  /*  Put in Cbreak mode where break signals lead to interrupts */
	if(tcsetattr(ttyFd, TCSAFLUSH, &OrigTermios)==-1)
		errExit(" Couldn't modify terminal settings ");


	/* set owner process that is to receive "I/O possible" signal */
	/*NOTE: replace stdin_fileno with the /dev/tty04 or whatever */
	if (fcntl(ttyFd, F_SETOWN, getpid()) == -1)
		errExit("fcntl(F_SETOWN)");

	/* enable "I/O Possible" signalling and make I/O nonblocking for FD
	 * O_ASYNC flag causes signal to be routed to the owner process, set
	 * above */
	flags = fcntl(ttyFd, F_GETFL );
	if (fcntl(ttyFd, F_SETFL, flags | O_ASYNC | O_NONBLOCK) == -1 ){
		errExit("fcntl(F_SETFL)");
	}

	for ( i=0 ; i<100 ; i++){

	#if DEBUG_VERBOSITY > 0
		printf("Sleep Return = %i Seconds \n", sleepReturn);
	#endif

	//Puts the program to sleep for 30 seconds, or until we receive a signal
	sleepReturn=nanosleep(30);
		if(gotSigio ){

			//want to avoid interrupting the reads by another signal.
			sa.sa_handler = SIG_IGN;
			if (sigaction(SIGIO, &sa, NULL) == -1 )
				errExit("sigaction, sig ignore");


			done=0;
			TotalRxBytes=0;
			AccumulatedRxByteCount = 0;
			memset(ReadBuff, 0, sizeof(ReadBuff));
			memset(RxBuffer, 0, sizeof(RxBuffer));


             /* Read buffered Serial data using the file descriptor until we
               don't receive anymore (signaled by done flag) */
			while ( !done)  {
				TotalRxBytes=read(ttyFd, ReadBuff, MAX_READ_BYTES);

				/*Terminate Loop if we see 0 bytes returned, or an ERROR */
				if(TotalRxBytes <= 0){
					done=1;
					continue;
				}
				else{

					#if DEBUG_VERBOSITY > 10
					/* Print the raw hex values, for debugging */
						printf("\n Read Buff ");
						for(i=0; i<sizeof(ReadBuff); i++){
							printf(" %x",ReadBuff[i]);
						}
					#endif

					//Copy bytes received this loop iteration into the RxBuff
					CurrentArrayPosition=&RxBuffer[AccumulatedRxByteCount];
					strncpy(CurrentArrayPosition, ReadBuff, TotalRxBytes);
					memset(ReadBuff, 0, sizeof(ReadBuff));

					#if DEBUG_VERBOSITY > 5
						printf("\nRxBuffer %s ",RxBuffer);
					#endif

					AccumulatedRxByteCount+=TotalRxBytes;

					#if DEBUG_VERBOSITY > 10
					printf("\nCurrent Array Positions is %i \n", *CurrentArrayPosition);
					#endif
				}
			}

			/* Outside the while loop. Process our received data */

			/* Get Current System Time, copy it to our serial packet header */
				if (gettimeofday(&CurrentTime, NULL) == -1 ){
						usageErr("Couldn't get current time \n");
						memset(&CurrentTime, 0x0, sizeof(CurrentTime));
				}
				else{
						CurrentTimeString = ctime(&CurrentTime.tv_sec);
						strcpy(CurrentSerialPacket->TimeReceived,CurrentTimeString);
				}


			//Copy data into text file, adding null byte and formatting appropriately
			sprintf(FileOutputBuffer, "%s \n %s \n", CurrentTimeString, RxBuffer);
			outputFd = open(FileName, O_RDWR| O_CREAT | O_APPEND,
						S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
			write(outputFd, FileOutputBuffer,strlen(FileOutputBuffer));

			if(close(outputFd)==-1)
					errExit("close output file failed");

			#if DEBUG_VERBOSITY > 5
				printf("New Complete Message Received \n");
			#endif

			//Done with data processing. Reset signal IO after we read the available data
			sa.sa_handler = sigioHandler;
			if (sigaction(SIGIO, &sa, NULL) == -1 )
				errExit("sigaction ,reset signal");
			}
		}

	/* Restore original terminal settings */
	if(tcsetattr(ttyFd, TCSAFLUSH, &OrigTermios)==-1)
		errExit(" Couldn't restore original terminal settings ");


	printf("Exiting Serial Daemon \n");
	exit(EXIT_SUCCESS);
}

