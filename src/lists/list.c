#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include "list.h"
#include "trie.h"

/** @file
   @brief Generic thread-safe words container.
   This file implements a generic thread-safe words container. It uses an underlying trie implementation, and a mutex to
      control concurrent accesses to this trie.
   Thus, the functions provided here are nothing more than the same functions offered by the trie implementation, except
      that everything is wrapped in a structurewith accesses controlled by a mutex.
   Two core functions, `list_find_and_execute()`, and `list_find_and_execute_globalock()`, are provided to perform
      atomic arbitrary operations on the list items. Besides the global locking mechanism,each node added to a list is
      packed with a lock just for itself. This allows multiple threads to work on different list elements instead of
      having to wait for the global lock to become available even if theirtarget node was not being used.
      `list_find_and_execute()` uses this approach to allow different threads to work on different nodes concurrently,
      but it shall only be called with functions that won't invoke any deletion or insertion operations. This is
      necessary because additions and insertions to the list must be controlled by the global locking mechanism, and
      these functions are not called with a global lock acquired. `list_find_and_execute_globalock()` removes this
      limitation at the cost of executing the whole operation while holding the global lock. Thus, it is safe for
      functions to invoke a delete operation on the node they're workingon (and only on that node), and the function
      guarantees that no other threads are waiting to acquire the node's lock once it is deleted in the current thread.
      See this function's documentation to learn how this is achieved.
   It is a very interesting and recommendable exercise to go through the trie implementation. Interested readers are
      invited to look at trie.c.
   @author Filipe Goncalves
   @date November 2013
   @see trie.c
 */

/** Structure defining a generic thread-safe words list. */
struct yaircd_list {
	pthread_mutex_t mutex; /**<Mutex to synchronize concurrent access to this list */
	struct trie_t *trie; /**<The underlying list implementation. A trie is used to associate words to data. */
	void (*free_func)(void *); /**<Pointer to a function that knows how to free a generic data type stored in this
	                             list by the code using this module. */
};

/** This is what we associate to each word stored. Each word is denoted a node; the trie allows association of a generic
   data type with a word.
   Every word is associated to an instance of this structure, so that we can have more parallelism with functions like
      `list_find_and_execute()`.
   This is possible because each node stored holds its own mutex, thus, functions that can't possibly add or remove an
      element from the list are freeto execute concurrently with other threads accessing different elements from the
      list. A naive implementation would use a single mutex for the whole list,this would quickly become a bottleneck
      because `list_find_and_execute()` would have to perform a global lock to execute an action in a single element,
      while other threads wanting to mess withcompletely different nodes would have to wait anyway.
 */
struct yaircd_node {
	void *data; /**<Generic data type stored at this node; provided by the upper caller */
	pthread_mutex_t mutex; /**<Mutex to synchronize concurrent access to this specific node */
};

/** Wrapper structure to hold arguments to pass to `free_yaircd_node()`. See `destroy_word_list()` and `list_delete()`
  for further information. */
struct destroy_args_wrapper {
	int free_data; /**<Did the original caller want to free data? This will either be `LIST_FREE_NODE_DATA` or
	                 `LIST_NO_FREE_NODE_DATA`. */
	void (*free_func)(void *); /**<Original freeing function passed to `init_word_list()`. */
};

/** Function that knows how to delete a trie's node.
   @param node_generic This will always be a pointer to `struct yaircd_node` corresponding to the element being deleted.
   @param arg This will always be of type `struct destroy_args_wrapper `, and it holds important information coming from
      the code above, indicating if the generic data stored in a node by the upper caller is to be freed.
 */
static void free_yaircd_node(void *node_generic, void *arg)
{
	struct yaircd_node *node = (struct yaircd_node*)node_generic;
	struct destroy_args_wrapper *args = (struct destroy_args_wrapper*)arg;
	pthread_mutex_destroy(&node->mutex);
	if (args != NULL && args->free_data == LIST_FREE_NODE_DATA) {
		(*args->free_func)(node->data);
	}
	free(node);
}

/** Initializes a new, empty list, with the necessary structures to control concurrent thread access.
   @param free_function Each word can be associated to a generic pointer hereby denoted `data`. This function will be
      called to free a node's `data` when it is removed from the list. It can be NULL if nothing shall be done when
      deleting a node. See `destroy_word_list()` and `list_delete()` for further info.
   @param is_valid Pointer to function that returns `1` if a char is part of this wordlist's alphabet; `0` otherwise.
   @param pos_to_char Pointer to function that converts an index position from `edges` back to its character
      representation.
   @param char_to_pos Pointer to function that converts a character `s` into a valid, unique position.
   @param charcount How many characters compose this list's alphabet.
   @return A pointer to a new, empty word list instance, or `NULL` is there weren't enough resources to create a new
      list.
 */
