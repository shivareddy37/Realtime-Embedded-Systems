/* simple.c - a sample program to use as an introduction to QNX and Momentics */

/* $Id: simple.c,v 1.1 2007-03-12 05:47:46 se463 Exp $ */

/*Authors: Quentin Goyert, Justin Cotner*/

/* includes */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <semaphore.h>
#include <pthread.h>
#include <sys/trace.h>
#include "instrex.h"

#define DELAY_USEC  10000

/* run states, provides for shutdown in stages; ensures that no
   routine tries to take a semaphore that no longer exists */
#define ALL_GO 		0
#define SHUTDOWN 	1
#define GATEKEEPER_STOP 2
#define ALL_STOP        3

/* globals */
pthread_t tidGatekeeper;
pthread_t tidSimple;

int runState;                   /* running state of the system */
int count;                     /* track number of time simple runs */

sem_t syncSemId;				/* semaphore that controls when simple can run */

/* forward declarations */
void* gatekeeper(void*);
void* simple(void*);

/*************************************************************************
*
* returnCheck - a common routine for checking the return value of a POSIX call
*
* Parameters: int retValue - value returned by the POSIX call
*             bool exitOnError - true if the program should exit on an error
*             int exitValue - the exit value to use if terminating the program
*             char* message - message to display if an error was returned
* RETURNS: the return value from the call
*/
int returnCheck(int retValue, bool exitOnError, int exitValue, char* message) {
	if(retValue == -1) {
		perror(message);
		if(exitOnError) {
			exit(exitValue);
		}
	}

	return(retValue);
}

/*************************************************************************
*
* progStart - start the simple program.
*
* RETURNS: OK
*/

int main(int argc, char* argv[])
{
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_DELALLCLASSES));
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_CLRCLASSPID, _NTO_TRACE_KERCALL));
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_CLRCLASSTID, _NTO_TRACE_KERCALL));
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_CLRCLASSPID, _NTO_TRACE_THREAD));
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_CLRCLASSTID, _NTO_TRACE_THREAD));

	returnCheck(pthread_setname_np(pthread_self() ,"main"), true, 1, "setting main thread name");
    returnCheck(sem_init(&syncSemId, 0, 0), true, 1, "syncSem init failed");

    /*
     * Set fast emitting mode for all classes and
     * their events.
     */
    TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_SETALLCLASSESFAST));

    /*
     * Intercept all event classes
   	 */
    TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_ADDALLCLASSES));

    /* get started */
    runState = ALL_GO;

    //TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_START));


	/* spawn the gatekeeper and simple threads */
    if(pthread_create(&tidGatekeeper, NULL, gatekeeper, NULL) != 0) {
    	fprintf(stderr, "tGatekeeper pthread_create failed\n");
    	exit(2);
    }
	returnCheck(pthread_setname_np(tidGatekeeper, "gatekeeper"), true, 1, "setting gatekeeper thread name");

    if(pthread_create(&tidSimple, NULL, simple, NULL) != 0) {
    	fprintf(stderr, "tSimple pthread_create failed\n");
    	exit(2);
    }
    returnCheck(pthread_setname_np(tidSimple, "simple"), true, 1, "setting simple thread name");

	printf("Gatekeeper and simple are started.\n");

	/* let the threads run for 20 seconds, and then request the shutdown */
	sleep(20);
	runState = SHUTDOWN;

    /* Wait for everyone to finish up */
    while (runState != ALL_STOP)
	sleep(1);

    /* clean up, and output the run count */
    returnCheck(sem_destroy(&syncSemId), true, 8, "sem_destroy failed");
    printf ("Simple executed %d times.\n",count);

    return (0);
}

/*************************************************************************
*
* gatekeeper - routine that supplies the semaphore simple task waits on
*
*/

void* gatekeeper (void* dummy)
{
    while (runState == ALL_GO)
    {
        returnCheck(sem_post(&syncSemId), true, 3, "sem_post failed");
        returnCheck(usleep(DELAY_USEC + (100 * ((rand() & 0x0f) - 8))), true, 4,
        	                                                               "usleep failed");
    }

    runState = GATEKEEPER_STOP;
    returnCheck(sem_post(&syncSemId), true, 5, "gatekeeper terminating sem_post failed");

    return NULL;
}

/*************************************************************************
*
* simple - consume the semaphore and do not do much else
*
*/

void* simple (void* dummy)
{
    count = 0;

    while (runState != GATEKEEPER_STOP)
    {
    	TraceEvent(_NTO_TRACE_INSERTUSRSTREVENT, 111,"Simple Started\n" );
        returnCheck(sem_wait(&syncSemId), true, 6, "sem_wait failed");
        count++;
        TraceEvent(_NTO_TRACE_INSERTUSRSTREVENT, 222, "Simple  Ended\n");
    }

    runState = ALL_STOP;
    return NULL;
}
