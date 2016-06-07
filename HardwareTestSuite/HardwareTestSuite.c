#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/siginfo.h>
#include <sys/neutrino.h>
#include <sys/trace.h>		// to support TraceEvent calls
#include <sys/netmgr.h>
#include <sys/syspage.h>
#include <stdint.h>       /* for uintptr_t */
#include <hw/inout.h>     /* for in*() and out*() functions */
#include <sys/mman.h>     /* for mmap_device_io() */

#define A_D_PORT_LENGTH (1)
#define A_D_BASE_ADDRESS (0x280)
#define A_D_COMMAND_REGISTER (A_D_BASE_ADDRESS)
#define A_D_MSB_REGISTER (A_D_BASE_ADDRESS + 1)		// read access only
#define A_D_CHANNEL_REGISTER (A_D_BASE_ADDRESS + 2)
#define A_D_INPUT_GAIN_REGISTER (A_D_BASE_ADDRESS + 3)
#define A_D_INPUT_STATUS_REGISTER (A_D_INPUT_GAIN_REGISTER)
#define I_O_CONTROL_REGISTER (A_D_BASE_ADDRESS + 4)
#define A_OUT_LSB_REGISTER (A_D_BASE_ADDRESS + 6)
#define A_OUT_MSB_REGISTER (A_D_BASE_ADDRESS + 7)
// for Helios only
// Need to set up page 2 D/A output range with range and polarity.
// This is on Page 2 at offset 0xe (14.)
//
// This is the page register:
#define DATA_ACQ_PAGE_REGISTER ( A_D_MSB_REGISTER )	// write access only

#define D_I_O_PORT_LENGTH (1)
#define D_I_O_CONTROL_REGISTER (A_D_BASE_ADDRESS + 0x0b)
#define D_I_O_PORT_A (A_D_BASE_ADDRESS + 0x08)
#define D_I_O_PORT_B (A_D_BASE_ADDRESS + 0x09)
#define D_I_O_PORT_C (A_D_BASE_ADDRESS + 0x0a)

//Counter/Timer divisor registers
#define COUNTER_LSB (A_D_BASE_ADDRESS + 0x0d)
#define COUNTER_MSB (A_D_BASE_ADDRESS + 0x0e) //acts as the middle byte for counter 0
#define COUNTER_0_MSB (A_D_BASE_ADDRESS + 0x0f) //only works with counter 0, do not use with 1

//For Helios Only
//Used for setting AD mode configuration such as polarity, used on page 2

#define A_D_MODE_REGISTER (COUNTER_LSB)
#define D_A_MODE_CONFIG (COUNTER_MSB)

// make static as good programming practice (not in global symbol table)
static uintptr_t a_d_command_handle ;
static uintptr_t a_d_LSB_handle ;
static uintptr_t a_d_MSB_handle ;
static uintptr_t a_d_channel_handle ;
static uintptr_t a_d_input_status_handle ;	// also used once for analog input gain setting
static uintptr_t page_handle ; // for Helios only
static uintptr_t a_d_mode_handle ; // for Helios only
static uintptr_t d_a_mode_handle ; // for Helois only

static uintptr_t d_i_o_control_handle ;		// control register for ports A, B, and C
static uintptr_t d_i_o_port_a_handle ;
static uintptr_t d_i_o_port_b_handle ;
static uintptr_t d_i_o_port_c_handle ;

// analog output register handles
static uintptr_t a_out_value_MSB_handle ;	// bits 0 - 3 are 4 MSB bits. Bits 6 & 7 are channel
static uintptr_t a_out_value_LSB_handle ;	// write before write MSB

//Counter register handles
static uintptr_t counter_lsb_handle ;
static uintptr_t counter_msb_handle ;
static uintptr_t counter_0_msb_handle ;

// Control flag that tells software to adjust for helios board
static uint8_t helios_flag;

static void SetSingleAtoDchannel( int channelNumber )
{
	if ( channelNumber <= 15 && channelNumber >= 0 )
	{
		// configure to only use just one input channel
		out8( a_d_channel_handle, channelNumber | ( channelNumber << 4 ) ) ;
		while ( in8( a_d_input_status_handle ) & 0x20 ) ;	// wait for WAIT bit to go low
															//-- means channel mux is ready to use (takes 9 microseconds)
	}
}

static short MeasureVoltageOnChannel( int channelNumber )
{
	unsigned short value ;
	unsigned short lsb_value ;

	SetSingleAtoDchannel( channelNumber ) ;
	// start the capture of this channel
	out8( a_d_command_handle, 0x90 ) ;	// reset the FIFO and start the measurement
	while ( in8( a_d_input_status_handle ) & 0x80 ) ;		// wait for STS bit to go false meaning data is ready

	lsb_value = in8( a_d_LSB_handle ) ;
	value = in8( a_d_MSB_handle ) << 8 ;
	value |= lsb_value ;
	return (short)value ;
}