Word_list_ptr init_word_list(void (*free_function)(void *), int (*is_valid)(char), char (*pos_to_char)(
				     int), int (*char_to_pos)(char), int charcount)
{
	Word_list_ptr new_list;

	new_list = malloc(sizeof(struct yaircd_list));
	if (new_list == NULL) {
		return NULL;
	}
	if (pthread_mutex_init(&new_list->mutex, NULL) != 0) {
		free(new_list);
		return NULL;
	}
	new_list->free_func = free_function;
	if ((new_list->trie = init_trie(free_yaircd_node, is_valid, pos_to_char, char_to_pos, charcount)) == NULL) {
		pthread_mutex_destroy(&new_list->mutex);
		free(new_list);
		return NULL;
	}
	return new_list;
}

/** Destroys a word list after it is no longer needed. Every resources previously allocated for this list are freed.
   @param list Pointer to the list structure that shall be freed.
   @param free_data `LIST_FREE_NODE_DATA` if the freeing function for a node's data shall be called for each node while
      destroying the list; `LIST_NO_FREE_NODE_DATA` if nothing is to me done regarding each node's data.
   @warning This function cannot be called when other threads are possibly reading from or writing to the list.
   @warning If `free_data` is `LIST_FREE_NODE_DATA`, then it is assumed that `free_function` pointer previously passed
      to `init_word_list` is a valid pointer to a function.
 */
void destroy_word_list(Word_list_ptr list, int free_data)
{
	struct destroy_args_wrapper args;
	args.free_data = free_data;
	args.free_func = list->free_func;
	destroy_trie(list->trie, TRIE_FREE_DATA, (void*)&args);
	if (pthread_mutex_destroy(&list->mutex) != 0) {
		perror("::list.c:destroy_word_list(): Could not destroy mutex");
	}
}

/** Finds a given word, and returns the data associated with this word, if a match was found.
   This function does not lock the global list mutex, it should only be called from within a function that is holding a
      lock.
   @param list The list to perform the search on
   @param word A pointer to a null terminated characters sequence holding the word to search for
   @return `NULL` if no match was found, or word contains invalid characters according to the `is_valid()` function
      pointer previously passed to `init_word_list()`. Otherwise, a pointer the data associated with this word is
      returned.
   @see list_find_and_execute()
 */
void *list_find_word_nolock(Word_list_ptr list, char *word)
{
	return ((struct yaircd_node*)find_word_trie(list->trie, word))->data;
}

/** Finds a given word, and returns the data associated with this word, if a match was found.
   @param list The list to perform the search on
   @param word A pointer to a null terminated characters sequence holding the word to search for
   @return `NULL` if no match was found, or word contains invalid characters according to the `is_valid()` function
      pointer previously passed to `init_word_list()`. Otherwise, a pointer the data associated with this word is
      returned.
   @warning Remember that only the search is atomic. The upper code using this module should be aware of the possibility
      of multiple threads holding pointers to the same data structure and performing concurrent work. See
      `list_find_and_execute()` for a possible workaround for this issue.
   @see list_find_and_execute()
 */
void *list_find_word(Word_list_ptr list, char *word)
{
	void *ret;
	pthread_mutex_lock(&list->mutex);
	ret = list_find_word_nolock(list, word);
	pthread_mutex_unlock(&list->mutex);
	return ret;
}

