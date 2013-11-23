#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "trie.h"

/** @file
	@brief Flexible trie implementation with some neat options.

	This file implements every trie operations available to the various list managers (nicknames list, commands list, channels list, and any other list of strings)
	It allows client code (by client we mean "the code that uses this") to define which characters are allowed inside a word.
	Client code is required to provide functions that convert a letter into a position (ID) and a position back into a letter. IDs must be unique, consecutive, and start at 0; there can be no gaps
	in the sequence, because IDs are used to index an array. So, for example, to allow an alphabet which consists of the characters `[a-z]` and `[0-9]`, client code must find a mapping which converts 
	any of these characters into an integer `i` such that `i >= 0 && i <= 35` (26 letters for the alphabet and 10 digits).
	One possible mapping would be to map any letter `c` in `[a-z]` to `c - &lsquo;a&rsquo;` and any number `i` in `[0-9]` to `&lsquo;z&rsquo; + i - &lsquo;0&rsquo;`.
	
	Please refer to http://en.wikipedia.org/wiki/Trie if you are not sure how a trie works. It always guarantees `O(n)` insertion, deletion and search time, where `n` is the size of the word. When compared to hash tables,
	it is a good alternative, since hash tables provide `O(1)` access, but normally take about `O(n)` time to compute the hash function, and there can be collisions.
	
	@author Filipe Goncalves
	@date November 2013
	@see client_list.c
	@warning This implementation is reentrant, but it is not thread safe. The same trie instance cannot be fed into this implementation from different threads concurrently. 
	Upper caller needs to make the necessary use of mutexes or other synchronization primitives.
*/

/** Initializes a new trie node with no children and no edges.
	@param edges Number of edges (alphabet size)
	@return The new node, or `NULL` if there are no resources to create a node.
*/
static struct trie_node *init_node(int edges) {
	int i;
	struct trie_node *new_node;
	if ((new_node = malloc(sizeof(struct trie_node))) == NULL) {
		return NULL;
	}
	new_node->is_word = 0;
	new_node->children = 0;
	new_node->data = NULL;
	if ((new_node->edges = malloc(sizeof(*new_node->edges)*edges)) == NULL) {
		free(new_node);
		return NULL;
	}
	for (i = 0; i < edges; i++) {
		new_node->edges[i] = NULL;
	}
	return new_node;
}

/** Creates a new trie.
	@param free_function Pointer to function that is called inside `destroy_trie()` to free a node's `data`
	@param is_valid Pointer to function that returns `1` if a char is part of this trie's alphabet; `0` otherwise
	@param pos_to_char Pointer to function that converts an index position from `edges` back to its character representation.
	@param char_to_pos Pointer to function that converts a character `s` into a valid, unique position that is used to index `edges`.
	@param edges How many edges each node is allowed to have, that is, the size of this trie's alphabet.
	@return A new trie instance with no words, or `NULL` if there isn't enough memory to create a trie.
*/
struct trie_t *init_trie(void (*free_function)(void *, void *), int (*is_valid)(char), char (*pos_to_char)(int), int (*char_to_pos)(char), int edges) {
	struct trie_t *trie;
	if ((trie = malloc(sizeof(struct trie_t))) == NULL) {
		return NULL;
	}
	if ((trie->root = init_node(edges)) == NULL) {
		free(trie);
		return NULL;
	}
	trie->free_f = free_function;
	trie->is_valid = is_valid;
	trie->pos_to_char = pos_to_char;
	trie->char_to_pos = char_to_pos;
	trie->edges_no = edges;
	return trie;
}

/** Frees a node's child. After returning, ensures that `node->edges[pos] == NULL` and that every resource previously allocated was freed.
	@param node Node that contains the child to free
	@param pos Which child to free
	@warning Assumes that `node->edges[pos]` is not `NULL`.
	@warning This function is not recursive. If the node in `node->edges[pos]` references other nodes, these references will be lost and there is a memory leak.
*/
static inline void free_child(struct trie_node *node, unsigned char pos) {
	free(node->edges[pos]->edges);
	free(node->edges[pos]);
	node->edges[pos] = NULL;
	node->children--;
}

