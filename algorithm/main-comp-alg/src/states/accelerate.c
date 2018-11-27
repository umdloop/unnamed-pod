#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h>
#include "./states/states.h"

int accelerate_state() {
	active = 1;
	last_state = ACCELERATE_SID;
	printf("ACCELERATING: %d\n", testaccel_counter);

	while (testaccel_counter < 10) {
		testaccel_counter += 1;
		return CONTINUE;
	}
	return SUCCESS;
}