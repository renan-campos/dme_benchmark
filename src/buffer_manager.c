#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <pthread.h>

#define PORTNO 1992
#define BSIZE  100

struct msg {
	int node_id;
	int donut_number;
};

struct msg buffer[BSIZE];
int buf_indx;

void error(char *msg) {
	perror(msg);
	exit(1);
}

void *handler(void *arg);

int main(int argc, char *argv[]) {
	int i, batch = 0;
	int sockfd, newsockfd, portno, clilen;
	struct sockaddr_in serv_addr, cli_addr;
	pthread_t thread_ID;

	// Creating socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		error("ERROR opening socket.");

	// Binding socket
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(PORTNO);
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
		error("ERROR on binding");
	listen(sockfd, 5);

	clilen = sizeof(cli_addr);
	while(1) {
        // Reset buffer index
        buf_indx = 0;
        // Listen for connections, create thread on accept.
        while (buf_indx < BSIZE-1) {
            newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
            pthread_create(&thread_ID, NULL, handler, (void *) &newsockfd);
        }

        // Wait for final thread to finish
        pthread_join(thread_ID, NULL);

        // Print content of buffer
        printf("------ Start Batch %d ------\n", batch);
        for (i = 0; i < BSIZE; i++)
            printf("NODE: %4d DONUT: %4d\n", buffer[i].node_id, buffer[i].donut_number);
        printf("------ End Batch %d ------\n", batch++);
        fflush(stdout);
    }

	return 0;
}

void *handler(void *arg) {
	// Cast the parameter into what is needed.
	int sockfd = *((int *) arg);
	int n;

	n = read(sockfd, &buffer[buf_indx], sizeof(struct msg));
	
	if (n < 0) error("ERROR reading from socket");
	
	// Sleeping to ensure corruption happens if no mutual exclusion.
	// Donut is placed in the buffer, but before the index is increased, another
	// node places a donut in the same buffer, then the index is increased twice. 
	usleep(5000);
	buf_indx++;
	
	n = write(sockfd, &buf_indx, sizeof(int));
	//if (n < 0) error("ERROR writing to socket");
	if (n < 0) printf("Warning: could not write to node\n");
    fflush(stdout);
}
