/******************************************************************************/
/*                                                                            */
/* fuchi.c - An implementation of Fuchi's distributed mutual exclusion        */
/*           algorithm described in the paper:                                */
/*           "An Improved sqrt(N) Algorithm for Mutual Exclusion in           */
/*            Decentralized Systems" (Fuchi, 1992).                           */
/*                                                                            */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <sys/msg.h>
#include <netinet/in.h>

#include "dme.h"

// The voting set must have the following properties:
// 1. All node's sets have a non-null intersection (optimally of size 1). 
// 2. Node i's set always contains i.
// 3. The size of i's set is K, for any i.
// 4. All nodes apear in an equal number of sets. 
// NOTE: Ideally this should be automated, but are currently pre-computed.
int voting_set[8][8][3] = {{{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, // Set of zero nodes
                           {{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, // Set of one node
                           {{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, // Set of two nodes
                           {{0,0,0},{1,2,0},{2,3,0},{1,3,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, // Set of three nodes
                           {{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, // Set of four nodes
                           {{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, // Set of five nodes
                           {{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, // Set of six nodes
                           {{0,0,0},{1,2,3},{2,4,6},{3,5,6},{1,4,5},{2,5,7},{1,6,7},{3,4,7}}, // Set of seven nodes
                       };
int voting_set_size[] = { 0, 1, 2,
              2,        // Set of three nodes
              4, 5, 6,
              3,        // Set of seven nodes
              8, 9, 10, 11, 12,
              4 };        // Set of thirteen nodes

/******************************************************************************/
/*                                                                            */
/* The following definitions have been taken directly from the appendix of    */
/* Fuchi's publication.                                                       */
/*                                                                            */
/******************************************************************************/

// Maximum number of nodes
#define N 7

// A number which is not a node number.
#define NULLnode -1

// Number smaller than any time.
#define NULLtime -1

// Function which returns the affixed number of eht element having the minimum
// value (except NULLtime) in the reference array. However, if all the values
// are Nulltime, then NULLnode is returned.
static int searchOldestRequest() {
    int i;
    int lowtime = NULLtime;
    int lownode = NULLnode
    for (i = 0; i < N; i++)
        if (lownode == NULLnode || myNode.requestTimes[i] < lowtime) {
            lownode = i;
            lowtime = myNode.requestTimes[i];
        }

    if (lowtime == NULLtime)
        return NULLnode;
    return lownode + 1;
}

// Form of information possessed by each node.
struct Node {
    int number;          // non-redundant number indicating node id
    int timeStamp;       // time stamp of node
    int *member;         // Members of node
    int requestTimes[N]; // Time of node requesting exclusion
    int finishTimes[N];  // Finish exclusion time of another node
    int waitNode;        // sender of finish exclusion message
    int waitTime;        // time the finish exclusion is received
    int oldestStamp;     // anti-starvation time stamp
    int have;            // holding situation of token (0 or 1)
} myNode;

// Form of exclusion request messages
struct Request {
    int timeStamp;       // Time stamp of message
    int sender;          // Number on the sender of message
    int RequestTimes[N]; // Time of node requesting exclusion
    int finishTimes[N];  // Finish exclusion time of another node
    int oldestStamp;     // anti-starvation time stamp
};

// Form of finish exclusion message
struct Finish {
    int timeStamp;      // Time stamp message
    int sender;         // number on the sender of message
    int finishTimes[N]; // finish exclusion time of another node
};

// Form of token representing exclusion privilege
struct Token {
    int timeStamp;       // time stamp of token
    int requestTimes[N]; // time of node requesting exclusion
    int finishTimes[N];  // finish exclusion time of another node
} keepToken;

/******************************************************************************/

// Data structures used by dme_msg_handler
// Types of messages that can be received
typedef enum { REQUEST,
               TOKEN,
               FINISH,
               LOCAL_REQUEST,
               LOCAL_FINISH     } m_type;

// Message holds type and one of three structures.
struct fuchi_msg {
    m_type type;
    union msg {
    struct Request request;
    struct Finish  finish;
    struct Token   token;
    }; 
};


// Helper function to send message to specified node.
static void send_msg(int msqid, struct fuchi_msg mmsg, int to) {
    MSG imsg;

    // If destination is local node, place directly in that queue.
    if (mmsg.nid == to)
        imsg.type = TO_DME;
    else
        imsg.type = TO_SND;
    imsg.network = to;
    memcpy(&imsg.buf, &(mmsg), sizeof(struct fuchi_msg));
    imsg.size = sizeof(struct fuchi_msg);

    if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
        perror("Error on message send\n");
        exit(1);
    }
}

void *dme_msg_handler(void *arg) {
    int nid  = *((int *) arg);      // nid contains the node id.
    int ntot = *(((int *) arg)+1);  // Total number of nodes. 
    int msqid = msgget(M_ID, 0600); // msqid contains the message queue id.
    MSG imsg;

    if (msqid == -1) {
        perror("msgget failed :\n");
        exit(1);
    }

    // prints are for logging information
    printf("Fuchi algorithm started with %d nodes\n", ntot); 
    fflush(stdout);
   
    // TODO Initialize Fuchi data structures.
 
    for (;;) {
        
        // Receiving next message
        if (msgrcv(msqid, &imsg, sizeof(MSG), TO_DME, 0) == -1) {
            perror("msgrcv failed :\n");
            exit(1);
        }
        
        printf("FUCHI: Message queue message received!\n");
        fflush(stdout);
    
        memcpy(&mmsg, &imsg.buf, sizeof(struct mae_msg));
        
        // TODO Handle received message.
        switch(mmsg.type) {
        case REQUEST:
            break;
        case TOKEN:
            break;
        case FINISH:
            break;
        case LOCAL_REQUEST:
            break;
        case LOCAL_FINISH:
            break;
        }
    }
}

void dme_down() { 
    int msqid = msgget(M_ID, 0600);
    MSG imsg;
    struct fuchi_msg mmsg;

    if (msqid == -1) {
        perror("msgget failed :\n");
        exit(1);
    }
    
    mmsg.type = LOCAL_REQUEST;

    memcpy(&imsg.buf, &mmsg, sizeof(struct fuchi_msg));

    // Place REQUEST in message queue, block until ready.
    imsg.size = sizeof(struct fuchi_msg);
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
    struct fuchi_msg mmsg;

    if (msqid == -1) {
        perror("msgget failed :\n");
        exit(1);
    }
    
    mmsg.type = LOCAL_FINISH;

    memcpy(&imsg.buf, &mmsg, sizeof(struct fuchi_msg));

    // Place RELEASE in message queue, block until ready.
    imsg.size = sizeof(struct fuchi_msg);
    imsg.type = TO_DME;
    if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
        perror("Error on message send\n");
        exit(1);
    }
}
