/*Scheduling Project for Performance Engineering of Real-Time and Embedded Systems
 *
 * Authors: Quentin Goyert,Justin Cotner
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/trace.h>
#include <sys/neutrino.h>
#include <time.h>
#include <sys/netmgr.h>
#include <errno.h>
#include "instrex.h"


#define STATE_IDLE 0
#define STATE_READY 1
#define STATE_RUNNING 2

static pthread_t scheduleThreadID;
int schedulerType;
int failures;
typedef struct{
		int runTime;
		int period;
		int deadline; // Date time that it needs to be done by.
		pthread_t threadID;
		//pthread_mutex_t mutex;
		pthread_attr_t threadAttr;
		int state;
		//The next three are necessary for doing the Least Slack Time algorithm.
		int runTimeLeft;
		int slackTime;
		int timeSinceReady;
		int runAmount;
} ProgramInfo;

ProgramInfo * programsArray;
static int numPrograms = 0;
pthread_cond_t control;

int pid;
int chid;
struct _pulse pulse;
int pulse_id = 0;


void * program(void * arg){
	int location = (int)&arg[0];
	//ProgramInfo * program = (ProgramInfo *)arg;
	int time = programsArray[location].runTime;

		//pthread_mutex_lock(&(programsArray[location].mutex));
		//if(programsArray[location].state == STATE_READY){
	TraceEvent(_NTO_TRACE_INSERTUSRSTREVENT, location,"Program Started\n" );
			programsArray[location].state = STATE_RUNNING;
			nanospin_ns(time * 1000000);

			programsArray[location].state = STATE_IDLE;
			programsArray[location].runAmount++;
	//}
}

void * rateMonotonicScheduler(void * arg){

	//int progNumbers = (int)&arg[0];
	//ProgramInfo * programsArray = (ProgramInfo *)arg;
	//pthread_attr_t progThreadAttributes;
	struct sched_param progParameters;
	struct _clockperiod clkper;
	struct sigevent event;
	struct itimerspec timer;
	timer_t timer_id;
	int policy;
	int loopCounter;
	char str[10];
	int repeat;

	//pthread_attr_init(&progThreadAttributes);
	pthread_getschedparam(pthread_self(),&policy, &progParameters);

	ProgramInfo buffer;
	int back;
	for(loopCounter = 1; loopCounter < numPrograms; loopCounter++){
		buffer= programsArray[loopCounter];
		back = loopCounter-1;
		while(back >= 0 && (programsArray[loopCounter].period < buffer.period)){
			programsArray[back + 1] = programsArray[back];
			back--;
		}
		programsArray[back + 1] = buffer;
	}

	for(loopCounter = 0; loopCounter < numPrograms; loopCounter++){
		progParameters.sched_priority--;

		pthread_attr_setschedparam(&(programsArray[loopCounter].threadAttr), &progParameters);
		if(pthread_create(&(programsArray[loopCounter].threadID), &(programsArray[loopCounter].threadAttr), &program, (void *)loopCounter)!=EOK){
			printf("IT BROKE WHEN FIRST STARTED YO!\n");
			exit(0);
		}
		sprintf(str,"Task %i",loopCounter);
		pthread_setname_np(programsArray[loopCounter].threadID,str);
	}


	/*Real Time Clock Setup*/
	clkper.nsec = 1000000;
	clkper.fract = 0;

	ClockPeriod(CLOCK_REALTIME, &clkper, NULL, 0);
	chid = ChannelCreate(0);
	assert(chid != -1);

	/*Event creation and Set up*/
	event.sigev_notify = SIGEV_PULSE;
	event.sigev_coid = ConnectAttach(ND_LOCAL_NODE,0,chid,0,0);
	event.sigev_priority = getprio(0);
	event.sigev_code = 1023;
	event.sigev_value.sival_ptr = (void *)pulse_id;

	/*Timer Set up and Creation*/
	timer_create( CLOCK_REALTIME, &event, &timer_id );

	timer.it_value.tv_sec = 0;
	timer.it_value.tv_nsec = 1000000;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 1000000;

	timer_settime( timer_id, 0, &timer, NULL );

	for(repeat = 0;repeat < 10000;repeat++){
		pid= MsgReceivePulse(chid, &pulse, sizeof(pulse),NULL);
		for(loopCounter = 0; loopCounter < numPrograms; loopCounter++){
			programsArray[loopCounter].deadline--;

			if(programsArray[loopCounter].deadline == 0){
				if(programsArray[loopCounter].state != STATE_IDLE){
					//perror("Program Failure Detected\n");
					//printf("Program %i Failed to meet Deadline\n",loopCounter);
					//exit(0);
					failures++;
					pthread_cancel(programsArray[loopCounter].threadID);
				}

				//if(programsArray[loopCounter].state == STATE_IDLE){
					programsArray[loopCounter].deadline = programsArray[loopCounter].period;
					programsArray[loopCounter].state = STATE_READY;
					//pthread_mutex_unlock(&(programsArray[loopCounter].mutex));
					if(pthread_create(&programsArray[loopCounter].threadID,&(programsArray[loopCounter].threadAttr),&program,(void *)loopCounter)!=EOK){
						printf("It broke in the overhead part\n");
						exit(0);
					}
					sprintf(str,"Task %i",loopCounter);
					pthread_setname_np(programsArray[loopCounter].threadID,str);
				//}
			}
		}
	}

	//printf("If you reached here, the scheduler stopped running\n");
}

