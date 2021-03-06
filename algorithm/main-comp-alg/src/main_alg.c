#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "sensors.h"
#include "states.h"
#include "commands.h"
#include "spacex.h"
#include "abort_run.h"
#include "udp.h"
#include "priority.h"
#include "fpga_cache.h"
#include "run_data.h"
#include "can_init.h"


typedef enum {STOPPING_DISTANCE, THRESHOLD1_LOW, THRESHOLD1_HIGH, THRESHOLD2_LOW, THRESHOLD2_HIGH, TEST1, TEST2, TEST3, TEST4} Config;

int g_abort_run = 0;
int g_shutoff = 0;

UMData run_data;

int main() {
    //////////////////////////////////////////////////////////////////////////////////////////////////
    // INIT FPGA                                                                                    //
    //////////////////////////////////////////////////////////////////////////////////////////////////
    Fpga fpga_dat; // FPGA metadata stored inside the struct

    Fpga *fpga = &fpga_dat;
    default_fpga(fpga);
    fpgaRunAndUpdateIf(fpga, init_fpga(fpga, 0), "Load NiFpga library and deploy bitfile");
    if (NiFpga_IsError(fpga->status)) {
        printf("Failed to initialize FPGA, status code %d, exiting\n", fpga->status);
        return 5;
    }

    fpgaRunAndUpdateIf(fpga, run_fpga(fpga, 0), "Run FPGA library");
    if (NiFpga_IsError(fpga->status)) {
        printf("Warning during fpga run, status code %d, exiting\n", fpga->status);
        return 5;
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////
    // INIT REAL-TIME SCHEDULING                                                                    //
    //////////////////////////////////////////////////////////////////////////////////////////////////
    const struct sched_param priority = {STATE_MACHINE_PRIO};
    
    /* Set thread priority */
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority) != 0) {
        printf("Failed to set MAIN thread priority\n");
        return 5;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // INIT COMMAND BUFFER                                                                          //
    //////////////////////////////////////////////////////////////////////////////////////////////////
    CommandBuffer cb;
    volatile int buffer[50];

    if (init_cmd_buffer(&cb, buffer, 50) != 0) {
        perror("ERROR: Failed to initialize command buffer!");
        return 1;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // INIT SPACEX TELEMETRY                                                                        //
    //////////////////////////////////////////////////////////////////////////////////////////////////
    if (init_spacex() != 0) {
        return 2;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // INIT CAN COMMUNICATION                                                                       //
    //////////////////////////////////////////////////////////////////////////////////////////////////
    if(can_init(&(run_data.can_data)) != 0) {
        return 12;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // CONFIG LOADING CODE                                                                          //
    //////////////////////////////////////////////////////////////////////////////////////////////////
    Thresholds thresholds;
    
    
    // char path[] = __FILE__;
    // strcpy(&path[strlen(__FILE__)-10], "config.csv");
    // FILE *config_file = fopen(path, "r");

    // printf("################################################################\n");
    // printf("#                       reading csv                            #\n");
    // printf("################################################################\n");

    // if ( config_file == NULL ) { /* error opening file */
        // printf("ERROR: cannot open file: %s\n", path);
        // return 0;
    // }
    // else {
        // char line [128]; /* or other suitable maximum line size */
        // while (fgets ( line, sizeof line, config_file ) != NULL) { /* read a line */
            // printf("%s", line); 
            // char *token = strtok(line, ",");
            // char *type = token;
            // int i = 1;

            // token = strtok(NULL, ",");
            
            // while( token != NULL ) {
                // char *curr_tok = token;
                
                // if (strcmp("track_length", type) == 0) {
                    // if (i == 1) thresholds.track_length = atof(curr_tok);
                    // else {
                    // }
                // }
                // else if (strcmp("brake_distance", type) == 0) {
                    // if (i == 1) thresholds.brake_distance = atof(curr_tok);
                    // else {
                    // }
                // }
                // else if (strcmp("acceleration_distance", type) == 0) {
                    // if (i == 1) thresholds.acceleration_distance = atof(curr_tok);
                    // else {
                    // }
                // }
                // else if (strcmp("battery_temperature", type) == 0) {
                    // if (i == 1) thresholds.battery_temperature_low = atof(curr_tok);
                    // else if (i == 2) thresholds.battery_temperature_high = atof(curr_tok);
                    // else if (i == 3) thresholds.battery_temperature_pers = atof(curr_tok);
                    // else {
                    // }
                // }
                // else if (strcmp("motor_temperature", type) == 0) {
                    // if (i == 1) thresholds.motor_temperature_low = atof(curr_tok);
                    // else if (i == 2) thresholds.motor_temperature_high = atof(curr_tok);
                    // else if (i == 3) thresholds.motor_temperature_pers = atof(curr_tok);
                    // else {
                    // }
                // }

                // i++;
                // token = strtok(NULL, ",");
            // }    
        // }
        // fclose ( config_file );
    // }

    // Edit for manual testing
    // printf("threshold stuct values: bd:%f ad:%f %f %f %f\n", 
        // thresholds.brake_distance, 
        // thresholds.acceleration_distance, 
        // thresholds.battery_temperature_low, 
        // thresholds.battery_temperature_high, 
        // thresholds.battery_temperature_pers);

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // INIT UDP COMMUNICATION                                                                       //
    //////////////////////////////////////////////////////////////////////////////////////////////////
    if(udp_init(&cb, &run_data) != 0) {
        return 3;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // REST OF STATE CODE STARTS HERE                                                               //
    //////////////////////////////////////////////////////////////////////////////////////////////////
    int (*fp_arr[NUM_STATES]) (Fpga *fpga, Thresholds *, int);   //state function calls

    //1D array of Function Pointers to state functions
    fp_arr[STARTUP_SID] = &startup_state;
    fp_arr[STANDBY_SID] = &standby_state;
    fp_arr[INITIALIZE_SID] = &initialize_state;
    fp_arr[SERVICE_SID] = &service_state;
    fp_arr[PRECHARGE_SID] = &precharge_state;
    fp_arr[ENABLEMOTOR_SID] = &enablemotor_state;
    fp_arr[ACCELERATE_SID] = &accelerate_state;
    fp_arr[NORMBRAKE_SID] = &normbrake_state;
    fp_arr[ESTOP_SID] = &estop_state;
    fp_arr[IDLE_SID] = &idle_state;
    fp_arr[HVCUT_SID] = &hvcut_state;
    fp_arr[DISCHARGE_SID] = &discharge_state;
    fp_arr[ENDRUN_SID] = NULL;
    
    printf("################################################################\n");
    printf("#                     beginning of run                         #\n");
    printf("################################################################\n");

    //Initial values for state flow
    int command = 0;
    int next_state = startup_state(fpga, &thresholds, command);
    bool continueRun = true;
    
    /* Init sclk count */
    int time;
    SEQ_STORE(run_data.software.sclk, 0);
    
    uint8_t fpga_fail = false;
    //main state loop
    while (continueRun) {
        if(!fpga_fail) {
            fpgaRunAndUpdateIf(fpga, refresh_cache(fpga), "Refresh FPGA values");
            if (NiFpga_IsError(fpga->status)) {
                printf("Failed to refresh fpga cache, status code %d, exiting\n", fpga->status);
                 fpga_fail = 1;
            }
        }

        if (g_abort_run) {
            next_state = ESTOP_SID;
        }
        
        read_cmd(&cb, &command);
        next_state = (*fp_arr[next_state])(fpga, &thresholds, command);
        //end loop condition, warning, this does NOT necessarily brake, just makes pod take a nap
        if (next_state == ENDRUN_SID) {
            continueRun = false;
        }
        
        time = SEQ_LOAD(run_data.software.sclk);
        SEQ_STORE(run_data.software.sclk, time + 1);
    }
    
    /* Join threads */
    pthread_t tid;
    
    tid = LOAD(run_data.software.can_tid);
    if (tid > 0) {
        pthread_join(tid, NULL);
    }
    
    tid = LOAD(run_data.software.tlm_tid);
    if (tid > 0) {
        pthread_join(tid, NULL);
    }
    
    tid = LOAD(run_data.software.cmd_tid);
    if (tid > 0) {
        pthread_join(tid, NULL);
    }

    fpclose(fpga, 0);
    fpfinalize(fpga);

    return 0;
}
