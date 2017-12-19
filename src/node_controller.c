// Standard headers
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>

// For message queue
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

// For signal handling
#include <signal.h>

// For posix threads
#include <pthread.h>

// For rand48
#include <sys/time.h>

// Socket headers
#include <sys/socket.h> // Socket structure declarations
#include <netinet/in.h> // Structures needed for internet domain addresses
#include <netdb.h>      // Defines structure hostnet

#include "dme.h"

// Node Controller port
#define NC_PORT 2017

// There are the hostnames expected in order to make connects
// TODO find a more dynamic approach.
static char *MY_NODES[] = {
  "Hostnames",
  "dme-1",
  "dme-2",
  "dme-3",
  "dme-4",
  "dme-5",
  "dme-6",
} ;

// message queue variable
// This is global because it will be read by multiple threads.
int msqid;

// These threads are responsible for keeping lifetime connection to other nodes.
// This will receive messages from message queue and send them out the socket.
void *sender_thread(void *arg);
// This will listen to the socket and place messages on message queue.
void *receiver_thread(void *arg);

// Used by threads to figure out what node they are connected to
int n_tot;
int *sock_fds;

// Signal thread
// This will remain blocked until a signal arrives 
// (recommended technique when dealing with threads and signals)
void *sig_waiter(void *arg);
// Signal handler function
void sig_handler(int sig);

// Error function to exit gracefully (frees resources)
void error(int error_code, char *msg, ...) { 
	va_list argptr;
	
	// Error code is used to figure out if there are resources that need to be freed.
	switch (error_code) {
		case 2:
		case 1:
		default:
			break;
	}

	// Get variable number of arguments and pass to fprintf.
	va_start(argptr, msg);
	vfprintf(stderr, msg, argptr);
	va_end(argptr);	

	exit(error_code);
}

