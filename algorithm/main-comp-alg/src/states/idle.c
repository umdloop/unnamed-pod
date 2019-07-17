#ifndef __STANDBY__
#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h>
#include "states.h"

#define __STANDBY__
#endif

int idle_state(Fpga *fpga, Thresholds *thresholds, int command) { //keeps high power on, but forbids movement
	printf(">> IDLE STATE - Command: %d\n", command);

	//(H8) E-STOP
	if (command == EMERGENCY_BRAKE) {
		return ESTOP_SID;
	}
	
	fpgaRunAndUpdateIf(fpga, write_actuate_brakes(fpga, NiFpga_True), "actuate brakes");
	//TODO: Motor Idle

	//(H6) Discharge
	if (command == DISCHARGE) {
		if (fpga->cache.brake_state == NiFpga_False) {

		}
		
		printf("Command received - Entering standby\n");
		return STANDBY_SID;
	}

	return IDLE_SID;
}

