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

// Each distributed lock needs an id. 
#define DME_ID 2009

// Node Controller port
#define NC_PORT 2017

// Message queue ID TODO Add to global file
// The msg type convention for the message queue also needs to be in global.h
// Along with the shared memory and semaphore i.d.s... 
// or all of this can be added to dme.h
#define M_ID 2017

// message queue variable
// This is global because it will be read by multiple threads.
int msqid;

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
	// 1. Processes that want to execute a critical section to the dme algorithm
	// 2. The dme algorithm threads to the sender thread
	// 3. A node's receiver thread to the dme algorithm
	if ((msqid = msgget(M_ID, (IPC_CREAT | 0600))) == -1) {
		perror("msgget failed :\n");
		exit(1);
	}
	
	// initialize distributed mutual exclusion algorithm
        // TODO

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
		if (pthread_create(&thread_id, NULL, receiver_thread, (void *) (long) i) != 0) {
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
		if (pthread_create(&thread_id, NULL, receiver_thread, (void *) (long) i) != 0) {
			perror("Error on thread creation\n");
			exit(1);
		}	

	}
	printf("Fully connected!\n");
        fflush(stdout);

	// Closing message queue
        if (msgctl(msqid, IPC_RMID, 0) == -1) {
		perror("msgctl failed :\n");
		exit(1);
	}

	return 0;
}

void sig_handler(int sig) {
	// TODO
}

void *sig_waiter(void *arg) {
}

void *receiver_thread(void *arg) {
}

void *sender_thread(void *arg) {
}