void * earliestDeadlineScheduler(void * arg){
	pthread_attr_t progThreadAttributes;
	struct sched_param progParameters;
	int policy;
	struct _clockperiod clkper;
	struct sigevent event;
	struct itimerspec timer;
	timer_t timer_id;

	pthread_attr_init(&progThreadAttributes);
	pthread_getschedparam(pthread_self(),&policy, &progParameters);

	int shortest = 500;
	int nextShortest = 500;
	int found = 1;
	int threadNum=0;
	int repeat;

	int loopCounter;
 	ProgramInfo buffer;
	int back;
	for(loopCounter = 1; loopCounter < numPrograms; loopCounter++){
		buffer = programsArray[loopCounter];
		back = loopCounter - 1;
		while(back >= 0 && (programsArray[loopCounter].deadline < buffer.deadline)){
			programsArray[back+1] = programsArray[back];
			back--;
		}
		programsArray[back + 1] = buffer;

	}

	for(loopCounter = 0; loopCounter < numPrograms; loopCounter++){
		progParameters.sched_priority--;

		pthread_attr_setschedparam(&progThreadAttributes, &progParameters);
		pthread_create(&(programsArray[loopCounter].threadID), &progThreadAttributes, &program, (void *)loopCounter);
	}

	/*Real Time Clock Setup*/
	clkper.nsec = 1000000;
	clkper.fract = 0;

	ClockPeriod(CLOCK_REALTIME, &clkper, NULL, 0);
	chid = ChannelCreate(0);
	assert(chid != -1);

	/*Event creation and Set up*/
	event.sigev_notify = SIGEV_PULSE;
	event.sigev_coid = ConnectAttach(ND_LOCAL_NODE,0,chid,0,0);
	event.sigev_priority = getprio(0);
	event.sigev_code = 1023;
	event.sigev_value.sival_ptr = (void *)pulse_id;

	/*Timer Set up and Creation*/
	timer_create( CLOCK_REALTIME, &event, &timer_id );

	timer.it_value.tv_sec = 0;
	timer.it_value.tv_nsec = 1000000;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 1000000;

	timer_settime( timer_id, 0, &timer, NULL );

	for(repeat = 0;repeat < 10000;repeat++){
		pid= MsgReceivePulse(chid, &pulse, sizeof(pulse),NULL);
		for(loopCounter = 0; loopCounter < numPrograms; loopCounter++){
			programsArray[loopCounter].deadline--;

			if(programsArray[loopCounter].deadline == 0){
				if(programsArray[loopCounter].state != STATE_IDLE){
					//perror("Program Failure Detected\n");
					//printf("Program %i Failed to meet Deadline\n",loopCounter);
					//exit(0);
					failures++;
					pthread_cancel(programsArray[loopCounter].threadID);
				}

				//if(programsArray[loopCounter].state == STATE_IDLE){
					programsArray[loopCounter].deadline = programsArray[loopCounter].period;
					programsArray[loopCounter].state = STATE_READY;
					if(pthread_create(&programsArray[loopCounter].threadID,&(programsArray[loopCounter].threadAttr),&program,(void *)loopCounter)!=EOK){
											printf("It broke in the overhead part\n");
											exit(0);
					//}
					//pthread_mutex_unlock(&(programsArray[loopCounter].mutex));
				}
			}
		}




		pthread_getschedparam(pthread_self(),&policy, &progParameters);

		for(loopCounter = 0; loopCounter < numPrograms; loopCounter++){
			if((programsArray[loopCounter].state == STATE_RUNNING || programsArray[loopCounter].state == STATE_READY) && programsArray[loopCounter].deadline < shortest){
				shortest = programsArray[loopCounter].deadline;
				threadNum = loopCounter;
			}
		}
		pthread_setschedprio(programsArray[threadNum].threadID, progParameters.sched_priority--);
		loopCounter = 0;
		while(found){
			found = 0;
			for(loopCounter = 0; loopCounter < numPrograms; loopCounter++){
				if(programsArray[loopCounter].state == STATE_RUNNING || programsArray[loopCounter].state == STATE_READY){
					if(programsArray[loopCounter].deadline < nextShortest && programsArray[loopCounter].deadline > shortest){
						nextShortest = programsArray[loopCounter].deadline;
						found = 1;
						threadNum = loopCounter;
					}
				}

				else{
					// Make sure idle tasks (probably) won't run
					pthread_setschedprio(programsArray[threadNum].threadID, 1);
				}
			}
			if(found){
				pthread_setschedprio(programsArray[threadNum].threadID, progParameters.sched_priority--);
			}
		}
	}
}