static void SetupDIO()
{
	d_i_o_control_handle = mmap_device_io( D_I_O_PORT_LENGTH, D_I_O_CONTROL_REGISTER ) ;
	d_i_o_port_a_handle = mmap_device_io( D_I_O_PORT_LENGTH, D_I_O_PORT_A ) ;
	d_i_o_port_b_handle = mmap_device_io( D_I_O_PORT_LENGTH, D_I_O_PORT_B ) ;
	d_i_o_port_c_handle = mmap_device_io( D_I_O_PORT_LENGTH, D_I_O_PORT_C ) ;
}

static void TestPorts()
{
	unsigned int testValue = 1 ;
	int count ;
	unsigned int portAvalue ;
	unsigned int portBvalue ;
	unsigned int portCvalue ;

	out8( d_i_o_control_handle, 0x02) ;		// make port B input

	// test output port A one bit at a time from low bit to high bit
	for ( count = 0 ; count < 8 ; count++, testValue <<= 1 )
	{
		out8( d_i_o_port_a_handle, testValue ) ;
		portBvalue = in8( d_i_o_port_b_handle ) ;
		if ( testValue != portBvalue )
			printf( "\nERROR on Port A Out to Port B In at bit %d -- got %u expected %u", count, portBvalue, testValue ) ;
	}

	out8( d_i_o_control_handle, 0x10) ;		// make port A input

	// test output port B one bit at a time from low bit to high bit
	testValue = 1 ;
	for ( count = 0 ; count < 8 ; count++, testValue <<= 1 )
	{
		out8( d_i_o_port_b_handle, testValue ) ;
		portAvalue = in8( d_i_o_port_a_handle ) ;
		if ( testValue != portAvalue )
			printf( "\nERROR on Port B Out to Port A In at bit %d -- got %u expected %u", count, portAvalue, testValue ) ;
	}
	if(helios_flag)
		out8( d_i_o_control_handle, 0x08) ;//For helios only, Make port C high input
	else
		out8( d_i_o_control_handle, 0x88) ; //make port C high input
	//test output of the low bits on port C one at a time
	testValue = 1;
	for(count = 0 ; count < 4 ; count++, testValue <<= 1)
	{
		out8( d_i_o_port_c_handle, testValue) ;
		portCvalue = in8(d_i_o_port_c_handle) ;
		if( !(( testValue << 4 )& portCvalue) )
			printf( "\nERROR on Port C low Out to Port C high In at bit %d -- got %u expected %u", count, portCvalue & 0xf0, testValue << 4);
	}
	if(helios_flag)
		out8( d_i_o_control_handle, 0x01) ; //For Helios only, make port C low input
	else
		out8( d_i_o_control_handle, 0x81) ; //make port C low input
	//test output of the high bits on port C one at a time
	testValue = 16 ;
	for(count = 0 ; count < 4 ; count++, testValue <<= 1)
	{
		out8( d_i_o_port_c_handle, testValue) ;
		portCvalue = in8(d_i_o_port_c_handle) ;
		if( !(( testValue >> 4 ) & portCvalue ) )
			printf( "\nERROR on Port C high Out to Port C low In at bit %d -- got %u expected %u", count+4, portCvalue & 0x0f, testValue >> 4) ;
	}

	printf( "\nDigital I O ports A, B, and C testing completed\n" ) ;
}

static void SetupAtoD()
{
	uintptr_t i_o_control_handle ;

	/* Get handles to the A to D registers */
	a_d_command_handle = mmap_device_io( A_D_PORT_LENGTH, A_D_COMMAND_REGISTER );
	a_d_LSB_handle = a_d_command_handle ;	// read on command port to get the A/D LSB
	a_d_MSB_handle = mmap_device_io( A_D_PORT_LENGTH, A_D_MSB_REGISTER );
	a_d_channel_handle = mmap_device_io( A_D_PORT_LENGTH, A_D_CHANNEL_REGISTER );
	a_d_input_status_handle = mmap_device_io( A_D_PORT_LENGTH, A_D_INPUT_GAIN_REGISTER );	// set to gain of 1
	i_o_control_handle = mmap_device_io( A_D_PORT_LENGTH, I_O_CONTROL_REGISTER ) ;			// only need for init
	if(helios_flag){
		a_d_mode_handle = mmap_device_io(A_D_PORT_LENGTH, A_D_MODE_REGISTER) ;
	}
	/* Initialize the A/D converter */
	out8( a_d_command_handle, 0x7f );		// clear everything but do not start a new A/D conversion


	out8( a_d_input_status_handle, 0 );		// set to 10 volt range and clear scan mode enable
	out8( i_o_control_handle, 0 ) ;			// set  AINTE to off for polling mode for trigger via A/D command register
}

