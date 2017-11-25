#ifndef _DME
#define _DME
// This declares the functions needed to perform distributed mutual exclusion
// The inner workings of these functions depend on the implementation (With general guidelines)
// The one that will be used on compilation is dependent on link flags.

// Message queue ID used by dme algorithm
#define M_ID 2017

// Message type convention
#define TO_DME 1
#define TO_CON 2
#define TO_SND 3

// Message queue message definition
typedef struct dme_message {
	long type;            // TO_SNDR | TO_DME | TO_CONS
	char size;            // Size of dme message
	char network;         // Used by the dme thread to determine whether to convert byte order
	char buf[255];        // Contains the dme data structure. NOTE: limited to 255 bytes. 
} MSG;

// This is the thread that is created by the node controller to process requests sent to the message queue.
// First it initializes all of the global structures needed for its implemenetation. 
// It then listens for messages from consumers (via dme_down and dme_up) or the node controller's receiver thread.
// The messages should have the type TO_DME
// Its internal state is updated after receiving messages,
// and it can send messages to the consumers (causing dme_down or dme_up to unblock) or to the sender thread.
// Messages to consumers will have type TO_CON
// Messages to sender will have type TO_SND
// When sending to sender, message must be in network byte order.
// Its argument is the current node id.
void *dme_msg_handler(void *arg);

// This function is called when a process would like to use a critical section.
// It places a message in the queue for the dme_msg_handler to process...
// ...Then blocks until a message is received back. 
// Messages sent will be of type TO_DME
// Messages received will be of type TO_CON
void dme_down();

// This function is called when a process has completed its critical section.
// It places a message in the queue for the dme_msg_handler to process...
// ... Then blocks until message is received back (if necassary).
// Messages sent will be of type TO_DME
// Messages received (if necassary) will be of type TO_CON
void dme_up();

#endif
