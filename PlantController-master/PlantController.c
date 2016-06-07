/*
 * Program sets a desired output, then enters 0 volts into the plant, from there it checks plant output vs desired and calculates error, it then performs math as outlined in article, sets new input based on this math
 * and gets resulting output, calculating error again, user should be able to set hard variables to control this better. Function works when it can reduce error. See articles by wescott
 * Integral must take sum of previous errors and * coef, limiting itself to a max and min. Proportional is jsut an error * coef. Differential is current-previous position, * coef
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/neutrino.h>
#include <sys/mman.h>
#include <sys/netmgr.h>
#include <hw/inout.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>

#define BASE 0x280

struct control{
	float period;
	float integral;
	float derivative;
	float error;
	float iMax;
	float iMin;
	float iState;
	float goal;
};

struct control PID;

pthread_mutex_t mutex;
pthread_t uInput;

void SetUp(){
	PID.period = 1.0;
	PID.integral = 0.099;
	PID.derivative = 0.095;
	PID.iState=0;
	PID.iMin= -100.0;
	PID.iMax = 100.0;
	PID.goal= 5.0;
	PID.error=0.0;

	pthread_mutex_init(&mutex, NULL);

}


/*
 * InitializeAD
 * Initializes the A/D converter for use
 */
void InitializeAD(){
	uintptr_t gain_handle; //controls the gain
	uintptr_t chan_handle; //controls which channels
	gain_handle = mmap_device_io(1,BASE+3);
	chan_handle = mmap_device_io(1,BASE+2);
	out8(chan_handle,0x11); //sets channel 1 as A/D input
	out8(gain_handle,0x0); //Sets gain as +-10V
}

/* Start
 * Starts an A/D conversion
 */
void start(){
	uintptr_t write_handle; //the address that starts the conversion
	write_handle = mmap_device_io(1,BASE);
	out8(write_handle,0x80); //writes a high bit to tell hardware to perform conversion
}


 /* Convert
 * Performs an A/D Conversion
 */
int16_t convertAD(){
	uintptr_t status_handle; //maintains the status of the converter
	uintptr_t least_handle; //Least significant bit of the conversion
	uintptr_t most_handle; //most significant bit of the conversion
	int LSB;
	int MSB;
	status_handle = mmap_device_io(1,BASE+3); //Status register of A/D converter
	least_handle = mmap_device_io(1,BASE); //Register where LSB is held
	most_handle = mmap_device_io(1,BASE+1);
	start();
	while(in8(status_handle)& 0x80){ //waits for status bit to change and conversion to finish

	}
	LSB = in8(least_handle);
	MSB = in8(most_handle);

	return MSB*256+LSB;
}

void convertDA(int16_t input){
	uintptr_t lsb_handle;
	uintptr_t msb_handle;
	uintptr_t busy_handle;
	lsb_handle= mmap_device_io(1,BASE+6);
	msb_handle =mmap_device_io(1,BASE+7);
	busy_handle = mmap_device_io(1,BASE+3);


	int8_t lsb = input&255;
	int8_t msb=input/256; //add value to use channel other then 0
	out8(lsb_handle,lsb);
	out8(msb_handle,msb);

	while(in8(busy_handle)&0x10){

	}


}

float compute(float error){

	float pTerm, iTerm, dTerm;

	pthread_mutex_lock(&mutex);
	pTerm = PID.period * error;

	PID.iState+=error;
	if(PID.iState > PID.iMax){
		PID.iState=PID.iMax;
	}
	if(PID.iState < PID.iMin){
		PID.iState=PID.iMin;
	}

	iTerm= PID.integral * PID.iState;

	dTerm = PID.derivative +(PID.error-error);
	PID.error=error;
	pthread_mutex_unlock(&mutex);

		return pTerm+iTerm+dTerm;

}


void performMath(){
	int16_t analog = 0;
	uint16_t convertOut = 0;
	float convertAnalog = 0;
	float out = 0.0;
	float error = 0.0;

	analog = convertAD();
	convertAnalog = ((float) analog)/32768 * 10;
	pthread_mutex_lock(&mutex);
	error = PID.goal - convertAnalog;
	pthread_mutex_unlock(&mutex);
	out = compute(error);
	convertOut = (int)(((out/20)*2048)+2048);

	if(convertOut > 4095)
		 convertOut = 4095;
	else if(convertOut < 0)
		convertOut = 0;

	convertDA(convertOut);
	//printf("Analog: %d\n",analog);
	//printf("Error: %d\n",error);
	//printf("Out: %d\n",out);
}


void * userControl(void * args){
	float per;
	float inte;
	float der;

	while(1){
		pthread_mutex_lock(&mutex);
		per = PID.period;
		inte=PID.integral;
		der=PID.derivative;
		pthread_mutex_unlock(&mutex);
		printf("Current Values- Period: %f, Integral %f, Derivative %f\n",per, inte, der);
		printf("Enter Period \n");
		scanf("%f",&per);

		printf("Enter Integral \n");
		scanf("%f",&inte);

		printf("Enter Derivative \n");
		scanf("%f", &der);

		pthread_mutex_lock(&mutex);
		PID.period = per;
		PID.integral = inte;
		PID.derivative = der;
		pthread_mutex_unlock(&mutex);
	}
}

int main(int argc, char *argv[]) {


	int pid;
	int chid;
	int pulse_id = 0;
	struct _pulse pulse;
	struct _clockperiod clkper;
	struct sigevent event;
	struct itimerspec timer;
	timer_t timer_id;
	int privity_err;

	pthread_attr_t threadAttributes;
	struct sched_param parameters;
	int policy;
	pthread_attr_init(&threadAttributes);
	pthread_getschedparam(pthread_self(),&policy, &parameters);

	parameters.sched_priority--;
	pthread_attr_setdetachstate(&threadAttributes, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setschedparam(&threadAttributes, &parameters);

	privity_err = ThreadCtl( _NTO_TCTL_IO, NULL );
	if ( privity_err == -1 )
	{
		fprintf( stderr, "can't get root permissions\n" );
		return -1;
	}

	/*Real Time Clock Setup*/
	clkper.nsec = 10000000;
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
	timer.it_value.tv_nsec = 10000000;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 10000000;

	timer_settime( timer_id, 0, &timer, NULL );
	SetUp();
	InitializeAD();

	pthread_create(&uInput, &threadAttributes, &userControl, NULL);

	while(1){
		pid = MsgReceivePulse ( chid, &pulse, sizeof( pulse ), NULL );
		performMath();
	}

	return EXIT_SUCCESS;
}