/** Recursively frees every node reachable from `node`.
	@param node The top node (in the beginning, most likely the root node).
	@param trie A trie, as returned by `init_trie()`.
	@param free_data `TRIE_FREE_DATA` if the free function stored in `trie` shall be used to free each node's data, `TRIE_NO_FREE_DATA` otherwise
	@param args A pointer to a generic data type that will be passed as the second argument to this trie's node data freeing function, as defined in `init_trie()`, if `TRIE_FREE_DATA` is set. 
				This can be `NULL`.
				The first parameter is always the pointer to the deleted node's data.
*/
static void destroy_aux(struct trie_node *node, struct trie_t *trie, int free_data, void *args) {
	int i;
	for (i = 0; i < trie->edges_no; i++) {
		if (node->edges[i] != NULL) {
			destroy_aux(node->edges[i], trie, free_data, args);
		}
	}
	if (free_data == TRIE_FREE_DATA) {
		(*trie->free_f)(node->data, args);
	}
	free(node->edges);
	free(node);
}

/** Frees every allocated storage for a trie.
	@param trie A trie, as returned by `init_trie()`.
	@param free_data `TRIE_FREE_DATA` if the free function stored in `trie` shall be used to free each node's data, `TRIE_NO_FREE_DATA` otherwise
	@param args A pointer to a generic data type that will be passed as the second argument to this trie's node data freeing function, as defined in `init_trie()`, if `TRIE_FREE_DATA` is set. 
				This can be `NULL`.
				The first parameter is always the pointer to the deleted node's data.
*/
void destroy_trie(struct trie_t *trie, int free_data, void *args) {
	destroy_aux(trie->root, trie, free_data, args);
	free(trie);
}

/** Recursive implementation that is called by `add_word_trie()`.
	@param root Current node.
	@param trie A trie, as returned by `init_trie()`.
	@param word The word to add. Must be a null-terminated characters sequence.
	@param data The data to associate to this word.
	@return On success, 0 is returned. Otherwise, a non-zero constant is returned to denote an error, which can be either `TRIE_INVALID_WORD` or `TRIE_NO_MEM`.
			`TRIE_INVALID_WORD` means that there are characters in `word` that are no part of this trie's alphabet, as defined by the functions indicated in `init_trie()`.
			`TRIE_NO_MEM` means that there wasn't enough memory to add `word`. In this case, it is undefined whether `word` is valid or not: it just means that we couldn't keep on
			checking the word and adding the necessary structures anymore.
			When an error occurs, the trie remains unchanged.
	@note If the word already exists, its `data` will now point to the new data. Care must be taken not to lose reference to the old data.
*/
static int add_word_trie_aux(struct trie_node *root, char *word, struct trie_t *trie, void *data) {
	unsigned char pos;
	int ret;
	if (*word == '\0') {
		root->is_word = 1;
		root->data = data;
		return 0;
	} else {
		if (!(*trie->is_valid)(*word)) {
			return TRIE_INVALID_WORD;
		}
		if (root->edges[pos = (*trie->char_to_pos)(*word)] == NULL) {
			if ((root->edges[pos] = init_node(trie->edges_no)) == NULL) {
				return TRIE_NO_MEM;
			}
			root->children++;
			if ((ret = add_word_trie_aux(root->edges[pos], word+1, trie, data)) != 0) {
				/* There was an error somewhere down there */
				free_child(root, pos);
				return ret;
			}
			/* assert: ret == 0 */
			return 0;
		}
		return add_word_trie_aux(root->edges[pos], word+1, trie, data);
	}
}

