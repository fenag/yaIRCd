#include <stdlib.h>
#include <string.h>
#include "client.h"
#include "write_msgs_queue.h"
#include "msgio.h"
/** @file
   @brief Client's messages write queue management functions
   This file provides a module that knows how to operate on a client's messages write queue.
   It is easy for a client's thread to wake up and read incoming data using an IO watcher. However, sporadically, we
      also need to wake up a client's thread to write to his socket.
   For example, if user A PRIVMSGs user B, user A's thread must be able to somehow inform user B thread that something
      needs to be sent to user B.
   To do so, we use an async watcher. An async watcher allows an arbitrary thread X to wake up another thread Y. Thread
      Y must be running an events loop and must have initialized and started an async watcher.
   An async watcher works pretty much the same way as an IO watcher, but libev's documentation explicitly states that
      queueing is not supported. In other words, if more async messages arrive when we are processing an async callback,
      these will be silently discarded. Or, if multiple async requests are issued by different threads, they may all
	  be packed in a single callback. To avoid losing messages like this, we implement our own messages queueing
      system.
   Each client holds a queue of messages waiting to be written to his socket. These messages can originate from any
      thread.
   Every operation in a client's queue shall be invoked through the use of the functions declared in this file.
   @author Filipe Goncalves
   @date November 2013
 */

/** Initializes a queue. This function is typically called when a new client is created.
   No queue insertions or deletions can be performed before initializing a queue.
   @param queue The queue to initialize.
   @return `0` on success; `-1` if there aren't enough resources to initialize a queue.
   @warning Undefined behavior will occur if queue operations are invoked in a non-initialized queue.
 */
int client_queue_init(struct msg_queue *queue)
{
	queue->top = 0;
	queue->bottom = 0;
	queue->elements = 0;
	return pthread_mutex_init(&queue->mutex, NULL);
}

/** Destroys a queue. This function is typically called when a client is exiting and is about to be destroyed.
   @param queue The queue to destroy.
   @return `0` on success, `-1` if the destroy operation was not successfull. The error condition should never happen if
      this module is used properly and no race conditions occur. If `-1` is returned, it means something went terribly
      wrong with the thread synchronization mechanism used in a queue. For example, maybe a queue destroy operation was
      invoked whena lock to this queue was active.
   @warning Queue destroy operations must be performed carefully to avoid race conditions. The code that uses this
      module must ensure that after a queue is destroyed, no other thread will try to insert new messages in this queue.
      The program shall consider the case that a thread successfully destroys a queue, and is immediately interrupted
      after that, switching to another thread that was stopped precisely in an enqueue operation. This situation cannot
      occur; the IRCd will fail miserably and crash if it happens.
 */
int client_queue_destroy(struct msg_queue *queue)
{
	int i;
	int j;
	for (i = queue->bottom, j = 0; j < queue->elements; i = (i + 1) % WRITE_QUEUE_SIZE, j++) {
		free(queue->messages[i]);
	}
	return pthread_mutex_destroy(&queue->mutex);
}

/** Inserts a new message in a queue.
   @param queue The target queue where the message shall be written to.
   @param message A null terminated characters sequence to enqueue. A fresh new copy of `message` is performed using
      `strdup()`, to ensure that the characters sequence lives for as long as it is needed. The caller of this function
      need not worry about allocating and freeing resources, this module will take care of that.
   @return `0` on success; `-1` if there is no space left in this client's queue, or if there's no memory to perform a
      fresh copy of the message.
 */
int client_enqueue(struct msg_queue *queue, char *message)
{
	char *str;
	pthread_mutex_lock(&queue->mutex);
	if ((str = strdup(message)) == NULL || queue->elements == WRITE_QUEUE_SIZE) {
		pthread_mutex_unlock(&queue->mutex);
		return -1;
	}
	queue->messages[queue->top] = str;
	queue->top = (queue->top + 1) % WRITE_QUEUE_SIZE;
	queue->elements++;
	pthread_mutex_unlock(&queue->mutex);
	return 0;
}

/** Dequeues a message previously enqueued. Dequeue operations follow a FIFO policy.
   @param queue The target queue to extract the message.
   @return Pointer to a null terminated characters sequence previously inserted in this queue; `NULL` if there are no
      elements. The pointer returned must be freed by the caller after it is no longer needed.
   @warning A memory leak will occur if the caller does not free the pointer returned when it no longer needs it.
 */
char *client_dequeue(struct msg_queue *queue)
{
	char *ptr;
	pthread_mutex_lock(&queue->mutex);
	if (queue->elements == 0) {
		pthread_mutex_unlock(&queue->mutex);
		return NULL;
	}
	ptr = queue->messages[queue->bottom];
	queue->bottom = (queue->bottom + 1) % WRITE_QUEUE_SIZE;
	queue->elements--;
	pthread_mutex_unlock(&queue->mutex);
	return ptr;
}

/** Determines if a queue is empty.
   @param queue The queue to examine.
   @return `0` if the queue is not empty; `1` if the queue is empty.
 */
inline int client_is_queue_empty(struct msg_queue *queue)
{
	int ret;
	pthread_mutex_lock(&queue->mutex);
	ret = (queue->elements == 0);
	pthread_mutex_unlock(&queue->mutex);
	return ret;
}

/** Function used when a client wants to flush his messages write queue.
	This will destructively iterate through a queue for a given client, writing every pending message
	to this client's socket.
	@param client Target client.
	@param queue The queue to flush.
 */
void flush_queue(struct irc_client *client, struct msg_queue *queue)
{
	/* We ignore possible errors that may arise during a write. We use write_to(), not write_to_noerr(),
	   because write_to_noerr() calls pthread_exit() if things go wrong, and we can't call pthread_exit() here,
	   since we're holding a lock to a queue. Things would fall apart if we did so.
	 */
	int i;
	int j;
	pthread_mutex_lock(&queue->mutex);
	for (i = queue->bottom, j = 0; j < queue->elements; j++, i = (i + 1) % WRITE_QUEUE_SIZE) {
		(void)write_to(client, queue->messages[i], strlen(queue->messages[i]));
		free(queue->messages[i]);
	}
	queue->bottom = queue->top = queue->elements = 0;
	pthread_mutex_unlock(&queue->mutex);
}

