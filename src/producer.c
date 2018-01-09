#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "dme.h"

#define PORTNO 1992
#define BSIZE  100

// Assuming buffer manager container hostname
#define BUFFMAN "dme_bm"

struct msg {
	int node_id;
	int donut_number;
};

void error(char *msg) {
	perror(msg);
	exit(1);
}

int main(int argc, char *argv[]) {
	int sockfd, portno, n;
	struct sockaddr_in serv_addr;
	struct hostent *server;
	int i, msgs, node_id, num;
	struct msg donut;

	void *handle;
	void (*dme_up)(void);
	void (*dme_down)(void);

	// Determining node id
	node_id = atoi(argv[1]);
	// Determining number of messages needed to send
	msgs = atoi(argv[2]);
	// Open shared library and define functions functions
	handle = dlopen(argv[3], RTLD_LAZY);
	if (!handle) {
		fprintf(stderr, "%s\n", dlerror());
		exit(1);
	}
	dme_up = dlsym(handle, "dme_up");
	if (dlerror() != NULL) {
		fprintf(stderr, "%s\n", dlerror());
		exit(1);
	}
	dme_down = dlsym(handle, "dme_down");
	if (dlerror() != NULL) {
		fprintf(stderr, "%s\n", dlerror());
		exit(1);
	}
	

	for (i = 0; i < msgs; i++) {
        // Setting up socket to make connections.
        portno = PORTNO;
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
            error("ERROR opening socket");
        server = gethostbyname(BUFFMAN);
        if (server == NULL) {
            fprintf(stderr, "ERROR, no such host\n");
            exit(0);
        }
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        bcopy((char *)server->h_addr,
                (char *)&serv_addr.sin_addr.s_addr,
                server->h_length);
        serv_addr.sin_port = htons(portno);
		
        usleep(5);

        // Get distributed mutex in order to run critical section
		(*dme_down)();

		// Connecting to buffer manager
		if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
			error("ERROR connecting");
		
		// Send donut to buffer manadger
		donut.node_id      = node_id;
		donut.donut_number = i;

		n = write(sockfd, &donut, sizeof(struct msg));
		if (n < 0)
			error("ERROR writing from socket");
		
		// Get response from buffer manager
		n = read(sockfd, &num, sizeof(int));
		if (n < 0)
			error("ERROR reading from socket");
		
		printf("PROD: Provided buffer manager with donut #%d\n", num);
		fflush(stdout);

        close(sockfd);
        sockfd = -1;

		// Free distributed mutext lock
		(*dme_up)();
	}

	dlclose(handle);

	return 0;
}