/** Adds a new word to a trie.
	@param trie A trie, as returned by `init_trie()`.
	@param word The word to add. Must be a null-terminated characters sequence.
	@param data The data to associate to this word.
	@return On success, 0 is returned. Otherwise, a non-zero constant is returned to denote an error, which can be either `TRIE_INVALID_WORD` or `TRIE_NO_MEM`.
			`TRIE_INVALID_WORD` means that there are characters in `word` that are no part of this trie's alphabet, as defined by the functions indicated in `init_trie()`.
			`TRIE_NO_MEM` means that there wasn't enough memory to add `word`. In this case, it is undefined whether `word` is valid or not: it just means that we couldn't keep on
			checking the word and adding the necessary structures anymore.
			When an error occurs, the trie remains unchanged.
	@note If the word already exists, its `data` will now point to the new data. Care must be taken not to lose reference to the old data.
*/
int add_word_trie(struct trie_t *trie, char *word, void *data) {
	return add_word_trie_aux(trie->root, word, trie, data);
}

/** Recursive implementation called by `delete_word_trie()`.
	@param root Current node.
	@param trie A trie, as returned by `init_trie()`.
	@param word The word to delete. Must be a null-terminated characters sequence.
	@return If the word existed, its associated data is returned. Otherwise, `NULL` is returned.
*/
static void *delete_word_trie_aux(struct trie_node *root, char *word, struct trie_t *trie) {
	unsigned char pos;
	void *ret;
	if (*word == '\0') {
		if (root->is_word) {
			root->is_word = 0;
			return root->data;
		}
		return NULL;
	} else {
		if (!(*trie->is_valid)(*word) || root->edges[pos = (*trie->char_to_pos)(*word)] == NULL) {
			return NULL;
		} else {
			/* assert: root->edges[pos] != NULL */
			ret = delete_word_trie_aux(root->edges[pos], word+1, trie);
			if (root->edges[pos]->children == 0 && !root->edges[pos]->is_word) {
				free_child(root, pos);
			}
			return ret;
		}
	}
}

/** Deletes a word from a trie.
	@param trie A trie, as returned by `init_trie()`.
	@param word The word to delete. Must be a null-terminated characters sequence.
	@return If the word existed, its associated data is returned. Otherwise, `NULL` is returned.
*/
void *delete_word_trie(struct trie_t *trie, char *word) {
	return delete_word_trie_aux(trie->root, word, trie);
}

/** Recursive search implementation used by `find_word_trie()`.
	@param root Current node.
	@param trie A trie, as returned by `init_trie()`.
	@param word The word to search for. Must be a null-terminated characters sequence.
	@return The data associated with `word` if there's a match; `NULL` if there's no match, or `word` contains invalid characters.
*/
static void *find_word_trie_aux(struct trie_node *root, char *word, struct trie_t *trie) {
	struct trie_node *ptr;
	if (*word == '\0') {
		return root->is_word ? root->data : NULL;
	}
	if (!(*trie->is_valid)(*word)) {
		return NULL;
	}
	ptr = root->edges[(*trie->char_to_pos)(*word)];
	return ptr ? find_word_trie_aux(ptr, word+1, trie) : NULL;
}

/** Searches for a word in a trie.
	@param trie A trie, as returned by `init_trie()`.
	@param word The word to search for. Must be a null-terminated characters sequence.
	@return The data associated with `word` if there's a match; `NULL` if there's no match, or `word` contains invalid characters.
*/
void *find_word_trie(struct trie_t *trie, char *word) {
	return find_word_trie_aux(trie->root, word, trie);
}

static void trie_for_each_aux(struct trie_t *trie, struct trie_node *node, void (*f)(void *, void *), void *fargs) {
	int i;
	if (node->is_word) {
		(*f)(node->data, fargs);
	}
	for (i = 0; i < trie->edges_no; i++) {
		if (node->edges[i] != NULL) {
			trie_for_each_aux(trie, node->edges[i], f, fargs);
		}
	}
}

void trie_for_each(struct trie_t *trie, void (*f)(void *, void *), void *fargs) {
	trie_for_each_aux(trie, trie->root, f, fargs);
}

/** Pops an element off the stack that represents an on going search by prefix.
	@param st The stack.
	@return Element at the top of the stack.
*/
static inline struct trie_node_stack_elm *trie_pop(struct trie_node_stack *st) {
	struct trie_node_stack_elm *res = st->top;
	st->top = st->top->next;
	return res;
}

