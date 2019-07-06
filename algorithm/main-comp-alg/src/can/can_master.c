#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include "can_master.h"
#include "can_handlers.h"
#include "vs_can_api.h"


#define CAN_FREQ 1000000L /* 1 kHz */
#define NS_IN_SEC 1000000000L
#define UPDATE_DELAY(name) name.tv_sec = name.tv_sec + ((name.tv_nsec + CAN_FREQ) / NS_IN_SEC);\
                           name.tv_nsec = (name.tv_nsec + CAN_FREQ) % NS_IN_SEC;
#define INIT_TIMES(name, time) sec = name.tv_sec + ((name.tv_nsec + time) / NS_IN_SEC);\
                               nsec = (name.tv_nsec + time) % NS_IN_SEC;

#define CAN_BUF_LEN 100

/* Used internally to handle received can messages */
static void check_timeouts(CAN_Data *data, struct timespec *now);
static void handle_can_message(CAN_Data *data, VSCAN_MSG *msg, struct timespec *timestamp);
static bool check_match(VSCAN_MSG *lookup, VSCAN_MSG *received);

/* Global variables */
VSCAN_MSG request_lookup[NUM_CAN_REQUESTS];
CAN_Response_Lookup response_lookup[NUM_CAN_RESPONSES];
VSCAN_HANDLE handle;


/* This function executes all CAN bus reads and writes through the NetCAN
 * device. This function should only be launched by can_init() after successful 
 * initialization of CAN messages and data.
 * 
 * NOTE: This function will loop as a thread, waiting for the state machine or other
 * functions to signal that a CAN message should be sent.
 *
 * Params:
 *     CAN_Data *data -> pointer to CAN_Data struct used by state machine
 *
 * Returns:
 *     void *
 */
void *can_master(void *args) {
    /* Buffer to read CAN messages into */
    CAN_Data *data = (CAN_Data *)args;
    VSCAN_MSG read_buffer[CAN_BUF_LEN];
    DWORD ret_val;
    VSCAN_STATUS status;
    CAN_State state;
    bool flush;
    int i;
    
    /* Initialize loop timing */
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        printf("clock_gettime() error: %s\n", strerror(errno));
        //TODO: Comm Loss
    }
    time_t sec = now.tv_sec;
    long nsec = now.tv_nsec;

    INIT_TIMES(now, CAN_FREQ)
    struct timespec delay = {sec, nsec};
    
    
    while (1) {
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &delay, NULL);
        
        /* Read CAN bus messages */
        status = VSCAN_Read(handle, read_buffer, CAN_BUF_LEN, &ret_val);
        if (status == VSCAN_ERR_OK) {
            for (i = 0; i < ret_val; i++) {
                handle_can_message(data, &(read_buffer[i]), &delay);
            }
        } else {
            //TODO: Comm Loss
        }
        
        /* Check timeouts */
        check_timeouts(data, &delay);
        
        /* Write CAN bus messages */
        for (i = 0; i < NUM_CAN_REQUESTS; i++) {
            /* Check SEND requests */
            state = SEQ_LOAD(data->requests[i].state);
            
            if (state == SEND) {
                /* Send message */
                status = VSCAN_Write(handle, &(request_lookup[i]), 1, &ret_val);
                if (ret_val != 1 || status != VSCAN_ERR_OK) {
                    //TODO: Comm Loss
                }
                
                /* Flush write-buffer this cycle */
                flush = true;
                
                /* Update to WAITING state */
                SEQ_STORE(data->requests[i].state, WAITING);
                
                /* Record timestamp */
                SEQ_STORE(data->requests[i].sent_time.tv_sec, delay.tv_sec);
                SEQ_STORE(data->requests[i].sent_time.tv_nsec, delay.tv_nsec);
            }
        }
        
        /* Flush write-buffer if anything was written */
        if (flush) {
            status = VSCAN_Flush(handle);
            if (status != VSCAN_ERR_OK) {
                //TODO: Comm Loss
            }
            flush = false;
        }
        
        UPDATE_DELAY(delay)
    }
}

/* This function iterates through all responses and requests to determine if
 * they have overrun their timeout bounds. A communication loss condition is
 * triggered if any response with check_timeout set to true overruns its limit.
 * If a request times out, the request is set to the TIMEOUT state and its
 * timeout counter is incremented.
 *
 * Params:
 *     CAN_Data *data -> pointer to CAN_Data struct used by state machine
 *     struct timespec now -> current CLOCK_MONOTONIC time
 *
 * Returns:
 *     void
 */
