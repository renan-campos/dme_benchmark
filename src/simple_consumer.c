#include <unistd.h>
#include <stdio.h>

#include "dme.h"

int main(int argc, char* argv[]) { 
	int i;

	for (i = 0; i < 10; i++) {
        printf("CONSUMER: Creating request %d\n", i);
        fflush(stdout);
		dme_down();
		sleep(5);
		dme_up();
	}
	return 0;
}