/** Checks if the stack of an on going search by prefix is empty.
	@param st The stack.
	@return 1 if there's at least one element in the stack; 0 otherwise.
*/
static inline int trie_stack_empty(struct trie_node_stack *st) {
	return st->top == NULL;
}

/** Pushes a new element into a stack of an on going search by prefix. Creates a new instance of `struct trie_node_stack_elm` and places it at the top of `st`.
	@param st The stack.
	@param el A trie node that is associated with this element.
	@param depth This element's depth in the stack.
	@param letter Last letter used to arrive to this node.
	@return A pointer to the new node pushed, or `NULL` if there wasn't enough memory to push a new node, in which case the stack remains unchanged.
*/
static inline struct trie_node_stack_elm *trie_push(struct trie_node_stack *st, struct trie_node *el, int depth, char letter) {
	struct trie_node_stack_elm *new_el;

	if ((new_el = malloc(sizeof(struct trie_node_stack_elm))) == NULL) {
		return NULL;
	}
	
	new_el->letter = letter;
	new_el->depth = depth;
	new_el->next = st->top;
	st->top = new_el;
	new_el->el = el;
	
	return new_el;
}

/** Finds the next match for an on going search by prefix.
	@param st The stack with containing state information. It is assumed that `st != NULL`.
	@param result Buffer that stores the additional path taken by this branch after processing the prefix. For example, if `prefix` is "hel", and this branch finds a match "hello", then `result` will hold "lo".
		   It is imperative that `result` points to a memory location large enough to hold at least `st->depth` characters, of which `st->depth-1` characters will belong to the branch path.
		   When this function returns a value that is not `NULL`, it is guaranteed that `result` is null-terminated and contains a valid match for an on going prefix search.
	@param trie A trie, as returned by `init_trie()`
	@param err_code A pointer that will be used to store error conditions that may arise. In case of success, `*err_code` will be `0`. In case of error, `*err_code` holds the value of a non-zero constant describing
		   the error. At the moment, only `TRIE_NO_MEM` is possible. When `*err_code == TRIE_NO_MEM`, it means that it was not possible to generate new state information that would otherwise be useful and necessary
		   for future searches to continue. However, this error condition does not affect this function's correctness: `TRIE_NO_MEM` only implies that an on going search will not be able to find every possible match
		   for a given prefix, since it cannot store new state information. Note that it can use old state information stored in previous calls, and will continue to do so even after `TRIE_NO_MEM` is signalized.
		   Thus, it is always safe to use this function's result when it returns someting that is not `NULL`, but when `TRIE_NO_MEM` is reported, it is not guaranteed that every match will be found.
	@return State information for the next call; `NULL` if no more matches were found. If `NULL` is returned, `result` may have been written, but its contents are meaningless, and it is not guaranteed to be null-terminated.
	@warning If this function returns `NULL`, the contents of `result` are undefined.
	@warning This function does not free state information when it returns `NULL`. Thus, the caller is required to save `st` in an auxiliary variable. If the same variable is used, then the reference to the last
			 valid state is lost and it is not possible to free it anymore.
*/
static struct trie_node_stack *find_by_prefix_next_trie_n(struct trie_node_stack *st, char *result, struct trie_t *trie, int *err_code, void **data) {
	struct trie_node_stack_elm *curr;
	int i;
	*err_code = 0;
	while (!trie_stack_empty(st)) {
		curr = trie_pop(st);
		if (curr->depth+1 < st->depth) {
			for (i = trie->edges_no-1; i >= 0 && *err_code == 0; i--) {
				if (curr->el->edges[i] != NULL) {
					if (trie_push(st, curr->el->edges[i], curr->depth+1, (*trie->pos_to_char)(i)) == NULL) {
						*err_code = TRIE_NO_MEM;
					}
				}
			}
		}
		st->path[curr->depth-1] = curr->letter;
		if (curr->el->is_word) {
			st->path[curr->depth] = '\0';
			strcpy(result, st->path);
			*data = curr->el->data;
			free(curr);
			return st;
		}
		free(curr);
	}
	return NULL;
}

