#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include "list.h"
#include "trie.h"

/** @file
	@brief Generic thread-safe words container.
	
	This file implements a generic thread-safe words container. It uses an underlying trie implementation, and a mutex to control concurrent accesses to this trie.
	Thus, the functions provided here are nothing more than the same functions offered by the trie implementation, except that everything is wrapped in a structure
	with accesses controlled by a mutex.
	
	It is a very interesting and recommendable exercise to go through the trie implementation. Interested readers are invited to look at trie.c.
	
	@author Filipe Goncalves
	@date November 2013
	@see trie.c
*/

/** Structure defining a generic thread-safe words list. */
struct yaircd_list {
	pthread_mutex_t mutex; /**<Mutex to synchronize concurrent access to this list */
	struct trie_t *trie; /**<The underlying list implementation. A trie is used to associate words to data. */
};

/** Initializes a new, empty list, with the necessary structures to control concurrent thread access.
	@param free_function Each word can be associated to a generic pointer hereby denoted `data`. This function will be called to free a node's `data` when it is removed from the list.
						 It can be NULL if nothing shall be done when deleting a node.
	@param is_valid Pointer to function that returns `1` if a char is part of this wordlist's alphabet; `0` otherwise.
	@param pos_to_char Pointer to function that converts an index position from `edges` back to its character representation.
	@param char_to_pos Pointer to function that converts a character `s` into a valid, unique position.
	@param charcount How many characters compose this list's alphabet.
	@return A pointer to a new, empty word list instance, or `NULL` is there weren't enough resources to create a new list.
 */
Word_list_ptr init_word_list(void (*free_function)(void *), int (*is_valid)(char), char (*pos_to_char)(int), int (*char_to_pos)(char), int charcount) {
	Word_list_ptr new_list;

	new_list = malloc(sizeof(struct yaircd_list));
	if (new_list == NULL) {
		return NULL;
	}
	if (pthread_mutex_init(&new_list->mutex, NULL) != 0) {
		free(new_list);
		return NULL;
	}
	if ((new_list->trie = init_trie(free_function, is_valid, pos_to_char, char_to_pos, charcount)) == NULL) {
		pthread_mutex_destroy(&new_list->mutex);
		free(new_list);
		return NULL;
	}
	return new_list;	
}

/** Destroys a word list after it is no longer needed. Every resources previously allocated for this list are freed.
	@param list Pointer to the list structure that shall be freed.
	@param free_data `LIST_FREE_NODE_DATA` if the freeing function for a node's data shall be called for each node while destroying the list; `LIST_NO_FREE_NODE_DATA` if nothing is to me done regarding each node's data.
	@warning This function cannot be called when other threads are possibly reading from or writing to the list.
	@warning If `free_data` is `LIST_FREE_NODE_DATA`, then it is assumed that `free_function` pointer previously passed to `init_word_list` is a valid pointer to a function.
*/
void destroy_word_list(Word_list_ptr list, int free_data) {
	destroy_trie(list->trie, free_data == LIST_FREE_NODE_DATA ? TRIE_FREE_DATA : TRIE_NO_FREE_DATA);
	if (pthread_mutex_destroy(&list->mutex) != 0) {
		perror("::list.c:destroy_word_list(): Could not destroy mutex");
	}
}

/** Finds a given word, and returns the data associated with this word, if a match was found.
	@param list The list to perform the search on
	@param word A pointer to a null terminated characters sequence holding the word to search for
	@return `NULL` if no match was found, or word contains invalid characters according to the `is_valid()` function pointer previously passed to `init_word_list()`.
			Otherwise, a pointer the data associated with this word is returned.
	@warning Remember that only the search is atomic. The upper code using this module should be aware of the possibility of multiple threads holding pointers to the same data structure and performing concurrent work.
			 See `list_find_and_execute()` for a possible workaround for this issue.
	@see list_find_and_execute()
*/
void *list_find_word(Word_list_ptr list, char *word) {
	void *ret;
	pthread_mutex_lock(&list->mutex);
	ret = find_word_trie(list->trie, word);
	pthread_mutex_unlock(&list->mutex);
	return ret;
}

/** Finds and performs an action on a word's data atomically, if a match exists.
	@param list The list to perform the search on.
	@param word A pointer to a null terminated characters sequence holding the word to search for.
	@param f A pointer to a function returning a generic pointer that shall be called if a match is found. 
			 In such case, `f` is called with the matching node's data as its first parameter, and with `fargs` as second parameter.
	@param fargs This will be passed to `f` as a second parameter when a match is found and `f` is called.
	@param success After this function returns, `*success` will hold `1` if `(*f)()` was called, otherwise it will hold `0`. This allows the caller to have `(*f)()` returning `NULL` and still distinguish between
				   a successfull and failed match.
	@return The result of evaluating `(*f)(matching_node_data, fargs)`. If no client matches, `NULL` is returned.
	@warning `(*f)()` must not call `pthread_exit()`, otherwise, the lock for this list is never unlocked, and the whole IRCd freezes.
	@warning Keep in mind that this function locks the whole list. Depending on how critical and used this list is, it can be inefficient to lock the whole list just to perform an operation in a single node.
			 Using this function requires discipline; the operations performed by `f` should generally be fast in time.
*/
void *list_find_and_execute(Word_list_ptr list, char *word, void *(*f)(void *, void *), void *fargs, int *success) {
	void *ret;
	*success = 0;
	pthread_mutex_lock(&list->mutex);
	ret = find_word_trie(list->trie, word);
	if (ret != NULL) {
		ret = (*f)(ret, fargs);
		*success = 1;
	}
	pthread_mutex_unlock(&list->mutex);
	return ret;
}

/** Atomically adds a new word to a list if that word is not stored in the list yet.
	This operation is thread safe and guaranteed to be free of race conditions. The search and add operations are executed atomically.
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
int list_add(Word_list_ptr list, void *data, char *word) {
	int ret;
	pthread_mutex_lock(&list->mutex);
	if (find_word_trie(list->trie, word) != NULL) {
		ret = LST_ALREADY_EXISTS;
	}
	else {
		ret = add_word_trie(list->trie, word, data);
	}
	pthread_mutex_unlock(&list->mutex);
	return ret == TRIE_INVALID_WORD ? LST_INVALID_WORD : ret == TRIE_NO_MEM ? LST_NO_MEM : 0;
}

/** Deletes an entry from a list. If no such entry exists, nothing happens.
	@param list The list.
	@param word A null terminated characters sequence denoting the entry to be deleted.
	@return If the word existed, its associated data is returned. Otherwise, `NULL` is returned.
*/
void *list_delete(Word_list_ptr list, char *word) {
	void *ret;
	pthread_mutex_lock(&list->mutex);
	ret = delete_word_trie(list->trie, word);
	pthread_mutex_unlock(&list->mutex);
	return ret;
}