/** Finds and performs an action on a word's data atomically, if a match exists.
   This is the magical function that allows parallelism and locking at the same time. First, the global lock is obtained
      to search the list for a match. If a match is not found, `nomatch_fun` is called, and then theglobal lock is
      released and the function returns.
   If a match is found, this function guarantees that a unique lock attached to the match is obtained, and then the
      global list lock is released. Then, `match_fun` is called with the datapreviously associated to `word`. As a
      consequence, this function is atomically executed with respect to the specific structure associated to `word`.
      When that function returns, this unique lock is released, and thisfunction returns.
   This means that multiple threads don't necessarily have to wait for each other when they want to do some work on
      different list nodes. However, if a thread is doing some work on node B, and another thread comes in andasks for
      node B, it will have to wait for the former thread to finish processing, and it will do so while holding the
      global list lock. That is, if two threads concurrently try to access the same node, one of them willhave to wait,
      and will force every other thread trying to access ANY node to wait.
   This is necessary; we can't obtain the unique lock for a node without holding a global lock, otherwise, the
      unfortunate situation in which we release the global lock; another thread deletes node B, and then we get back and
      try to lock node B could arise, and we would be in very big trouble.
   User supplied functions (`match_fun` and `nomatch_fun`) are allowed to be `NULL`, in that case, the corresponding
      pointer to the function is ignored.
   When `word` was not found in the list, `nomatch_fun` is called with `nomatch_fargs` while holding a global list lock.
   When `word` is found, a unique lock associated to `word` is obtained, the global lock is released, and `match_fun` is
      called with the generic data that was previously associated to `word` as its first parameter, and with
      `match_fargs` as second parameter.
   This function should only be called with a `match_fun` that can't possibly invoke list operations that will add or
      delete elements from the list.
   On the other hand, since `nomatch_fun` is called while holding the global lock, it is safe to perform other list
      operations, as far as it uses operations that do not lock (`list_find_word_nolock()` and others).
   @param list The list to perform the search on.
   @param word A pointer to a null terminated characters sequence holding the word to search for.
   @param match_fun A pointer to a function returning a generic pointer that shall be called if a match is found. Under
      such scenario, the function is called while holding a unique lock associated to the node found, but without
      holding a global lock to the list, to increase parallelism. The first parameter passed to this function is the
      matching node's data, and the second is `match_fargs`. Because it does not hold a global lock to the list, this
      function shall not invoke other list operations, especially add or remove.
   @param nomatch_fun A pointer to a function returning a generic pointer that shall be called if a match is not found.
      Under such scenario, the function is called while holding the global list lock, and it is passed
        `nomatch_fargs`.
   @param match_fargs This will be passed to `match_fun` as a second parameter when a match is found.
   @param nomatch_fargs This will be passed to `nomatch_fun` when a match is not found.
   @param success After this function returns, `success` will hold `1` if `(match_fun)()` was called, otherwise it will
      hold `0`. This allows the caller to have `(match_fun)()` or `(nomatch_fun)()` returning `NULL` and still
      distinguish between a successfull and failed match.
   @return
   <ul>
   <li>If no match is found and `nomatch_fun` is `NULL`, then `NULL` is returned.</li>
   <li>If no match is found and `nomatch_fun` is not `NULL`, then the result of evaluating
      `(nomatch_fun)(nomatch_fargs)` is returned.</li>
   <li>If a match is found and `match_fun` is `NULL`, then `NULL` is returned.</li>
   <li>If a match is found and `match_fun` is not `NULL`, then the result of evaluating `(match_fun)(node_data,
      match_fargs)` is returned.</li>
   </ul>
   @warning The functions must not call `pthread_exit()`.
   @warning Read the documentation carefully, and make sure to understand which locks are active inside `match_fun` and
      `nomatch_fun`. It is easy to create deadlock situations when not paying attention.
   @note `nomatch_fun` is free to add or delete elements from the list, since it will be executing inside a globally
      atomic block. Keep in mind that `list_add_nolock()` and `list_delete_nolock()` must be used instead of the regular
      functions. On the other hand, `match_fund` must not add or delete elements from the list, since `match_fun` is
      executed without holding a global lock for the list. See `list_find_and_execute_globalock()` for a possible
       workaround.
 */
void *list_find_and_execute(Word_list_ptr list,
			    char *word,
			    void *(*match_fun)(void *, void *),
			    void *(*nomatch_fun)(void*),
			    void *match_fargs,
			    void *nomatch_fargs,
			    int *success)
{
	void *ret;
	struct yaircd_node *node;
	*success = 0;
	pthread_mutex_lock(&list->mutex);
	ret = find_word_trie(list->trie, word);
	if (ret == NULL) {
		ret = (nomatch_fun != NULL ? (*nomatch_fun)(nomatch_fargs) : NULL);
		pthread_mutex_unlock(&list->mutex);
		return ret;
	}
	node = (struct yaircd_node*)ret;
	pthread_mutex_lock(&node->mutex);
	pthread_mutex_unlock(&list->mutex);
	ret = (match_fun != NULL ? (*match_fun)(node->data, match_fargs) : NULL);
	pthread_mutex_unlock(&node->mutex);
	*success = 1;
	return ret;
}