void free_trie_stack(struct trie_node_stack *st);
/** Finds words in a trie by prefix. Special efforts have been made to maintain this function reentrant; no internal state is preserved (we delegate this to the upper caller).
	To find every match for a given prefix, this function must be repeatedly called until no more matches are reported. Each call to this function will pop a new match for the prefix.
	@param trie A trie, as returned by `init_trie()`
	@param st The value that was returned by the previous call to this function. This is necessary to allow the function to continue from where it previously stopped.
			  If this is the first call, this parameter must be `NULL`. Note that this parameter must be `NULL` everytime a new prefix search takes place.
	@param prefix A null terminated characters sequence describing the prefix. For example, "hel" is a prefix that will match words like "hell", "hello", and others. It will also match "hel", if "hel" is a word.
				  If this parameter contains invalid characters, `NULL` is returned.
				  This parameter is only needed in the first call. If `st` is not `NULL`, this parameter is ignored.
	@param depth Indicates max. size of a match. Only matches with at most `depth-1` characters are reported. If the caller knows how long is the biggest word ever inserted, using that value plus 1 allows it to get every match.
				 Because matches are written in `result`, `result` must be able to hold at least `depth` characters, of which `depth-1` are word characters, and the last one is the null terminator.
				 It is assumed that this parameter is `> 0`.
				 This parameter is ignored if `st` is not `NULL`. Thus, the max. size of a match must be decided in the first call to find matches for this prefix.
	@param result A buffer where the new match found (if there is one) will be stored. It is assumed that this parameter points to a valid memory location with enough space to hold `depth` characters, 
				  of which at most `depth-1` will belong to a word match. The buffer will be null-terminated in the position after the last character of a match.
	@return <ul>
				<li>`NULL` if no more matches are available</li>
				<li>Otherwise, a structure that contains state information for the next call.</li>
			</ul>
	@param err_code A pointer that will be used to store error conditions that may arise. In case of success, `*err_code` will be `0`. In case of error, `*err_code` holds the value of a non-zero constant describing
		   the error. At the moment, only `TRIE_NO_MEM` is possible. When `*err_code == TRIE_NO_MEM`, it means that it was not possible to generate new state information that would otherwise be useful and necessary
		   for future searches to continue. However, this error condition does not affect this function's correctness: `TRIE_NO_MEM` only implies that an on going search will not be able to find every possible match
		   for a given prefix, since it cannot store new state information. Note that it can use old state information stored in previous calls, and will continue to do so even after `TRIE_NO_MEM` is signalized.
		   Thus, it is always safe to use this function's result when it returns someting that is not `NULL`, but when `TRIE_NO_MEM` is reported, it is not guaranteed that every match will be found.
		   This error can be reported even in the first call to this function.
	@warning `result` may have been modified even if `NULL` was returned. In this case, its content is undefined.
	@warning `prefix` must be a characters sequence such that `strlen(prefix) <= depth-1`. Ignoring this requirement leads to buffer overflows when writing to `result`.
	@warning This function will have undefined behavior if it is called with a state `st` that holds information for a `prefix`, but in the meantime, words were removed that matched `prefix`. The caller must ensure that
			 this never happens, otherwise, the program will most likely crash for accessing invalid memory positions.
	@warning It is not allowed to call this function with old `st` values. The only valid `st` is the one that was returned by the previous call, since this function frees some of the state information as the search goes along.
			 Thus, it is assumed that the search always moves forward, and never backwards. Calling this with an old value for `st` results in undefined and erratic behavior.
	@note If the caller no longer wishes to keep on searching, state information previously returned can be freed by calling `free_trie_stack()`. Again, only the last returned value can be freed.
		  It is not required to call `free_trie_stack()` after no more matches exist, and in fact it is not allowed to. After no more matches exist, this function automatically frees state information.
	@note It is important to note that everytime this function returns `NULL`, one shall not call `free_trie_stack()`. This function will implicitly free state information when no more matches are found.
	@note It is always safe to use `result` as a valid match as long as this function does not return `NULL`. If `*err_code == TRIE_NO_MEM`, it just means that subsequent searches may end prematurely, and that
		  not every match will possibly be returned.
*/
struct trie_node_stack *find_by_prefix_next_trie(struct trie_t *trie, struct trie_node_stack *st, const char *prefix, int depth, char *result, int *err_code, void **data) {
	struct trie_node *n;
	struct trie_node_stack *new_st;
	const char *ptr;
	int i;
	int size;
	
