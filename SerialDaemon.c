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
#include <semaphore.h>
#include <time.h>
#include <mqueue.h>

#include "tlpi_hdr.h"
#include "tty_functions.h"

#include "SerialPacket.h"
#include "SerialDaemon.h"
#include "SerialLib8051.h"
#include "typedef.h"


/* Something required by RT Signals */
/* Supposedly already defined in /arm-linux-gneabihf/include/features.h */
//#define _POSIX_C_SOURCE 199309

#define DEBUG_VERBOSITY 100


/* keeps the main from being built, so that we can use the functions of the Serial Daemon */

#define DAEMON_TEST

/* Globals set by the signal handler that the application
 * can use to determine what action to compelete when it
 * receives signal */
static volatile sig_atomic_t gotSigio = 0, gotSigUsr1 = 0;

int
SemaphoreInitialization(void){
	/* Set permissions such that anyone can read or write the SEM*/
	mode_t perms;
	int flags = 0;
	sem_t *sem;
	perms = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

	/* Create named semaphore, if not available */
	flags |= O_CREAT;

	/* Use the PID as the value of the Semphore, something can see then see the Serial Daemon
	 * PID by checking the Sem Value */
	sem = sem_open(SERIAL_DAEMON_SEM, flags, perms, (unsigned int)getpid());
	if(sem == SEM_FAILED){

		#if DEBUG_LEVEL > 0
			errMsg("SemaphoreInitialization: Sem_Open failed");
		#endif
		return SEM_OPEN_FAIL;

	}

	return 1;

}