static void SetupAout()
{
	/* Get handles to the D to A registers */
	a_out_value_MSB_handle = mmap_device_io( A_D_PORT_LENGTH, A_OUT_MSB_REGISTER );
	a_out_value_LSB_handle = mmap_device_io( A_D_PORT_LENGTH, A_OUT_LSB_REGISTER );
	if(helios_flag){
		d_a_mode_handle = mmap_device_io(A_D_PORT_LENGTH, D_A_MODE_CONFIG) ;
		out8(page_handle, 0x02) ;
		out8(d_a_mode_handle, 0x0a) ;
		out8(page_handle, 0x00) ;
	}
}

int GetRootAccess()
{
	int status = 0 ;
	int privity_err ;

	/* Give this thread root permissions to access the hardware */
	privity_err = ThreadCtl( _NTO_TCTL_IO, NULL );
	if ( privity_err == -1 )
	{
		fprintf( stderr, "can't get root permissions\n" );
		status = -1;
	}

	return status ;
}

void GenerateAout( int voltage, int output_channel )
{
	unsigned int lsb_value = 0 ;
	unsigned int msb_and_channel = 0 ;
	unsigned int msb_and_lsb = 0 ;
	double converted_voltage = ( 2048 * voltage ) / 10.0 ;	// assume output goes positive and negative

	msb_and_lsb = 2048 + (int) ( ( converted_voltage + 0.5 ) ) ;	// round by adding 1/2 and truncating.
	if ( msb_and_lsb > 4095 )
		msb_and_lsb = 4095 ;					// clip at largest possible value.
	lsb_value = msb_and_lsb & 0xff ;			// just get the low byte
	msb_and_channel = msb_and_lsb >> 8 ;		// just get the top 4 bits
	msb_and_channel |= ( output_channel << 6 ) ;	// move channel to the top two bits.
	printf( "MSB %02x  LSB %02x   ", msb_and_channel, lsb_value ) ;
	out8( a_out_value_LSB_handle, lsb_value ) ;
	out8( a_out_value_MSB_handle, msb_and_channel ) ;
}

//Function not yet implemented, Intended to test counter output and input and proper operation
void TestCounter(int divisor_value){

}

int main(int argc, char *argv[])
{
	int loop ;
	int aout_channel ;
	int voltage ;
	int entry;
    time_t time_of_day;
    time_t start_time ;

    start_time = time( NULL );
    int analog_input_channel_lookup_table[] =
			{0, 8, 1, 9} ;	// Vout 0 to Vin 0, Vout 1 to Vin 8
							// Vout 2 to Vin 1, Vout 3 to Vin 9

	//TraceEvent( _NTO_TRACESTART ) ;
	//TraceEvent( _NTO_TRACE_INSERTUSRSTREVENT, _NTO_TRACE_USERFIRST, "start test" ) ;
	if ( ! GetRootAccess() )
	{
		//Prompts user to determine if running on helios or athena
		do{
			printf("\nIs this a Helios board (Y/N): ") ;
			entry = getchar();
			getchar();
			entry = toupper(entry);
		}while(entry != 'Y' && entry != 'N');

		if(entry == 'Y'){
			helios_flag = 1;
			page_handle = mmap_device_io(A_D_PORT_LENGTH, DATA_ACQ_PAGE_REGISTER);
			printf("\nHelios Mode Enabled") ;
		}
		else{
			helios_flag = 0;
		}

		SetupAtoD() ;
		SetupDIO() ;
		SetupAout() ;

		printf( "\nInstall Aout / Ain test cable and press a key: " ) ;
		getchar() ;

		// Add additional loop for temporary testing
		//for ( loop = 0 ; loop < 10000 ; loop++ )
		for ( aout_channel = 0 ; aout_channel < 4 ; aout_channel++ )
			for ( voltage = -1 ; voltage <= 1 ; voltage++ )
			{
				GenerateAout( voltage, aout_channel ) ;
				sleep( 1 ) ;
				time_of_day = time( NULL ) ;
				printf( "Voltage %3d Channel %2d A/D in: %6d   %s", voltage, aout_channel,
						MeasureVoltageOnChannel( analog_input_channel_lookup_table[ aout_channel ] ),
						ctime( &time_of_day ) ) ;
			}

		printf( "\nInstall Ain and DIO cable and press a key: " ) ;
		getchar() ;
		printf( "\nStarting measurement on analog input line 0\n" ) ;
		for ( loop = 0 ; loop < 16 ; loop++ )
		{
			printf( "\n%6d", MeasureVoltageOnChannel( loop ) ) ;
		}
		printf( "\n\nStarting Port A, Port B, and Port C tests\n" ) ;
		TestPorts() ;
	}
	else
		printf( "\nFailure getting root access for I/O register mapping\n") ;

	//TraceEvent( _NTO_TRACE_INSERTUSRSTREVENT, _NTO_TRACE_USERFIRST, "stop test" ) ;
	//TraceEvent( _NTO_TRACESTOP ) ;
	return EXIT_SUCCESS;
}