	*err_code = 0;
	if (st == NULL) {
		for (size = 0, n = trie->root, ptr = prefix; *ptr != '\0'; ptr++, size++) {
			if (!(*trie->is_valid)(*ptr) || (n = n->edges[(*trie->char_to_pos)(*ptr)]) == NULL) {
				return NULL;
			}
		}
		/* assert: n != NULL */
		if ((st = malloc(sizeof(struct trie_node_stack))) == NULL) {
			return NULL;
		}
		if ((st->prefix = strdup(prefix)) == NULL) {
			free(st);
			return NULL;
		}
		if ((st->path = malloc(depth-size)) == NULL) {
			free(st->prefix);
			free(st);
			return NULL;
		}
		st->top = NULL;
		st->depth = depth-size;
		if (st->depth > 1) {
			/* We can still write at least 1 char in result */
			for (i = trie->edges_no-1; i >= 0 && *err_code == 0; i--) {
				if (n->edges[i] != NULL) {
					if (trie_push(st, n->edges[i], 1, (*trie->pos_to_char)(i)) == NULL) {
						*err_code = TRIE_NO_MEM;
					}
				}
			}
		}
		if (n->is_word) {
			strcpy(result, st->prefix);
			*data = n->data;
			return st;
		}
	}
	result += sprintf(result, "%s", st->prefix);
	if ((new_st = find_by_prefix_next_trie_n(st, result, trie, err_code, data)) == NULL) {
		free_trie_stack(st);
	}
	return new_st;
}


/** Frees a whole stack of elements.
	@param el Element on the top of the stack.
*/
static void free_stack_elements(struct trie_node_stack_elm *el) {
	if (el == NULL)
		return;
	free_stack_elements(el->next);
	free(el);
}

/** Frees every storage allocated for a prefix search. This function cannot called after `find_by_prefix_next_trie()` returned `NULL` for a given search.
	@param st Last state instance returned by `find_by_prefix_next_trie()` for a given search.
	@note It is safe to call this function after destroying a trie.
*/
void free_trie_stack(struct trie_node_stack *st) {
	if (st == NULL) {
		return;
	}
	free_stack_elements(st->top);
	free(st->path);
	free(st->prefix);
	free(st);
}

