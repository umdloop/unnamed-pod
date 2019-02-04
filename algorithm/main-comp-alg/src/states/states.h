#ifndef __STATES__
/* This is where our state includes will live */
typedef enum {STANDBY_SID = 0, INITIALIZE_SID = 1, SERVICE_SID = 2, ACCELERATE_SID = 3, NORMBRAKE_SID = 4, ESTOP_SID = 5, NUM_STATES} State; 
typedef enum {SUCCESS = 0, REPEAT = 1, ERROR = 2, ESTOP = 3, NUM_CODES} State_Status; 

typedef struct {
	int stopping_distance;
	int threshold1_low;
	int threshold1_high;
	int threshold2_low;
	int threshold2_high;
	int test1;
	int test2;
	int test3;
	int test4;
} Thresholds;

int standby_state();
int initialize_state();
int service_state();
int accelerate_state();
int normbrake_state();
int estop_state();

//temporary thresholds, commands, and other values for transition logic
#define estop_command false
#define acknowledge_command true //braking acknowledgement
//available in standby state:
#define prelaunch_command true
#define enter_service_command false
//available in initialize state:
#define launch_command true
#define abort_launch_command false
//available in service state:
#define enter_standby_command false
#define start_service_propulsion_command false
#define stop_service_propulsion_command false
#define depressurize_command false
#define slow_service_propulsion_command false
#define medium_service_propulsion_command false
#define fast_service_propulsion_command false
#define forward_service_propulsion_command false
#define backward_service_propulsion_command false

#define min_distance_from_end 305 //meters
#define track_length 1000 
#define max_motor_temp 120 //celcius
#define max_battery_temp 60 //celcius


#define __STATES__
#endif

