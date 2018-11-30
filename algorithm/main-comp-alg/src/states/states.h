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
} Thresholds;

int standby_state();
int initialize_state();
int service_state();
int accelerate_state();
int normbrake_state();
int estop_state();

#define __STATES__
#endif