/** Similar to `list_find_and_execute()`, except that both `match_fun` and `nomatch_fun` are executed while holding a
   global lock. This must be used everytime `match_fun` can possibly add or delete items from the list.
   First, a global lock for the list is obtained. The list is searched for `word`.
   When a match is found, it is guaranteed that `match_fun` is called with the data previously associated to `word` and
      with `match_fargs` only when no other thread is working on the same node.
   This holds true even for threads that may be working on this node without holding a global lock. It is safe for
      `match_fun` to commit suicide, i.e., delete the node it's working on, as far as it uses `list_delete_nolock()`.
   No other deletion operations are allowed to be performed by `match_fun` other than deleting the node it's working on.
      To do so, it must call `list_delete_nolock()`.
   We use a clever little trick to ensure both atomicity on this node and the capability to delete itself: after
      acquiring the global lock, a unique lock to the node is acquired and then immediately released; by the time it is
      released,	no other thread can be working on this node, and since the global lock is with us, no one else is stuck
      trying to lock this specific node. Also, because we released the node's lock, it is safe for the node to destroy
      itself.
   If a match is not found, `nomatch_fun` is called with `nomatch_fargs` while holding the global list lock.
   @param list The list to perform the search on.
   @param word A pointer to a null terminated characters sequence holding the word to search for.
   @param match_fun A pointer to a function returning a generic pointer that shall be called if a match is found under
      the scenario described above.
   @param nomatch_fun A pointer to a function returning a generic pointer that shall be called if a match is not found
      under the scenario described above.
   @param match_fargs This will be passed to `match_fun` as a second parameter when a match is found.
   @param nomatch_fargs This will be passed to `nomatch_fun` when a match is not found.
   @param success After this function returns, `success` will hold `1` if `(match_fun)()` was called, otherwise it will
      hold `0`. This allows the caller to have `(match_fun)()` or `(nomatch_fun)()` returning `NULL` and still
      distinguish between a successfull and failed match.
   @return
   <ul>
   <li>If no match is found and `nomatch_fun` is `NULL`, then `NULL` is returned.</li>
   <li>If no match is found and `nomatch_fun` is not `NULL`, then the result of evaluating
      `(nomatch_fun)(nomatch_fargs)` is returned.</li>
   <li>If a match is found and `match_fun` is `NULL`, then `NULL` is returned.</li>
   <li>If a match is found and `match_fun` is not `NULL`, then the result of evaluating `(match_fun)(node_data,
      match_fargs)` is returned.</li>
   </ul>
   @warning Deletion operations inside `match_fun` are only allowed for itself, i.e., `match_fun` cannot delete a node
      that is not the same node with which it is currently working. Ignoring this will lead to race conditions, and all
      bets are off.
 */
void *list_find_and_execute_globalock(Word_list_ptr list,
				      char *word,
				      void *(*match_fun)(void *, void *),
				      void *(*nomatch_fun)(void*),
				      void *match_fargs,
				      void *nomatch_fargs,
				      int *success)
{
	void *ret;
	struct yaircd_node *node;
	*success = 0;
	pthread_mutex_lock(&list->mutex);
	ret = find_word_trie(list->trie, word);
	if (ret == NULL) {
		ret = (nomatch_fun != NULL ? (*nomatch_fun)(nomatch_fargs) : NULL);
		pthread_mutex_unlock(&list->mutex);
		return ret;
	}
	node = (struct yaircd_node*)ret;
	/* Make sure no threads without the global lock are working on this node */
	pthread_mutex_lock(&node->mutex);
	/* By this point, we hold:
	        - The global lock
	        - The lock for this node
	   Nobody is able to access the list and grab this node; we can unlock node->mutex, and in fact we need to,
	   since match_fun can commit suicide (i.e. delete the node it's working on)
	 */
	pthread_mutex_unlock(&node->mutex);
	ret = (match_fun != NULL ? (*match_fun)(node->data, match_fargs) : NULL);
	pthread_mutex_unlock(&list->mutex);
	*success = 1;
	return ret;
}

/** Adds a new word to a list if that word is not stored in the list yet without obtaining any lock.
   This funtion should only be called by anyone holding a lock for the list.
   @param list The list to add the word to.
   @param data Pointer to the new node's data. Cannot be `NULL`.
   @param word A null terminated characters sequence that will be associated to `data`.
   @return <ul>
   <li>`0` on success</li>
   <li>`LST_INVALID_WORD` if the word contains invalid characters, in which case nothing was added to the list</li>
   <li>`LST_NO_MEM` if there isn't enough memory to create a new entry</li>
   <li>`LST_ALREADY_EXISTS` if there's a known entry using `word`</li>
   </ul>
 */