/* Grab data out of Tx Queue and write out to the 8051 File Descriptor */
static int
SerialTx(int ttyFd, const char *FileName)
{
	int32_t flags = 0;
	mqd_t mqd;

	uint32_t prio;
	struct mq_attr attr;

	int outputFd = 0, TotalTxBytes = 0, numRead = 0;


	char RxBuffer[MAX_RX_BUFF_SIZE];
	char ReadBuff[MAX_READ_BYTES];
	char FileOutputBuffer[FILE_OUT_BUFF_SIZE]={0};
	char * CurrentArrayPosition;
	struct timeval CurrentTime;
	char *CurrentTimeString;
	size_t count = 0;

	SerialPacket * CurrentSerialPacket=malloc(sizeof(SerialPacket));

	/* Open for Read, Create if not open, Open non-blocking-rcv and send will
		 * fail unless they can complete immediately. */
		flags = O_RDONLY | O_NONBLOCK;


	mqd=mq_open( SERIAL_TX_QUEUE , flags);

	/* check for fail condition, if we failed asssume we need to open a
	* message queue for the first time, then go ahead and do so
	* the function below creates a message queue if not already open*/
		if(mqd == (mqd_t) -1){
			mqd=Serial8051Open(SERIAL_TX_QUEUE);

			/* Return an error code now, .something bad is happening */
			if(mqd == (mqd_t) -1){
				#if DEBUG_LEVEL > 10
					errMsg("SerialDaemon TX: Message Open Failed");
				#endif
			return MSG_QUEUE_OPEN_FAIL;
			}

		}

		/* Get message attributes for allocating size */
		if(mq_getattr(mqd, &attr) == -1){
			errMsg("SerialDaemon TX:: Failed to get message attributes");
			return SERIAL_RECEIVE_MESSAGE_ATTR_FAIL;
		}

		ARM_char_t *ASCII_Buff = (ARM_char_t*) malloc(attr.mq_msgsize);

		numRead = mq_receive(mqd, ASCII_Buff, attr.mq_msgsize, &prio);

		RxMsgInfo* MessageInfo = (RxMsgInfo*) malloc(sizeof(RxMsgInfo));

		/* Figure out how many bytes are in this message so that we
		 * can know how many bytes to write. */
		ARM_char_t ProcessReturn=ProcessPacket(MessageInfo, ASCII_Buff );

		if(ProcessReturn < 0){
			#if DEBUG_LEVEL > 10
			printf("SerialDaemon Tx:: ProcessPacket Fails with error = %u", ProcessReturn);
			#endif
			return ProcessReturn;
		}

		count = (size_t)MessageInfo->MsgLength;

		/* Read buffered Serial data using the file descriptor until we
		   don't receive anymore (signaled by done flag) */
			TotalTxBytes=write(ttyFd, ASCII_Buff, count);

			if(TotalTxBytes < 0){

					#if DEBUG_LEVEL > 5
						errMsg("SerialDaemonTx: Couldn't write to File Descriptor");
					#endif
					return SERIAL_TX_WRITE_FAIL;

				}


			if(TotalTxBytes == 0){

					#if DEBUG_LEVEL > 5
						printf("SerialDaemonTx: Wrote Zero Bytes");
					#endif

					return SERIAL_TX_ZERO_BYTES;

				}




		/* Get Current System Time, copy it to our serial packet header */
			if (gettimeofday(&CurrentTime, NULL) == -1 ){

					#if DEBUG_LEVEL > 20
						printf("Couldn't get current time \n");
					#endif
					memset(&CurrentTime, 0x0, sizeof(CurrentTime));

			}
			else{
					CurrentTimeString = ctime(&CurrentTime.tv_sec);
					strcpy(CurrentSerialPacket->TimeReceived,CurrentTimeString);
			}


		//Copy data into text file, adding null byte and formatting appropriately
		sprintf(FileOutputBuffer, "%s \n %s \n", CurrentTimeString, ASCII_Buff);
		outputFd = open(FileName, O_RDWR| O_CREAT | O_APPEND,
					S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		write(outputFd, FileOutputBuffer,strlen(FileOutputBuffer));

		if(close(outputFd)==-1){
			#if DEBUG_LEVEL > 5
				errMsg("SerialDaemonTxclose output file failed");
			#endif
		}

		#if DEBUG_LEVEL > 5
			printf("SerialDaemonTx: New Complete Message Sent \n");
		#endif

			free(CurrentSerialPacket);
			free(MessageInfo);


			if(mq_close(mqd) < (int)0){
				errMsg("SerialDaemonTx: Close Failed");

			}


	return 1;
}


/* Receive incoming serial data from ttyFd and place in message queue and log file */
static int
SerialRx(int ttyFd, const char *FileName)
{

	int TotalRxBytes = 0, AccumulatedRxByteCount = 0 , SndMsgRtn = 0;
	int outputFd = 0;
	int flags = 0;
	mqd_t mqd;

	char RxBuffer[MAX_RX_BUFF_SIZE];
	char ReadBuff[MAX_READ_BYTES];
	char FileOutputBuffer[FILE_OUT_BUFF_SIZE]={0};
	char * CurrentArrayPosition;
	struct timeval CurrentTime;
	Boolean done = 0;
	char *CurrentTimeString;


	SerialPacket * CurrentSerialPacket=malloc(sizeof(SerialPacket));

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

			#if DEBUG_LEVEL > 20
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

			#if DEBUG_LEVEL > 10
				printf("\nRxBuffer %s ",RxBuffer);
			#endif

			AccumulatedRxByteCount+=TotalRxBytes;

			#if DEBUG_VERBOSITY > 15
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

	#if DEBUG_LEVEL > 5
		printf("New Complete Message Received \n");
	#endif

		/* Blast this message out on the MSG QUEUE */
		/* Open for Write, Create if not open, Open non-blocking-rcv and send will
		 * fail unless they can complete immediately. */
		flags = O_WRONLY | O_NONBLOCK;


		mqd=mq_open( SERIAL_RX_QUEUE , flags);

		/* check for fail condition, if we failed asssume we need to open a
		 * message queue for the first time, then go ahead and do so
		 * the function below creates a message queue if not already open*/
		if(mqd == (mqd_t) -1){
			mqd=Serial8051Open(SERIAL_RX_QUEUE);

			/* Return an error code now, something bad is happening */
			if(mqd == (mqd_t) -1){
				#if DEBUG_LEVEL > 10
					errMsg("SerialDaemonWrite: Message Open Failed");
				#endif
			return MSG_QUEUE_OPEN_FAIL;
			}

		}

		/* Send the Message  */
		SndMsgRtn = mq_send(mqd, ReadBuff, (size_t)(AccumulatedRxByteCount), 0);
		if(SndMsgRtn < 0){
			#if DEBUG_LEVEL > 10
				errMsg("SerialDaemonRx: Msg Send Fails");
			#endif
			return MSG_SEND_FAIL;
		}


		free(CurrentSerialPacket);

		if(mq_close(mqd) < (int)0){
			errMsg("SerialDaemonRx: Close Failed");

		}

		/* If we haven't bailed out anywhere else up here, go ahead and send a positive
		 * int as a return, meaning success */
			return 1;
}

static void
sigioHandler(int sig)
{
	if (sig == SIGIO)
		gotSigio = 1;


	if ( sig == SIGUSR1 )
		gotSigUsr1 = 1;

}

/* Eliminates the main if we just want to call functions from this file */
#ifndef DAEMON_TEST

int
main(int argc, char *argv[])
{

	struct termios OrigTermios, ModifiedTermios;
	struct timespec;
	struct sigevent sev;
	int flags = 0, Return = 0;
	int ttyFd;
	mqd_t mqd;

	//Used by sig handler to control process behavior when the signal arrives
	struct sigaction sa;

	/* Mask to block and restore signals prior to system calls */
	sigset_t blockSet, prevMask, emptyMask;

	/* COMPLETE ALL CONFIGUREATION OF SERIAL TERMINAL AND IO FILES before messing around with signals,
	 * because we don't want signals to interrupt any of this stuff */

	/* Create the semaphore for communication of PID with external Interface functions,
	 * so they can send a signal to this process usings it's PID */
	if(SemaphoreInitialization() == SEM_OPEN_FAIL){
		errExit("SerialDaemon: Failed  to Open PID semaphore, cannot communicate with SerialWrite Message Queues!!!");
	}

	/* Open Options Described in: https://www.cmrr.umn.edu/~strupp/serial.html
		The O_NOCTTY flag tells UNIX that this program doesn't want to
	    be the "controlling terminal" for that port. If you don't specify this then any
	    input (such as keyboard abort signals and so forth) will affect your process. */

	    ttyFd= open(SERIAL_FILEPATH, O_RDWR | O_NOCTTY | O_NDELAY );


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


	/* Create signal block set that we can use block signals
	 * during filesystem calls */
	sigemptyset(&blockSet);
	sigaddset(&blockSet, SIGIO);
	sigaddset(&blockSet, SIGUSR1);

	/* Block Signals while we are configuring them */
	if(sigprocmask(SIG_BLOCK, &blockSet, NULL)==-1){
		errExit("SerialDameon Main: sigprocmask");
	}

	/*Initialize  Signal Mask*/
	sigemptyset(&sa.sa_mask);

	/*Register Signal Handler */
	sa.sa_handler = sigioHandler;
	sa.sa_flags    = 0;

	/* Configure the sigevent struct, which is used to pass on information
	 * to the mqnotify call about how to notify this function */
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGUSR1;

	/* Open the message queue, and configure the notification for the
	 * write side (messages from SerialLib8051 write to this interface) */
	mqd = mq_open(SERIAL_TX_QUEUE ,O_RDONLY | O_NONBLOCK);
	if(mqd== (mqd_t) -1 )
		errExit("SerialDameon Main: mq_open");

	/* Everything is configured now, use sigaction to enable signal notification
	 * and interruption of the process */

	/* Attach SIGIO to this process (SIGIO sent by kernel
		 * when FD that process owns is written too */
		if (sigaction(SIGIO, &sa, NULL) == -1 )
			errExit("SerialDameon Main: SIGACTION");

		/* Register SIGURS1, which is raised by processes that
		 * has added data to the message queue to be transmitted out
		 * via serial */
		if (sigaction(SIGUSR1, &sa, NULL) == -1)
			errExit("SerialDameon Main: SIGUSR1");

	/* configure the notification to notify when message available in the
	 * write queue (messages from SerialLib8051 write to this interface) */
	if (mq_notify(mqd, &sev)==-1){
		errExit("SerialDameon Main: mq_notify");
	}

	/* Create an Empty Signal Mask that sigsuspend will use to
	 * mask / block signals during non-interuptible system calls
	 */
	sigemptyset(&emptyMask);


	for ( ;; ){
		/* Wait for signal, if we receive one, apply empty mask to block incoming signals.
		 * Complete tasks below uninterrupted, and once the loop restarts, call to same function
		 * activates signals again and waits for incoming message. */
		sigsuspend( &emptyMask );

		/* SIGO is sent when we have received data at our file descriptor */
		if(gotSigio ){

			/* Retrieves message from FD belong to the Serial Interface, places it in outgoing message
			 * queue that can be accessed by interface layer */
			Return = SerialRx(ttyFd, SERIAL_RX_LOG_FILENAME);

			if(Return<0){
				#if DEBUG_LEVEL > 10
					printf("Serial Daemon SerialRx fails with error code = %i \n", Return);
				#endif
			}

		}

		/* Sent when user places a message in the outgoing queue, via Serial8051Write */
		if(gotSigUsr1){

			Return = SerialTx(ttyFd, SERIAL_TX_LOG_FILENAME);

			#if DEBUG_LEVEL > 10
				printf("Serial Daemon SerialRx fails with error code = %i \n", Return);
			#endif

		}

	}
	/* Restore original terminal settings */
	if(tcsetattr(ttyFd, TCSAFLUSH, &OrigTermios)==-1)
		errExit(" Couldn't restore original terminal settings ");

	/* Close Serial Daemon Semaphore, which will make it very obvious that the Serial Daemon is not running */
	if(sem_unlink(SERIAL_DAEMON_SEM)==-1){
		errExit("SerialDaemon: Failed to close Semaphore");
	}

	#if DEBUG_LEVEL > 0
		printf("Exiting Serial Daemon \n");
	#endif

	exit(EXIT_SUCCESS);

}

#endif /*Daemon Test */
