/******************************************************************************/
/*                                                                            */
/* ricart.c - An implementation of Ricart and Agrawala's mutual exclusion     */
/*            algorithm described in:                                         */
/*           "An Optimal Algorithm for Mutual Exclusion in Computer Networks" */
/*           Ricart & Agrawala, 1981                                          */
/*                                                                            */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <sys/msg.h>
#include <netinet/in.h>

#include "dme.h"

typedef enum {REQUEST, REPLY} r_type; 

struct ric_msg {
    r_type type;
	int clk;
	int nid;
};

struct qent {
    struct ric_msg rmsg;
    int reply_count;     // Number of replies.
    struct qent *next;
};

void *dme_msg_handler(void *arg) {
    static int clock = 1;
    int nid  = *((int *) arg);      // nid contains the node id.
    int ntot = *(((int *) arg)+1);  // Total number of nodes. 
	int msqid = msgget(M_ID, 0600); // msqid contains the message queue id.
	MSG imsg;
	struct ric_msg rmsg;

    // Queue for requests.
    struct qent *ric_front;
    struct qent *temp1, *temp2;

    int i;

	if (msqid == -1) {
		perror("msgget failed :\n");
		exit(1);
	}

    printf("Ricart algorithm started with %d nodes\n", ntot); // prints are for logging information
    fflush(stdout);
    
    for( i = 0, temp1 = ric_front; temp1 != NULL; temp1 = temp1->next, i++) ;
    printf("RICART: %d entries in queue.\n", i);
    fflush(stdout);

	for (;;) {
        for( i = 0, temp1 = ric_front; temp1 != NULL; temp1 = temp1->next, i++) ;
        printf("RICART: %d entries in queue.\n", i);
        fflush(stdout);

		if (msgrcv(msqid, &imsg, sizeof(MSG), TO_DME, 0) == -1) {
			perror("msgrcv failed :\n");
			exit(1);
		}
		
        printf("RICART: Message queue message received!\n");
        fflush(stdout);
    
		memcpy(&rmsg, &imsg.buf, sizeof(struct ric_msg));
        
        // Update clock
        if (rmsg.clk > clock) {
            clock = rmsg.clk;
        }
        
        if (rmsg.type == REQUEST) {
        // REQUEST received, add to the queue, then look at top at queue. If the
        // top is a request for another node, immediatley send a reply.
        // Repeat until request that belongs to this node is found, or the queue is empty. 
            // Check if local request, update nid and clock.
            if (rmsg.nid == 0) {
                rmsg.nid = nid;
                rmsg.clk = clock++;
            }

            
            // Add to queue
            if (ric_front == NULL || ric_front->rmsg.clk > rmsg.clk ||
            (ric_front->rmsg.clk == rmsg.clk && ric_front->rmsg.nid > rmsg.nid)) {
                // Queue empty, or bettwe than first entry -> add to the front
                temp1 = (struct qent *) malloc(sizeof(struct qent));
                temp1->rmsg        = rmsg; 
                temp1->reply_count = ntot-1;
                temp1->next        = ric_front;
                ric_front = temp1;
            }
            else {
                // Queue is not empty or not better than front, find appropriate spot to place this item. 
                for (temp1 = ric_front; (temp1->next != NULL &&
                (temp1->next->rmsg.clk < rmsg.clk || (temp1->next->rmsg.clk ==
                rmsg.clk && temp1->next->rmsg.nid < rmsg.nid))); temp1=temp1->next) ;
                
                temp2 = (struct qent *) malloc(sizeof(struct qent));
                temp2->rmsg = rmsg;
                temp2->reply_count = ntot-1;
                temp2->next = temp1->next;
                temp1->next = temp2;
            }
            
            // If local request, broadcast to other nodes.
            if (rmsg.nid == nid) {
                memcpy(&imsg.buf, &rmsg, sizeof(struct ric_msg));
                imsg.size = sizeof(struct ric_msg);
                imsg.type = TO_SND;
                imsg.network = 0; // Broadcast. 

                if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
                    perror("Error on message send\n");
                    exit(1);
                }	
            }
        }
        else {
        // REPLY received, find request in queue (on top), decrement number of replies
        // needed, if it is zero, set priority to zero (To ensure no REPLYs will
        // be sent from this node until critical section is complete), then send
        // message to client to start running the critical section.
        // If the number of replies needed is negative one (meaning this client
        // sent a REPLY), the client has completed executing its critical
        // section, and the queue can now be cleared of external requests. 
            temp1 = ric_front;
            
            if (temp1->rmsg.nid != nid) {
                // The top of the queue must be the local request because we
                // assume only one client per node and all remote requests are
                // immediatly REPLYed to unless in critical section. 
                printf("ERROR: Invalid message sent\n");
                exit(1);
            }

            temp1->reply_count--;
            if (temp1->reply_count == 0) {
                // Node ready to allow client to run critical section.
                // Block other requests and notify client.
                temp1->rmsg.clk = 0; // Now no requests will supercede this.
                // NOTE: doesn't matter what is in imsg, dme_down just needs a
                // message to unblock.
                imsg.type = TO_CON;
                if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
                    perror("Error on message send\n");
                    exit(1);
                }	
            }
            if (temp1->reply_count == -1) {
                // The client has finished the critical section, remove entry
                // and reply to remote requests.
                printf("RICART: Can send REPLY's again\n");
                fflush(stdout);
                ric_front = ric_front->next;
                free(temp1);
            }

        }
            
        // Look at top of queue. Send REPLYs while queue is not empty or top
        // is local request. 
        while (ric_front != NULL && ric_front->rmsg.nid != nid) {
            temp1 = ric_front;
            ric_front = ric_front->next;
            
            imsg.type = TO_SND;
            imsg.network = temp1->rmsg.nid;
            temp1->rmsg.nid = nid;
            temp1->rmsg.clk = clock++;
            temp1->rmsg.type = REPLY;
            memcpy(&imsg.buf, &(temp1->rmsg), sizeof(struct ric_msg));
            imsg.size = sizeof(struct ric_msg);

            printf("RICART: REPLY SENT\n");
            fflush(stdout);
            
            if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
                perror("Error on message send\n");
                exit(1);
            }
            // Remove entry from memory.
            free(temp1);
        }
	}
}