void check_timeouts(CAN_Data *data, struct timespec *now) {
    int i;
    bool check;
    time_t d_s;
    long d_ns;
    struct timespec interval;
    unsigned char count;
    
    /* Check responses */
    for (i = 0; i < NUM_CAN_RESPONSES; i++) {
        if (SEQ_LOAD(data->responses[i].check_timeout)) {
            interval.tv_sec = SEQ_LOAD(data->responses[i].timeout_interval.tv_sec);
            interval.tv_nsec = SEQ_LOAD(data->responses[i].timeout_interval.tv_nsec);
            d_s = now->tv_sec - SEQ_LOAD(data->responses[i].last_time.tv_sec);
            d_ns = now->tv_nsec - SEQ_LOAD(data->responses[i].last_time.tv_nsec);
            
            if (d_s > interval.tv_sec || (d_s == interval.tv_sec && d_ns > interval.tv_nsec)) {
                //TODO: Comm Loss
            }
        }
    }
    
    /* Check requests */
    for (i = 0; i < NUM_CAN_REQUESTS; i++) {
        if ((SEQ_LOAD(data->requests[i].check_timeout) == WAITING) && SEQ_LOAD(data->requests[i].check_timeout)) {
            interval.tv_sec = SEQ_LOAD(data->requests[i].timeout_interval.tv_sec);
            interval.tv_nsec = SEQ_LOAD(data->requests[i].timeout_interval.tv_nsec);
            d_s = now->tv_sec - SEQ_LOAD(data->requests[i].sent_time.tv_sec);
            d_ns = now->tv_nsec - SEQ_LOAD(data->requests[i].sent_time.tv_nsec);
            
            if (d_s > interval.tv_sec || (d_s == interval.tv_sec && d_ns > interval.tv_nsec)) {
                count = SEQ_LOAD(data->requests[i].timeout_count);
                SEQ_STORE(data->requests[i].timeout_count, count+1);
                SEQ_STORE(data->requests[i].state, TIMEOUT);
            }
        }
    }
}

/* This function takes one message received from the CAN bus and:
 *     1) Matches it with a lookup message template
 *     2) Updates CAN_Data associated with identified message
 *     3) Calls message-specific handler function (see can_handlers.c)
 *
 * NOTE: If the message cannot be identified, this function will signal
 *       a communication loss condition to stop the pod.
 *
 * Params:
 *     CAN_Data *data -> pointer to CAN_Data struct used by state machine
 *     VSCAN_MSG *msg -> message received from the CAN bus
 *     timespec *timestamp -> CLOCK_MONOTONIC time the message was received
 *
 * Returns:
 *     void
 */
void handle_can_message(CAN_Data *data, VSCAN_MSG *msg, struct timespec *timestamp) {
    int msg_id = -1;
    int count, id, req_id;
    
    /* Identify message */
    for (id = 0; id < NUM_CAN_RESPONSES; id++) {
        if (check_match(&(response_lookup[id].msg), msg)) {
            msg_id = id;
            break;
        }
    }
    
    if (msg_id == -1) {
        //TODO: Comm Loss
    }
    
    /* Update CAN_Data metadata for this message */
    SEQ_STORE(data->responses[msg_id].last_time.tv_sec, timestamp->tv_sec);
    SEQ_STORE(data->responses[msg_id].last_time.tv_nsec, timestamp->tv_nsec);
    count = SEQ_LOAD(data->responses[msg_id].rx_count);
    SEQ_STORE(data->responses[msg_id].rx_count, count+1);
    
    /* Call handler function to update CAN_Data fields */
    response_lookup[msg_id].handler(msg, data);
    
    /* If this msg satisfies a request, mark request as COMPLETE */
    req_id = response_lookup[msg_id].request_num;
    if (req_id != -1) {
        SEQ_STORE(data->requests[req_id].state, COMPLETE);
    }
}

/* This function compares a received CAN message to a template lookup message.
 * This function checks the flags, id, and data bytes specified in the template
 * for equality to determine if the two messages match.
 * 
 * Params:
 *     VSCAN_MSG *lookup   -> Cached CAN message template
 *     VSCAN_MSG *received -> Received CAN message
 *
 * Returns:
 *     true  -> messages match
 *     false -> messages do not match
 */
bool check_match(VSCAN_MSG *lookup, VSCAN_MSG *received) {
    int i;
    
    if (lookup->Flags != received->Flags)
        return false;
    
    if (lookup->Id != received->Id)
        return false;
    
    for (i = 0; i < lookup->Size; i++) {
        if (lookup->Data[i] != received->Data[i])
            return false;
    }
    
    return true;
}

