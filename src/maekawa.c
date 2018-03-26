/******************************************************************************/
/*                                                                            */
/* maekawa.c - An implementation of Maekawa's distributed mutual exclusion    */
/*             algorithm described in the paper:                              */
/*             "A sqrt(N) Algorithm for Mutual Exclusion in Decentralized     */
/*              Systems" (Maekawa, 1985).                                     */
/*                                                                            */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <sys/msg.h>
#include <netinet/in.h>

#include "dme.h"

// Data structures used by dme_msg_handler
// Types of messages that can be received
typedef enum { REQUEST,
               LOCK,
               FAIL,
               INQUIRY,
               RELINQUISH,
               RELEASE,
               LOCAL_REQUEST,
               LOCAL_RELEASE     } m_type;

// Message holds type and Lamport clock
struct mae_msg {
    m_type type;
	int clk;
	int nid;
};

// Queue is a collection of qents ordered by clock.
struct qent {
    struct mae_msg mmsg;
    struct qent *next;
};

// Inquery list entry.
struct ient {
    int node;
    struct ient *next;
}

// Fail flag set to 1 if FAIL received.
bool fflag; 

// The voting set must have the following properties:
// 1. All node's sets have a non-null intersection (optimally of size 1). 
// 2. Node i's set always contains i.
// 3. The size of i's set is K, for any i.
// 4. All nodes apear in an equal number of sets. 
// NOTE: Ideally this should be automated, but are currently pre-computed.
int voting_set[][][] = { {},                          // Set of zero nodes
                         {},                          // Set of one node
                         {},                          // Set of two nodes
                         { {}, {1,2}, {2,3}, {1,3} }, // Set of three nodes
                       };
// Predicate used to check if clock a preceeds b.
static bool preceed(struct mae_msg a, struct mae_msg b) {
    if (a.clk < b.clk)
        return true;
    if (a.clk == b.clk && a.nid < b.nid)
        return true;
    return false;
}

static void send_msg(struct mae_msg mmsg, int to) {
	MSG imsg;

	// If destination is local node, place directly in that queue.
	if (mmsg.nid == to)
		imsg.type = TO_DME;
	else
		imsg.type = TO_SND;
	imsg.network = to;
	memcpy(&imsg.buf, &(mmsg), sizeof(struct mae_msg));
	imsg.size = sizeof(struct mae_msg);

	if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
		perror("Error on message send\n");
		exit(1);
	}
}

