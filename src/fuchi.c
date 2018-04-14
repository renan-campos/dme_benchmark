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
    int haveToken;       // holding situation of token (0 or 1)
} myNode;

// Form of exclusion request messages
struct Request {
    int timeStamp;       // Time stamp of message
    int sender;          // Number on the sender of message
    int requestTimes[N]; // Time of node requesting exclusion
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
    } msg; 
};


// Helper function to send message to specified node.
static void send_msg(int msqid, struct fuchi_msg mmsg, int to) {
    MSG imsg;

    // If destination is local node, place directly in that queue.
    if (myNode.number == to)
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

// Function which returns the affixed number of eht element having the minimum
// value (except NULLtime) in the reference array. However, if all the values
// are Nulltime, then NULLnode is returned.
static int searchOldestRequest() {
    int i;
    int lowtime = NULLtime;
    int lownode = NULLnode;
    for (i = 0; i < N; i++)
        if (lownode == NULLnode || myNode.requestTimes[i] < lowtime) {
            lownode = i;
            lowtime = myNode.requestTimes[i];
        }

    if (lowtime == NULLtime)
        return NULLnode;
    // Adding one because nodes start at 1, while array starts at 0.
    return lownode + 1;
}

static int max(int a, int b) {
	if (a > b)
		return a;
    return b;
}

void *dme_msg_handler(void *arg) {
    int nid  = *((int *) arg);      // nid contains the node id.
    int ntot = *(((int *) arg)+1);  // Total number of nodes. 
    int msqid = msgget(M_ID, 0600); // msqid contains the message queue id.
    MSG imsg;
    struct fuchi_msg mmsg;

    int M = voting_set_size[ntot];
            
    struct Request *request;
    struct Token *token;
    struct Finish *finish;
    int i;
    int nextNode;

    if (msqid == -1) {
        perror("msgget failed :\n");
        exit(1);
    }

    // prints are for logging information
    printf("Fuchi algorithm started with %d nodes\n", ntot); 
    fflush(stdout);
   
    // TODO Initialize Fuchi data structures.
    myNode.number = nid;
    myNode.timeStamp = 0;
    myNode.member = (int *) &voting_set[ntot][nid];
    for (i = 0; i < N; i++) {
	myNode.requestTimes[i] = NULLtime;
	myNode.finishTimes[i] = NULLtime;
    }
    myNode.waitNode = NULLnode;
    myNode.waitTime = NULLtime;
    myNode.oldestStamp = NULLtime;
    myNode.haveToken = 0;
    if (nid == 1) {
	/* Initialize token */
	myNode.haveToken = 1;
	keepToken.timeStamp = 0;
	for (i = 0; i < N; i++) {
	    keepToken.requestTimes[i] = NULLtime;
	    keepToken.finishTimes[i] = NULLtime;
	}
	/* Send finishes to members of 1's voting set. */
	finish = &mmsg.msg.finish;
	finish->timeStamp = 0;
	finish->sender = nid;
	for (i = 0; i < N; i++) finish->finishTimes[i] = NULLtime; 

	mmsg.type = FINISH;
	for (i = 0; i < M; i++) {
	    printf("FUCHI: FINISH sent to %d\n", myNode.member[i]);
	    fflush(stdout);
	    send_msg(msqid, mmsg, myNode.member[i]);
	}
    }

 
    for (;;) {
        
        // Receiving next message
        if (msgrcv(msqid, &imsg, sizeof(MSG), TO_DME, 0) == -1) {
            perror("msgrcv failed :\n");
            exit(1);
        }
        
        printf("FUCHI: Message queue message received!\n");
        fflush(stdout);
    
        memcpy(&mmsg, &imsg.buf, sizeof(struct fuchi_msg));
        
        switch(mmsg.type) {
        case REQUEST:
	    printf("FUCHI: REQUEST received!\n");
	    fflush(stdout);
            /* Exclusion request receiving procedure */
            request = &mmsg.msg.request;
            /* Updating time stamp */
            myNode.timeStamp = max(myNode.timeStamp, request->timeStamp);
            /* Updating finish exclusion information */
            for (i = 0; i < N; i++)
                myNode.finishTimes[i] = max(myNode.finishTimes[i], request->finishTimes[i]);
            /* Masking */
            for (i = 0; i < N; i++) {
                if (myNode.requestTimes[i] <= myNode.finishTimes[i])
                    myNode.requestTimes[i] = NULLtime;
                if (request->requestTimes[i] <= myNode.finishTimes[i])
                    request->requestTimes[i] = NULLtime;
            }
            /* Updating exclusion request time information */
            for (i = 0; i<N; i++)
                myNode.requestTimes[i] = max(myNode.requestTimes[i], request->requestTimes[i]);
            /* Processing for the case of waiting for finish message */
            if (myNode.waitNode != NULLnode && searchOldestRequest(myNode.requestTimes) != NULLnode) {
                /* If there is exclusion request */
                if (myNode.waitTime > myNode.finishTimes[myNode.waitNode]) {
                    /* If effective */
                    myNode.timeStamp++;
                    request->timeStamp = myNode.timeStamp;
                    for (i = 0; i < N; i ++) request->requestTimes[i] = myNode.requestTimes[i];
                    for (i = 0; i < N; i ++) request->finishTimes[i] = myNode.finishTimes[i];
                    
                    printf("FUCHI: REQUEST sent to %d\n", myNode.waitNode);
                    fflush(stdout);
                    send_msg(msqid, mmsg, myNode.waitNode);
                }
                myNode.waitNode = NULLnode;
                myNode.waitTime = NULLtime;
            }
            /* Processing anti-starvation time stamp */
            for (i = 0; i < N; i++)
                if (request->oldestStamp >= myNode.requestTimes[i]) {
                    myNode.timeStamp++;
                    request->timeStamp = myNode.timeStamp;
                    for (i = 0; i < N; i++) request->requestTimes[i] = myNode.requestTimes[i];
                    for (i = 0; i < N; i++) request->finishTimes[i] = myNode.finishTimes[i];
                    
                    printf("FUCHI: REQUEST sent to %d\n", request->sender);
                    fflush(stdout);
                    send_msg(msqid, mmsg, request->sender);
                    break;
                }
            /* If token is held */
            if (myNode.haveToken) {
                nextNode = searchOldestRequest(myNode.requestTimes);
                if (nextNode != NULLnode) {
                    myNode.timeStamp++;
                    myNode.oldestStamp = myNode.requestTimes[nextNode];
                    myNode.waitNode = NULLnode;
                    myNode.waitTime = NULLtime;
                    myNode.haveToken = 0;
                    for (i = 0; i < N; i++) keepToken.requestTimes[i] = myNode.requestTimes[i];
                    for (i = 0; i < N; i++) keepToken.finishTimes[i] = myNode.finishTimes[i];
                    keepToken.timeStamp = myNode.timeStamp;
                    
                    mmsg.msg.token = keepToken;
                    mmsg.type = TOKEN;
                    
                    printf("FUCHI: TOKEN sent to %d\n", nextNode);
                    fflush(stdout);
                    send_msg(msqid, mmsg, nextNode);
                }
            }
            break;
        case TOKEN:
	    printf("FUCHI: TOKEN received!\n");
	    fflush(stdout);
            /* Token receiving procedure */
            token = &mmsg.msg.token;
            finish = &mmsg.msg.finish;
            /* Updating time stamp */
            myNode.timeStamp = max(myNode.timeStamp, token->timeStamp);
            /* Updating finish exclusion information */
            for (i = 0; i < N; i++) myNode.finishTimes[i] = max(myNode.finishTimes[i], token->finishTimes[i]); 
            /* Masking */
            for (i = 0; i < N; i++) {
                if (myNode.requestTimes[i] <= myNode.finishTimes[i])
                    myNode.requestTimes[i] = NULLtime;
                if (token->requestTimes[i] <= myNode.finishTimes[i])
                    token->requestTimes[i] = NULLtime;
            }
            /* Updating exclusion request time information */
            for (i = 0; i < N; i++)
                myNode.requestTimes[i] = max(myNode.requestTimes[i], token->requestTimes[i]);
            
            /* Storing token locally */
            keepToken = *token;
            
            /* Critical Section */
            // Send process that called dme_down a message.
            memcpy(&imsg.buf, &mmsg, sizeof(struct fuchi_msg));
            imsg.size = sizeof(struct fuchi_msg);
            imsg.type = TO_CON;
            printf("FUCHI: message sent to producer\n");
            fflush(stdout);
            if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
                perror("Error on message send\n");
                exit(1);
            }	
            break;
        case FINISH:
	    printf("FUCHI: FINISH received!\n");
	    fflush(stdout);
            /* Finish exclusion procedure */
            finish = &mmsg.msg.finish;
            /* Updating time stamp */
            myNode.timeStamp= max(myNode.timeStamp, finish->timeStamp);
            /* Updating finish exclusion information */
            for (i = 0; i < N; i++)
                myNode.finishTimes[i] = max(myNode.finishTimes[i], finish->finishTimes[i]);
            /* Masking */
            for (i = 0; i < N; i++)
                if (myNode.requestTimes[i] <= myNode.finishTimes[i])
                    myNode.requestTimes[i] = NULLtime;
            if (finish->timeStamp > myNode.finishTimes[finish->sender]) {
                /* Case of effective finish message */
                if (searchOldestRequest(myNode.requestTimes) != NULLnode) {
                    /* If there is exclusion request */
                    /* Forwarding exclusion request */
                    myNode.timeStamp++;
                    nextNode = finish->sender;
                    request = &mmsg.msg.request;
                    request->timeStamp = myNode.timeStamp;
                    request->sender = myNode.number;
                    for (i = 0; i < N; i++) {
                        request->requestTimes[i] = myNode.requestTimes[i];
                        request->finishTimes[i] = myNode.finishTimes[i];
                    }
                    request->oldestStamp = NULLtime;

                    mmsg.type = REQUEST;
                    printf("FUCHI: REQUEST sent to %d\n", nextNode);
                    fflush(stdout);
                    send_msg(msqid, mmsg, nextNode);

                    if (myNode.waitTime <= myNode.finishTimes[myNode.waitNode]) {
                        myNode.waitNode = NULLnode;
                        myNode.waitTime = NULLtime;
                    }
                }
                else {
                    /* If there is no exclusion request */
                    /* go into waiting state */
                    myNode.waitNode = finish->sender;
                    myNode.waitTime = finish->timeStamp;
                }
            }
            break;
        case LOCAL_REQUEST:
	    printf("FUCHI: LOCAL_REQUEST received!\n");
	    fflush(stdout);
            request = &mmsg.msg.request;
            if (myNode.haveToken) {
                // Turning haveToken off so that it doesn't get sent out while running c.s.
                // LOCAL_FINISH will turn it back on if there are no other requests.
                myNode.haveToken = 0;
				// Send process that called dme_down a message.
				memcpy(&imsg.buf, &mmsg, sizeof(struct fuchi_msg));
				imsg.size = sizeof(struct fuchi_msg);
				imsg.type = TO_CON;
                printf("FUCHI: message sent to producer\n");
                fflush(stdout);
				if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
					perror("Error on message send\n");
					exit(1);
				}	
            }
            else {
                myNode.timeStamp++;
                mmsg.msg.request.timeStamp = myNode.timeStamp;
                myNode.requestTimes[myNode.number] = myNode.timeStamp;
                request->sender = myNode.number;
                request->oldestStamp = myNode.oldestStamp;
                for (i=0; i<N; i++) request->requestTimes[i] = myNode.requestTimes[i];
                for (i=0; i<N; i++) request->finishTimes[i]  = myNode.finishTimes[i];
                // Send REQUEST to voting set.
                mmsg.type = REQUEST;
                for (i = 0; i < M; i++) {
                    printf("FUCHI: REQUEST sent to %d\n", myNode.member[i]);
                    fflush(stdout);
                    send_msg(msqid, mmsg, myNode.member[i]);
                }
            }
            break;
        case LOCAL_FINISH:
	    printf("FUCHI: LOCAL_FINISH received!\n");
	    fflush(stdout);
            token = &keepToken;
            finish = &mmsg.msg.finish;
            
            myNode.requestTimes[myNode.number] = NULLtime;
            myNode.finishTimes[myNode.number] = myNode.timeStamp;
            for (i = 0; i < N; i++)
                token->requestTimes[i] = myNode.requestTimes[i];
            for (i = 0; i < N; i++)
                token->finishTimes[i] = myNode.finishTimes[i];
            /* Searching the oldest exclusion request */
            nextNode = searchOldestRequest(myNode.requestTimes);
            myNode.timeStamp++;
            /* The case where there is an exclusion request */
            if (nextNode != NULLnode) {
                myNode.oldestStamp = myNode.requestTimes[nextNode];
                token->timeStamp = myNode.timeStamp;

                mmsg.msg.token = keepToken;
                
                printf("FUCHI: TOKEN sent to %d\n", nextNode);
                fflush(stdout);
                send_msg(msqid, mmsg, nextNode);
            }
            /* The case where there is no exclusion request */
            else {
                finish->timeStamp = myNode.timeStamp;
                finish->sender = myNode.number;
                for (i = 0; i < N; i++)
                    finish->finishTimes[i] = myNode.finishTimes[i];
                myNode.oldestStamp = NULLtime;
                myNode.waitNode = NULLnode;
                myNode.waitTime = NULLtime;
                myNode.haveToken = 1;
                
                mmsg.type = FINISH;
                for (i = 0; i < M; i++) {
                    printf("FUCHI: FINISH sent to %d\n", myNode.member[i]);
                    fflush(stdout);
                    send_msg(msqid, mmsg, myNode.member[i]);
                }
            }
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
