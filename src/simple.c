#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <sys/msg.h>
#include <netinet/in.h>

#include "dme.h"
// A simple implementation of distributed mutual exclusion
// When a node wants to go, it just tells everyone it is doing so, and then does it.
// This algorithm can have collisions.
struct simple_msg {
	int n;
	int r;
};

void *dme_msg_handler(void *arg) {
    int nid = *((int *) arg);
	int msqid = msgget(M_ID, 0600);
	MSG imsg;
	struct simple_msg smsg;

	if (msqid == -1) {
		perror("msgget failed :\n");
		exit(1);
	}

    printf("Simple dme started\n");
    fflush(stdout);

	for (;;) {
		if (msgrcv(msqid, &imsg, sizeof(MSG), TO_DME, 0) == -1) {
			perror("msgrcv failed :\n");
			exit(1);
		}
		
        printf("Simple: Message queue message received!\n");
        fflush(stdout);
    
		memcpy(&smsg, &imsg.buf, sizeof(struct simple_msg));
		// Check if from receiver or consumer
		if (imsg.network) {
			// Message was from another node. Convert to hardware byte order and print.
			printf("Simple: Node %d making request #%d\n", smsg.n, smsg.r);
            fflush(stdout);
		}	
		else {
			// Message was from consumer. Convert to network byte order, send to sender, and reply to consumer.
			printf("Simple: Sending message %d to sender\n", smsg.r);
            fflush(stdout);

			smsg.n = nid;

			memcpy(&imsg.buf, &smsg, sizeof(struct simple_msg));
			imsg.size = sizeof(struct simple_msg);
			imsg.type = TO_SND;
    
            if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
				perror("Error on message send\n");
				exit(1);
			}	
			imsg.type = TO_CON;
			if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
				perror("Error on message send\n");
				exit(1);
			}	
		}
	}
}

void dme_down() { 
	static int r = 0;
	int msqid = msgget(M_ID, 0600);
	MSG imsg;
	struct simple_msg smsg;

	if (msqid == -1) {
		perror("msgget failed :\n");
		exit(1);
	}

	smsg.n = 0;
	smsg.r = ++r;
	

	memcpy(&imsg.buf, &smsg, sizeof(struct simple_msg));
    
    imsg.size = sizeof(struct simple_msg);
	imsg.type = TO_DME;
	imsg.network = 0;

	if (msgsnd(msqid, &imsg, sizeof(MSG), 0) == -1) {
		perror("Error on message send\n");
		exit(1);
	}	

	if (msgrcv(msqid, &imsg, sizeof(MSG), TO_CON, 0) == -1) {
		perror("Error on message receive\n");
		exit(1);
	}	
}

void dme_up() {
}