void * leastSlackTime(void * arg){
	pthread_attr_t progThreadAttributes;
	struct sched_param progParameters;
	int policy;
	struct _clockperiod clkper;
	struct sigevent event;
	struct itimerspec timer;
	timer_t timer_id;

	pthread_attr_init(&progThreadAttributes);
	pthread_getschedparam(pthread_self(),&policy, &progParameters);

	int shortest = 500;
	int nextShortest = 500;
	int found = 1;
	int threadNum=0;
	int repeat;

	int loopCounter;
	ProgramInfo buffer;
	int back;
	for(loopCounter = 1; loopCounter < numPrograms; loopCounter++){
		buffer = programsArray[loopCounter];
		back = loopCounter - 1;
		while(back >= 0 && (programsArray[loopCounter].slackTime < buffer.slackTime)){
			programsArray[back + 1] = programsArray[back];
			back--;
		}
		programsArray[back + 1] = buffer;
	}

	for(loopCounter = 0; loopCounter < numPrograms; loopCounter++){
		progParameters.sched_priority--;

		pthread_attr_setschedparam(&progThreadAttributes, &progParameters);
		pthread_create(&(programsArray[loopCounter].threadID), &progThreadAttributes, &program, (void *)loopCounter);
	}

	/*Real Time Clock Setup*/
	clkper.nsec = 1000000;
	clkper.fract = 0;

	ClockPeriod(CLOCK_REALTIME, &clkper, NULL, 0);
	chid = ChannelCreate(0);
	assert(chid != -1);

	/*Event creation and Set up*/
	event.sigev_notify = SIGEV_PULSE;
	event.sigev_coid = ConnectAttach(ND_LOCAL_NODE,0,chid,0,0);
	event.sigev_priority = getprio(0);
	event.sigev_code = 1023;
	event.sigev_value.sival_ptr = (void *)pulse_id;

	/*Timer Set up and Creation*/
	timer_create( CLOCK_REALTIME, &event, &timer_id );

	timer.it_value.tv_sec = 0;
	timer.it_value.tv_nsec = 1000000;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 1000000;

	timer_settime( timer_id, 0, &timer, NULL );

	for(repeat = 0;repeat < 10000;repeat++){
		pid= MsgReceivePulse(chid, &pulse, sizeof(pulse),NULL);
		for(loopCounter = 0; loopCounter < numPrograms; loopCounter++){
			programsArray[loopCounter].deadline--;
			programsArray[loopCounter].timeSinceReady++;

			if(programsArray[loopCounter].state == STATE_RUNNING){
				programsArray[loopCounter].runTimeLeft--;
			}

			if(programsArray[loopCounter].deadline == 0){
				if(programsArray[loopCounter].state != STATE_IDLE){
					//perror("Program Failure Detected\n");
					//printf("Program %i Failed to meet Deadline\n",loopCounter);
					//exit(0);
					failures++;
					pthread_cancel(programsArray[loopCounter].threadID);
				}

				//if(programsArray[loopCounter].state == STATE_IDLE){
					programsArray[loopCounter].deadline = programsArray[loopCounter].period;
					programsArray[loopCounter].runTimeLeft = programsArray[loopCounter].runTime;
					programsArray[loopCounter].timeSinceReady = 0;
					programsArray[loopCounter].state = STATE_READY;
					if(pthread_create(&programsArray[loopCounter].threadID,&(programsArray[loopCounter].threadAttr),&program,(void *)loopCounter)!=EOK){
											printf("It broke in the overhead part\n");
											exit(0);}
					//pthread_mutex_unlock(&(programsArray[loopCounter].mutex));
				//}
			}
			programsArray[loopCounter].slackTime = programsArray[loopCounter].deadline - programsArray[loopCounter].runTimeLeft;
		}



		pthread_getschedparam(pthread_self(),&policy, &progParameters);

		for(loopCounter = 0; loopCounter < numPrograms; loopCounter++){
			if((programsArray[loopCounter].state == STATE_RUNNING || programsArray[loopCounter].state == STATE_READY) && programsArray[loopCounter].slackTime < shortest){
				shortest = programsArray[loopCounter].slackTime;
				threadNum = loopCounter;
			}
		}
		pthread_setschedprio(programsArray[threadNum].threadID, progParameters.sched_priority--);
		loopCounter = 0;
		while(found){
			found = 0;
			for(loopCounter = 0; loopCounter < numPrograms; loopCounter++){
				if(programsArray[loopCounter].state == STATE_RUNNING || programsArray[loopCounter].state == STATE_READY){
					if(programsArray[loopCounter].slackTime < nextShortest && programsArray[loopCounter].slackTime > shortest){
						nextShortest = programsArray[loopCounter].slackTime;
						found = 1;
						threadNum = loopCounter;
					}
				}

				else{
					// Make sure that idle tasks (probably) won't run.
					pthread_setschedprio(programsArray[threadNum].threadID, 1);
				}
			}
			if(found){
				pthread_setschedprio(programsArray[threadNum].threadID, progParameters.sched_priority--);
			}
		}
	}
}