void *dme_msg_handler(void *arg) {
    static int clock = 1;
    int nid  = *((int *) arg);      // nid contains the node id.
    int ntot = *(((int *) arg)+1);  // Total number of nodes. 
	int msqid = msgget(M_ID, 0600); // msqid contains the message queue id.
	struct mae_msg mmsg;

    // Queue for requests.
    struct qent *mae_front = NULL;
    struct qent *temp1, *temp2;

    // List of Inquirys, will be emptied when a FAIL or RELEASE is received. 
    struct ient *inq_front = NULL;
    struct ient *itemp;	
    int i;

    fflag = false;

	if (msqid == -1) {
		perror("msgget failed :\n");
		exit(1);
	}

    printf("Maekawa algorithm started with %d nodes\n", ntot); // prints are for logging information
    fflush(stdout);
    
    for (;;) {
        // For debugging
        for( i = 0, temp1 = mae_front; temp1 != NULL; temp1 = temp1->next, i++) ;
        printf("MAEKAWA: %d entries in queue.\n", i);
        fflush(stdout);
        
        // Receiving next message
		if (msgrcv(msqid, &imsg, sizeof(MSG), TO_DME, 0) == -1) {
			perror("msgrcv failed :\n");
			exit(1);
		}
		
        printf("MAEKAWA: Message queue message received!\n");
        fflush(stdout);
    
		memcpy(&mmsg, &imsg.buf, sizeof(struct mae_msg));
        
        // Update clock
        if (mmsg.clk > clock) {
            clock = mmsg.clk;
        }

        switch(mmsg.type) {
        case REQUEST:
            printf("MAEKAWA: REQUEST received.\n", i);
            fflush(stdout);
            // Add to queue.
            temp1 = (struct qent *) malloc(sizeof(struct qent));
            temp1->mmsg       = mmsg;
            temp1->next       = NULL;
            if (mae_front == NULL) {
                mae_front = temp1;
				// This request is now the current.
				// Send LOCK 
				mmsg.nid = nid;
				mmsg.clk = clock;
				mmsg.type = LOCK;
				msgsnd(mmsg, temp1->mmsg.nid);
            }
            else {
                for (temp2 = mae_front; temp2->next != NULL && preceed(temp2->mmsg, temp1->mmsg); temp2=temp2->next) ;
                if (temp2 == mae_front) {
                    // Send INQUIRY to current
					mmsg.nid = nid;
					mmsg.clk = clock;
					mmsg.type = INQUIRY;
					msgsnd(mmsg, mae_front->mmsg.nid);
				}
				else {
					// Send FAIL to requesting node
					mmsg.nid = nid;
					mmsg.clk = clock;
					mmsg.type = LOCK;
					msgsnd(mmsg, temp1->mmsg.nid);
				}
				temp1->next = temp2->next;
				temp2->next = temp1;
            }
            break;
        case LOCK:
            printf("MAEKAWA: LOCK received.\n", i);
            fflush(stdout);
			lock_count++;
			if (lock_count == set_size) {
				// Ready to do critial section
				// Reset fail flag and Inquiry list.
				fflag = false;
				for (itemp = inq_front; itemp != NULL; itemp = inq_front;) {
					inq_front = itemp->next;
					free(itemp);
				}
				lock_count = 0;
				// Send process that called dme_down a message.
				memcpy(&imsg.buf, &mmsg, sizeof(struct mae_msg));
				imsg.size = sizeof(struct mae_msg);
				imsg.type = TO_CON;
				if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
					perror("Error on message send\n");
					exit(1);
				}	
			}
            break;
        case FAIL:
            printf("MAEKAWA: FAIL received.\n", i);
            fflush(stdout);
			fflag = true;
			mmsg.nid  = nid;
			mmsg.type = RELINQUISH;
			while (inq_front != NULL) {
				itemp = inq_front;
				inq_front = inq_front->next;

				send_msg(mmsg, itemp->node); 

				free(itemp);
				// For every RELINQUISH sent, decrement lock_count
				lock_count--;
			}
            break;
        case INQUIRY:
            printf("MAEKAWA: INQUIRY received.\n", i);
            fflush(stdout);
			
			// Add to inquiry list
			itemp = (struct ient *) malloc(sizeof(struct ient));
			itemp->node = mmsg.nid;
			itemp->next = inq_front;
			
			inq_front = itemp;
			
			if (fflag) {
				mmsg.nid  = nid;
				mmsg.type = RELINQUISH;
				while (inq_front != NULL) {
					itemp = inq_front;
					inq_front = inq_front->next;

				    send_msg(mmsg, itemp->node); 

					free(itemp);
				    // For every RELINQUISH sent, decrement lock_count
					lock_count--;
				}
		    }
            break;
        case RELINQUISH:
            printf("MAEKAWA: RELINQUISH received.\n", i);
            fflush(stdout);
			if (mae_front->mmsg.nid == nid) {
				lock_count--;
            }
			// Requeue the current 
			temp1 = mae_front;
		    mae_front = mae_front->next;	
			for (temp2 = mae_front; temp2->next != NULL && preceed(temp2->mmsg, temp1->mmsg); temp2=temp2->next) ;
			temp1->next = temp2->next;
			// Send LOCK to new current.
			mmsg.nid = nid;
		    mmsg.type = LOCK;
			send_msg(mmsg, mae_front->mmsg.nid);
            break;
        case RELEASE:
            printf("MAEKAWA: RELEASE received.\n", i);
            fflush(stdout);
			// Pop next current from queue.
			temp1 = mae_front;
			mae_front = mae_front->next;
			free(temp1);
			// send LOCK to new current.
			mmsg.nid = nid;
		    mmsg.type = LOCK;
			send_msg(mmsg, mae_front->mmsg.nid);
			break;
        case LOCAL_REQUEST:
            printf("MAEKAWA: LOCAL_REQUEST received.\n", i);
            fflush(stdout);
            // Local request received.
            mmsg.nid = nid;
            mmsg.clk = clock++;
            // Send REQUEST to voting set.
			mmsg.type = REQUEST;
			for (i = 0; i < set_size; i++)
				send_msg(mmsg, voting_set[ntot][nid][i]);
            break;
        case LOCAL_RELEASE:
            printf("MAEKAWA: LOCAL_RELEASE received.\n", i);
            fflush(stdout);
            mmsg.nid = nid;
            mmsg.clk = clock++;
            // Send RELEASE to voting set.
			mmsg.type = RELEASE;
			for (i = 0; i < set_size; i++)
				send_msg(mmsg, voting_set[ntot][nid][i]);
            break;
        }
	}
}

void dme_down() { 
	int msqid = msgget(M_ID, 0600);
	MSG imsg;
	struct ric_msg mmsg;

	if (msqid == -1) {
		perror("msgget failed :\n");
		exit(1);
	}
    
    mmsg.type = LOCAL_REQUEST;
    mmsg.clk = 0; // Handler will fill in the clock value
    mmsg.nid = 0; // nid of 0 implies local node.

	memcpy(&imsg.buf, &mmsg, sizeof(struct mae_msg));

    // Place REQUEST in message queue, block until ready.
    imsg.size = sizeof(struct mae_msg);
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
	struct mae_msg mmsg;

	if (msqid == -1) {
		perror("msgget failed :\n");
		exit(1);
	}
    
    mmsg.type = LOCAL_RELEASE;

	memcpy(&imsg.buf, &mmsg, sizeof(struct mae_msg));

    // Place REQUEST in message queue, block until ready.
    imsg.size = sizeof(struct mae_msg);
	imsg.type = TO_DME;
	if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
		perror("Error on message send\n");
		exit(1);
	}
}