int list_add_nolock(Word_list_ptr list, void *data, char *word)
{
	int ret;
	struct yaircd_node *new_node;
	new_node = malloc(sizeof(*new_node));
	ret = 0;
	if (new_node == NULL) {
		return LST_NO_MEM;
	}
	if (pthread_mutex_init(&new_node->mutex, NULL) != 0) {
		free(new_node);
		return LST_NO_MEM;
	}
	new_node->data = data;
	if (find_word_trie(list->trie, word) != NULL) {
		return LST_ALREADY_EXISTS;
	}else  {
		return add_word_trie(list->trie, word, new_node);
	}
	if (ret == TRIE_INVALID_WORD || ret == TRIE_NO_MEM || ret == LST_ALREADY_EXISTS) {
		pthread_mutex_destroy(&new_node->mutex);
		free(new_node);
	}
	return ret == TRIE_INVALID_WORD ? LST_INVALID_WORD : ret == TRIE_NO_MEM ? LST_NO_MEM : ret;
}

/** Atomically adds a new word to a list if that word is not stored in the list yet.
   This operation is thread safe and guaranteed to be free of race conditions. The search and add operations are
      executed atomically.
   @param list The list to add the word to.
   @param data Pointer to the new node's data. Cannot be `NULL`.
   @param word A null terminated characters sequence that will be associated to `data`.
   @return <ul>
   <li>`0` on success</li>
   <li>`LST_INVALID_WORD` if the word contains invalid characters, in which case nothing was added to the list</li>
   <li>`LST_NO_MEM` if there isn't enough memory to create a new entry</li>
   <li>`LST_ALREADY_EXISTS` if there's a known entry using `word`</li>
   </ul>
 */
int list_add(Word_list_ptr list, void *data, char *word)
{
	int ret;
	struct yaircd_node *new_node;
	new_node = malloc(sizeof(*new_node));
	ret = 0;
	if (new_node == NULL) {
		return LST_NO_MEM;
	}
	if (pthread_mutex_init(&new_node->mutex, NULL) != 0) {
		free(new_node);
		return LST_NO_MEM;
	}
	new_node->data = data;
	pthread_mutex_lock(&list->mutex);
	if (find_word_trie(list->trie, word) != NULL) {
		ret = LST_ALREADY_EXISTS;
	}else  {
		ret = add_word_trie(list->trie, word, new_node);
	}
	pthread_mutex_unlock(&list->mutex);
	if (ret == TRIE_INVALID_WORD || ret == TRIE_NO_MEM || ret == LST_ALREADY_EXISTS) {
		pthread_mutex_destroy(&new_node->mutex);
		free(new_node);
	}
	return ret == TRIE_INVALID_WORD ? LST_INVALID_WORD : ret == TRIE_NO_MEM ? LST_NO_MEM : ret;
}

/** Deletes an entry from a list. If no such entry exists, nothing happens.
   The deletion operation will only take place after both the global lock and a unique lock associated to `word` are
      obtained. Thus, when an entry is deleted,no thread is ever doing some processing with that node.
   @param list The list.
   @param word A null terminated characters sequence denoting the entry to be deleted.
   @return If the word existed, its associated data is returned. Otherwise, `NULL` is returned.
   @warning This function does not free the data associated to a word.
 */
void *list_delete(Word_list_ptr list, char *word)
{
	void *ret;
	void *old_data;
	struct yaircd_node *node;
	pthread_mutex_lock(&list->mutex);
	ret = find_word_trie(list->trie, word);
	if (ret == NULL) {
		pthread_mutex_unlock(&list->mutex);
		return NULL;
	}
	node = (struct yaircd_node*)ret;
	pthread_mutex_lock(&node->mutex);
	(void)delete_word_trie(list->trie, word);
	pthread_mutex_unlock(&node->mutex);
	pthread_mutex_unlock(&list->mutex);
	old_data = node->data;
	free_yaircd_node((void*)node, NULL);
	return old_data;
}

/** Deletes an entry from a list. If no such entry exists, nothing happens.
   This function does not try to obtain the global list lock nor the unique lock associated to a node, thus, it should
      only be called from within a function holding a lock to the list and to the node being deleted.
   @param list The list.
   @param word A null terminated characters sequence denoting the entry to be deleted.
   @return If the word existed, its associated data is returned. Otherwise, `NULL` is returned.
   @warning This function does not free the data associated to a word.
 */
void *list_delete_nolock(Word_list_ptr list, char *word)
{
	void *ret;
	struct yaircd_node *node;
	ret = delete_word_trie(list->trie, word);
	if (ret == NULL) {
		return NULL;
	}
	node = (struct yaircd_node*)ret;
	ret = node->data;
	free_yaircd_node((void*)node, NULL);
	return ret;
}