int main(int argc, char *argv[]) { 
	// Command line arguments
	// General variables
	int n_id, i, n;
	char buffer[256];

	// Socket variables
	int sockfd_c, sockfd_l, newsockfd;
	struct sockaddr_in serv_addr, host_addr;
	struct hostent *server;

	// Signal handler variables
	sigset_t all_signals;
	struct sigaction new_act;
	int nsigs;
	int sigs[] = { SIGTERM, SIGBUS, SIGSEGV, SIGFPE };

	// Thread variables
	pthread_t thread_id;

	// Parse command line arguments
	if (argc < 3) 
		error(1, "Usage: %s node_id number_of_nodes\n", argv[0]);

	n_id  = atoi(argv[1]);
	n_tot = atoi(argv[2]);
	
	// Setup for managing signals	
	sigfillset(&all_signals);
	nsigs = sizeof(sigs) / sizeof(int);
	for ( i = 0; i < nsigs; i++ )
		sigdelset(&all_signals, sigs[i]);
	// Blocking all signals other than those listed in the signal array above.
	sigprocmask(SIG_BLOCK, &all_signals, NULL);
	sigfillset(&all_signals);
	for ( i = 0; i < nsigs; i++ ) {
		new_act.sa_handler = sig_handler;
		new_act.sa_mask    = all_signals;
		new_act.sa_flags   = 0;
		// This is saying that if the listed signals are receive, 
		// ignore any other incoming signals and run the sig_handler.
		if (sigaction(sigs[i], &new_act, NULL) == -1) {
			perror("Can't set signals :\n");
			exit(1);
		}	
	}
	// Creating a thread that will be in charge of handling signals
	if (pthread_create(&thread_id, NULL, sig_waiter, NULL) != 0) {
		fprintf(stderr, "pthread_create failed\n");
		exit(1);
	}

	// Creating message queue.
	// This will be used to relay messages from:
	// 1. Processes that want to execute a critical section to the dme thread
	// 2. The receiver thread to the dme thread
	// 3. The dme thread to process waiting to execute critical section
	// 4. The dme thread to sender thread
	if ((msqid = msgget(M_ID, (IPC_CREAT | 0600))) == -1) {
		perror("msgget failed :\n");
		exit(1);
	}
	
	// Start distributed mutual exclusion message handler.
        if (pthread_create(&thread_id, NULL, dme_msg_handler, (void *) &n_id) != 0) {
		fprintf(stderr, "pthread_create failed\n");
		exit(1);
	}

	// Begin socket connection
	// After binding the listening socket, socket connections are created for each node with a smaller node id.
	// When the connect() call is accepted, that file descriptor is added to the array,
	// and a sender/receiver thread is created to perform read()s and write()s on that fd. 
	// There will be one socket that listens for connections from other nodes with larger node ids.
	// When an accept() comes in, a receiver thread is created.
	// The sender thread looks at the message queue and write()s to the socket file descriptor matching the node the message is intended for.
	// The receiver thread reads() from the socket file descriptor and places the message in the message queue. 
	printf("Creating socket...\n");
        fflush(stdout);
	if ((sockfd_l = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		error(1, "Error on socket creation\n");

	// Set up host address
	bzero((char*) &host_addr, sizeof(host_addr));
	host_addr.sin_family      = AF_INET;
	host_addr.sin_addr.s_addr = INADDR_ANY;
	host_addr.sin_port        = htons(NC_PORT);

	if (bind(sockfd_l, (struct sockaddr *) & host_addr, sizeof(host_addr)) < 0)
		error(2, "Error on bind\n");
	
	// Setting socket to listen mode, with a backlog queue of five (max).
	listen(sockfd_l, 5);

	// Allocate array for socket file descriptors
	sock_fds = (int *) malloc(sizeof(int) * n_tot);
	for (i = 0; i < n_tot; i++)
		sock_fds[i] = -1;
	if (sock_fds == NULL) 
		error(2, "ERROR on malloc\n");

	printf("Connecting to other nodes...\n");
        fflush(stdout);
	for (i = 1; i < n_id; i++) {
		// Creating new socket for connection
		if ((sockfd_c = socket(AF_INET, SOCK_STREAM, 0)) == -1)
			error(2, "Error on socket creation\n");
		
		// Getting host information in order to connect.
		server = gethostbyname(MY_NODES[i]);

		if (server == NULL)
			error(2, "Failed on hostname resolution\n");

		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		bcopy((char *) server->h_addr, (char *) &serv_addr.sin_addr.s_addr, server->h_length);
		serv_addr.sin_port = htons(NC_PORT);

		if (connect(sockfd_c, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
			error(2, "Failed on node_controller connection.\n");

		// Once the connection has been made, provide a node id to the connected server, and read its node id.
		n = write(sockfd_c, argv[1], strlen(argv[1]));
		if (n < 0)
			error(2, "Error writing to socket\n");
		bzero(buffer, 256);
		n = read(sockfd_c, buffer, 255);
		if (n < 0)
			error(2, "Error reading from socket\n");
		n = atoi(buffer);
		printf("Connected to node %d.\n", n);
                fflush(stdout);

		sock_fds[i-1] = sockfd_c;
	        // TODO keep thread ids for closing later
		if (pthread_create(&thread_id, NULL, receiver_thread, (void *) &sock_fds[i-1]) != 0) {
			perror("Error on thread creation\n");
			exit(1);
		}	
	}

	printf("Accepting calls from other nodes...\n");
        fflush(stdout);
	for (i = n_id + 1; i <= n_tot; i++) {
		if ((newsockfd = accept(sockfd_l, NULL, NULL)) < 0)
			error(2, "Error on accept.\n");
		
		// Once connection has been made, read for the connectors node id, and respond with this node's id
		bzero(buffer, 256);
		n = read(newsockfd,buffer, 255);
		if (n < 0)
			error(2, "Error reading from socket\n");
		n = atoi(buffer);
		printf("Accepted connection from node %d.\n", n);
                fflush(stdout);
		n = write(newsockfd, argv[1], strlen(argv[1]));
		if (n < 0)
			error(2, "Error reading from socket\n");
		
		sock_fds[i-1] = newsockfd;
	        // TODO keep thread ids for closing later
		if (pthread_create(&thread_id, NULL, receiver_thread, (void *) &sock_fds[i-1]) != 0) {
			perror("Error on thread creation\n");
			exit(1);
		}	

	}
	
	// Start sender thread.
	if (pthread_create(&thread_id, NULL, sender_thread, NULL) != 0) {
		fprintf(stderr, "pthread_create failed\n");
		exit(1);
	}
	printf("Fully connected!\n");
        fflush(stdout);

	switch( fork() ) {
		case -1:
			error(2, "Error forking");
		case  0:
			sprintf(buffer, "%d", 100 / n_tot + 1); 
			execl("prod", argv[1], buffer, NULL);
	}
	// Run forever.
	for(;;);

	return 0;
}

void sig_handler(int sig) {
	// TODO
}

void *sig_waiter(void *arg) {
}

void *receiver_thread(void *arg) {
	// Receiver gets message from socket and places it in the message queue. 
	int sockfd = *((int *) arg);
	int i, x, node = -1;
	char header;
	MSG qmsg; 

	// Figure out which node this is the receiver thread for.
	// This is mostly for logging purposes.
	for (i = 0; i < n_tot; i++)
		if (sock_fds[i] == sockfd) {
			node = i+1;
			break;
		}
	if (node == -1)
		error(0, "Sockfd error\n");
	printf("Receiver thread for node %d started with %d\n", node, sockfd);
	fflush(stdout);

	for (;;) {
		// Read messages from socket.
		// Each message has a header that tells the number of bytes in the actual message.
		if ((x = read(sockfd, &header, 1)) == -1) {
			fprintf(stderr, "%s\n", strerror(errno));
			error(0, "Error on read\n");
		}

		if (x == 0)
			error(0, "EOF on socket\n");

		qmsg.size = header;
		printf("Receiving message from %d of size %d\n", node, qmsg.size);
		for (i = 0; i < qmsg.size; i++) {
			if ((x = read(sockfd, &qmsg.buf[i], 1)) == -1) {
				fprintf(stderr, "%s\n", strerror(errno));
				error(0, "Error on read\n");
			}
            printf("%d ", qmsg.buf[i]);
			if (x == 0)
				error(0, "EOF on socket\n");
		}
		printf("Message from %d received\n", node);
		fflush(stdout);
		
		qmsg.type = TO_DME;
		// This is used to check whether the structure needs its byte order to be changed. 
		qmsg.network = 1;
		// Place message on message queue for distributed mutual exclusion algorithm to process.
		// NOTE: the dme thread that receives this 
		// will have to convert from network byte order to host byte order (htohl)
		if (msgsnd(msqid, &qmsg, sizeof(MSG), 0) == -1)
			error(0, "Error in message queue\n");
	}
	return NULL;
}

void *sender_thread(void *arg) {
	// Sender thread listens to the message queue, and then writes its message to all sockets.
	// Before actually sending the message, a header must be sent specifying the message size, and type (producer vs. consumer)
	// Assuming that the dme sender thread handles converting from hardware byte order to network byte order.
	int i,j;
	MSG omsg;

	for (;;) {
		// Blocks until a message for sending is received from the queue
		// First a message stating what type of process is going to send a request is received,
		// Then the actuall message will be given. 
		// The ordering is guaranteed because only the dme_sender thread is writing to this message queue.
		if (msgrcv(msqid, &omsg, sizeof(MSG), TO_SND, 0) == -1)
			error(0, "NC: Error on message queue receive\n");

		printf("SENDER: message received of size %d\n", omsg.size);
		fflush(stdout);

		for (i = 0; i < n_tot; i++) {
			if (sock_fds[i] == -1)
				continue;
			if (write(sock_fds[i], &omsg.size, 1) == -1)
				error(0, "Error on write\n");
			for (j = 0; j < omsg.size; j++)
				if (write(sock_fds[i], &omsg.buf[j], 1) == -1)
					error(0, "Error on write\n");
		}
	}
	return NULL;
}
