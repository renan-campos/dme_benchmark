#include <unistd.h>

#include "dme.h"

int main(int argc, char* argv[]) { 
	int i;

	for (i = 0; i < 100; i++) {
		dme_down();
		sleep(5);
		dme_up();
	}
	return 0;
}