/* Debug */
/*
static void print_trie_aux(struct trie_node *root, int depth, struct trie_t *trie);
static void print_trie(struct trie_t *trie) {
	print_trie_aux(trie->root, 0, trie);
}
static void inline print_spaces(int d);
static void print_trie_aux(struct trie_node *root, int depth, struct trie_t *trie) {
	int i;
	for (i = 0; i < trie->edges_no; i++)
		if (root->edges[i] != NULL) {
			print_spaces(depth);
			printf("%c %s:\n", (*trie->pos_to_char)(i), (root->edges[i]->is_word ? "[WORD]" : ""));
			print_trie_aux(root->edges[i], depth+1, trie);
		}
}
static void inline print_spaces(int d) {
	while (d--) {
		putchar(' ');
		putchar(' ');
	}
}
#define ALPHABET_SIZE 26
#define SPECIAL_CHARS_SIZE 9
#define EDGES_NO ALPHABET_SIZE+SPECIAL_CHARS_SIZE
#define valid_char(s) (((s) >= 'a' && (s) <= 'z') || ((s) >= 'A' && (s) <= 'Z') || (s) == '-' || (s) == '[' || (s) == ']' || (s) == '\\' || (s) == '`' || (s) == '^' || (s) == '{' || s == '}' || s =='|')
#define special_char_id(s) ((s) == '-' ? 0 : s == '[' || s == '{' ? 1 : s == ']' || s == '}' ? 2 : s == '\\' || s == '|' ? 3 : s == '`' ? 4 : s == '^' ? 5 : -1)
#define special_id_to_char(i) ((i) == 0 ? '-' : (i) == 1 ? '{' : (i) == 2 ? '}' : (i) == 3 ? '|' : (i) == 4 ? '`' : (i) == 5 ? '^' : -1)
#define get_char_pos_m(s) ((((s) >= 'a' && (s) <= 'z') || ((s) >= 'A' && (s) <= 'Z')) ? tolower((unsigned char) (s)) - 'a' : ALPHABET_SIZE + special_char_id(s))
#define pos_to_char_m(i)  ((char) (((i) < ALPHABET_SIZE) ? ('a'+(i)) : special_id_to_char((i) - ALPHABET_SIZE)))
void free_f(void *args) {
	return;
}
int is_valid(char s) {
	return valid_char(s);
}
char pos_to_char(int i) {
	return pos_to_char_m(i);
}
int char_to_pos(char s) {
	return get_char_pos_m(s);
}
#define MAX_WORD_LENGTH 128
#define MAX_WORD_LENGTH_STR "%128s"
int main(void) {
	int option = -1, i;
	char word_buf[MAX_WORD_LENGTH+1];
	char prefix[MAX_WORD_LENGTH+1];
	char option_str[2], *endptr;
	struct trie_t *trie;
	struct trie_node_stack *st;
	int *my_data = &option;
	int *returned;
	trie = init_trie(free_f, is_valid, pos_to_char, char_to_pos, EDGES_NO);
	while (option != 0) {
		printf("Choose an option:\n");
		printf("1 - Add a word\n");
		printf("2 - Search a word\n");
		printf("3 - Delete a word\n");
		printf("4 - Search by prefix\n");
		printf("5 - Print trie\n");
		printf("0 - Quit\n");
		printf("> ");
		scanf("%2s", option_str);
		option = strtol(option_str, &endptr, 10);
		if (endptr == option_str) {
			printf("Please enter a valid number.\n");
			option = -1;
			continue;
		}
		switch (option) {
			case 1:
				printf("Type a word (max. %d characters): ", MAX_WORD_LENGTH);
				scanf(MAX_WORD_LENGTH_STR, word_buf);
				add_word_trie(trie, word_buf, (void *) my_data);
				break;
			case 2:
				printf("Type a word (max. %d characters): ", MAX_WORD_LENGTH);
				scanf(MAX_WORD_LENGTH_STR, word_buf);
				if ((returned = (int *) find_word_trie(trie, word_buf)) != NULL)
					printf("Word exists. Data: %d\n", *returned);
				else
					printf("Word does not exist.");
				break;
			case 3:
				printf("Type a word (max. %d characters): ", MAX_WORD_LENGTH);
				scanf(MAX_WORD_LENGTH_STR, word_buf);
				delete_word_trie(trie, word_buf);
				break;
			case 4:
				printf("Type a prefix (max. %d characters): ", MAX_WORD_LENGTH);
				scanf(MAX_WORD_LENGTH_STR, prefix);
				printf("Max. length of matches (at most %d): ", MAX_WORD_LENGTH);
				if (!(scanf("%d", &i) == 1 && i > 0 && i <= MAX_WORD_LENGTH)) {
					printf("Invalid number, assuming default (%d)\n", MAX_WORD_LENGTH);
					i = MAX_WORD_LENGTH;
				}
				st = find_by_prefix_next_trie(trie, NULL, prefix, i+1, word_buf);
				if (st == NULL)
					printf("No words matched.");
				else
					printf("Words matched:");
				getchar();
				while (st != NULL) {
					printf("\n%s [ENTER] to continue", word_buf);
					getchar();
					st = find_by_prefix_next_trie(trie, st, NULL, 0, word_buf);
				}
				break;
			case 5:
				print_trie(trie);
				break;
			case 6:
				printf("Type a prefix: ");
				scanf("%s", prefix);

				break;
			case 0:
				break;
			default:
				printf("Unknown option.");
				option = -1;
				break;
		}
		printf("\n");
	}
	destroy_trie(trie, 0);
	return 0;
}
*/