void ReadData(){
	char buffer[11];
	printf("Scheduler Type? (0 for Rate-Monotonic, 1 for Earliest Deadline, 2 for Least Slack Time)\n");
	fgets(buffer,sizeof(buffer),stdin);
	schedulerType=atoi(buffer);
	printf("Number of Programs?\n");
	fgets(buffer,sizeof(buffer),stdin);
	numPrograms = atoi(buffer);
	programsArray = (ProgramInfo *)malloc(numPrograms*sizeof(ProgramInfo));
	int i;
	for(i = 0; i < numPrograms; i++){
		printf("Runtime of Program %i?\n", i);
		fgets(buffer, sizeof(buffer), stdin);
		programsArray[i].runTime = atoi(buffer);

		printf("Period of Program %i?\n",i);
		fgets(buffer, sizeof(buffer), stdin);
		programsArray[i].period = atoi(buffer);

		printf("Deadline of Program %i?\n",i);
		fgets(buffer, sizeof(buffer), stdin);
		programsArray[i].deadline = atoi(buffer);

		if(programsArray[i].period != programsArray[i].deadline){
			perror("Deadline Not Equal to Period!\n");
			exit(EXIT_FAILURE);
		}

		programsArray[i].state = STATE_READY;
		programsArray[i].runTimeLeft = programsArray[i].runTime;
		programsArray[i].slackTime = programsArray[i].deadline - programsArray[i].runTime;
		programsArray[i].timeSinceReady = 0;
		programsArray[i].runAmount=0;

		pthread_attr_init(&(programsArray[i].threadAttr));
	}

}

int main(int argc, char *argv[]) {

	int loop;
	pthread_attr_t threadAttributes;
	struct sched_param parameters;
	int policy;
	pthread_attr_init(&threadAttributes);
	pthread_getschedparam(pthread_self(),&policy, &parameters);

	ThreadCtl(_NTO_TCTL_IO, NULL);

	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_DELALLCLASSES));
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_CLRCLASSPID, _NTO_TRACE_KERCALL));
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_CLRCLASSTID, _NTO_TRACE_KERCALL));
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_CLRCLASSPID, _NTO_TRACE_THREAD));
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_CLRCLASSTID, _NTO_TRACE_THREAD));

	/*
	     * Set fast emitting mode for all classes and
	     * their events.
	     */
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_SETALLCLASSESFAST));

	    /*
	     * Intercept all event classes
	   	 */
	TRACE_EVENT(argv[0], TraceEvent(_NTO_TRACE_ADDALLCLASSES));

	parameters.sched_priority--;
	pthread_attr_setdetachstate(&threadAttributes, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setschedparam(&threadAttributes, &parameters);

	failures = 0;

	nanospin_calibrate(0);

	ReadData();

	if(schedulerType == 1){
		pthread_create(&scheduleThreadID, &threadAttributes, &earliestDeadlineScheduler, (void *)1);
	}else if(schedulerType == 2){
		pthread_create(&scheduleThreadID, &threadAttributes, &leastSlackTime, (void *)1);
	}else
	{
		if(pthread_create(&scheduleThreadID, &threadAttributes, &rateMonotonicScheduler, (void *)1)!=EOK){
			printf("Scheduler not created!\n");
			exit(0);
		}
	}
	pthread_join(scheduleThreadID, NULL);
	printf("Number of Failed Deadlines %i\n",failures);
	for(loop = 0; loop < numPrograms;loop++){
		printf("Program %i Ran %i Times\n",loop,programsArray[loop].runAmount);
	}
	printf("Ending program\n");
	return EXIT_SUCCESS;
}


