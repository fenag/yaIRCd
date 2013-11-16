#ifndef __IRC_CLIENT_QUEUE_GUARD__
#define __IRC_CLIENT_QUEUE_GUARD__
#include <pthread.h>
/** @file
	@brief Client's messages queue management functions

	This file provides a module that knows how to operate on a client's messages queue. Every function is reentrant and thread-safe.
	It is easy for a client's thread to wake up and read incoming data using an IO watcher. However, sporadically, we also need to wake up a client's thread to write to his socket.
	For example, if user A PRIVMSGs user B, user A's thread must be able to somehow inform user B thread that something needs to be sent to user B.
	To do so, we use an async watcher. An async watcher allows an arbitrary thread X to wake up another thread Y. Thread Y must be running an events loop and must have initialized and started an async watcher.
	An async watcher works pretty much the same way as an IO watcher, but libev's documentation explicitly states that queueing is not supported. In other words, if more async messages arrive when we are processing
	an async callback, these will be silently discarded. To avoid losing messages like this, we implement our own messages queueing system.
	Each client holds a queue of messages waiting to be written to his socket. These messages can originate from any thread.
	
	Every operation in a client's queue shall be invoked through the use of the functions declared in this file, to ensure thread safety.
	
	@author Filipe Goncalves
	@date November 2013
*/

/** Defines the queue size. The queue size determines how many messages are allowed to be on hold waiting to be written to the client's socket.
	Each client's queue is writable by any other client's thread that wishes to deliver a message to this client. Queue operations are thread safe and reentrant.
*/
#define WRITE_QUEUE_SIZE 32

/** The structure that holds a queue */
struct msg_queue {
	char *messages[WRITE_QUEUE_SIZE]; /**<a queue with messages */
	int top; /**<index denoting the position where a new element will be inserted in `messages`. Will always be less than `WRITE_QUEUE_SIZE` */
	int bottom; /**<index denoting the position where the least recent element is located. This is the element that will be dequeued in the next dequeue operation. */
	int elements; /**<indicates how many elements are stored in this queue at the moment. */
	pthread_mutex_t mutex; /**<a mutex to coordinate concurrent access to a queue. */
};

/* Documented in client_queue.c */
int client_queue_init(struct msg_queue *queue);
int client_queue_destroy(struct msg_queue *queue);
int client_enqueue(struct msg_queue *queue, char *message);
char *client_dequeue(struct msg_queue *queue);
int client_is_queue_empty(struct msg_queue *queue);
void client_queue_foreach(struct msg_queue *queue, void (*f)(char *, void *), void *args);

#endif /* __IRC_CLIENT_QUEUE_GUARD__ */
