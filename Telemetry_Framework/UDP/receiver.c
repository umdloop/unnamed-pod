#include <stdio.h>
#include <sys/socket.h>
#include <stdbool.h>
#include "commands.h"
#include "receiver.h"

#define RECV_LEN 100


/* This function runs as a thread continuously to receive commands.
 * Commands are written to a command buffer used by the state machine.
 * Invalid commands and timeout trigger comm loss condition.
 * 
 * Params:
 *     int sock -> socket (timeout already configured: see udp.c)
 * 
 * Returns:
 *     Never
 *     TODO Comm loss will trigger signal
 */
void *recv_cmds(void *args) {
    int sock = ((ReceiverArgs *)args)->sock;
    CommandBuffer *cb = ((ReceiverArgs *)args)->cb;
    char buffer[RECV_LEN];
    int cmd, len;

    while (1) {
        /* Receive 1 command, failures and timeout trigger comm loss condition */
        len = recvfrom(sock, &buffer, RECV_LEN, 0, NULL, NULL);
        if (len == -1) {
            printf("Receiver Error\n");
            //TODO Comm Loss
        }

        /* Verify magic word, pass command to state machine */
        if (len == 8 && *((int *)&buffer) == 0xC0DE) {
            cmd = *(((int *)&buffer) + 1); 
            if (verify_cmd(cmd) && (write_cmd(cb, cmd) == 0)) {
                printf("Received: %d\n", cmd);
                //TODO Telemeter success
            } else {
                if (cmd != NONE) {
                    printf("Invalid Command: %d\n", cmd);
                    //TODO Comm Loss
                }
            }
        }
    }
}

/* This function checks the validity of a received command.
 * 
 * Params:
 *     int cmd -> received from host
 * 
 * Returns:
 *     true  -> valid command
 *     false -> invalid command
 */
int verify_cmd(int cmd) {
    if (cmd == EMERGENCY_BRAKE || (cmd > 0 && cmd < NUM_COMMANDS)) {
        return true;
    }
    
    return false;
}