void dme_down() { 
	int msqid = msgget(M_ID, 0600);
	MSG imsg;
	struct ric_msg rmsg;

	if (msqid == -1) {
		perror("msgget failed :\n");
		exit(1);
	}
    
    rmsg.type = REQUEST;
    rmsg.clk = 0; // Handler will fill in the clock value
    rmsg.nid = 0; // nid of 0 implies local node.

	memcpy(&imsg.buf, &rmsg, sizeof(struct ric_msg));

    // Place REQUEST in message queue, block until ready.
    imsg.size = sizeof(struct ric_msg);
	imsg.type = TO_DME;
	if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
		perror("Error on message send\n");
		exit(1);
	}	


    // Wait to hear back from handler to start critical section. 
	if (msgrcv(msqid, &imsg, sizeof(MSG), TO_CON, 0) == -1) {
		perror("Error on message receive\n");
		exit(1);
	}	
}

void dme_up() {
    // Tell handler critical section is complete
    // This will allow it to send replies. 
	int msqid = msgget(M_ID, 0600);
	MSG imsg;
	struct ric_msg rmsg;

	if (msqid == -1) {
		perror("msgget failed :\n");
		exit(1);
	}
    
    rmsg.type = REPLY;

	memcpy(&imsg.buf, &rmsg, sizeof(struct ric_msg));

    // Place REQUEST in message queue, block until ready.
    imsg.size = sizeof(struct ric_msg);
	imsg.type = TO_DME;
	if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
		perror("Error on message send\n");
		exit(1);
	}	
}
