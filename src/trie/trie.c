#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "trie.h"

/** @file
	@brief Trie implementation tuned for IRC nicknames

	This file implements every trie operations available to the nicknames list manager. 
	Please refer to http://en.wikipedia.org/wiki/Trie if you are not sure how a trie works.
	
	@author Filipe Goncalves
	@date November 2013
	@see { client_list.c }
	@warning This implementation is reentrant, but it is not thread safe. The same function with the same trie instance cannot be called concurrently. Upper caller needs to make the necessary use of mutexes or other
	synchronization primitives.
*/

#define valid_char(s) (((s) >= 'a' && (s) <= 'z') || ((s) >= 'A' && (s) <= 'Z') || (s) == '-' || (s) == '[' || (s) == ']' || (s) == '\\' || (s) == '`' || (s) == '^' || (s) == '{' || s == '}' || s =='|')
#define special_char_id(s) ((s) == '-' ? 0 : s == '[' || s == '{' ? 1 : s == ']' || s == '}' ? 2 : s == '\\' || s == '|' ? 3 : s == '`' ? 4 : s == '^' ? 5 : -1)
#define special_id_to_char(i) ((i) == 0 ? '-' : (i) == 1 ? '{' : (i) == 2 ? '}' : (i) == 3 ? '|' : (i) == 4 ? '`' : (i) == 5 ? '^' : -1)
#define get_char_pos(s) ((((s) >= 'a' && (s) <= 'z') || ((s) >= 'A' && (s) <= 'Z')) ? tolower((unsigned char) (s)) - 'a' : ALPHABET_SIZE + special_char_id(s))
#define pos_to_char(i)  ((char) (((i) < ALPHABET_SIZE) ? ('a'+(i)) : special_id_to_char((i) - ALPHABET_SIZE)))


/** Creates a new trie.
	@return `NULL` if there wasn't enough memory; a new trie instance with no words otherwise.
*/
struct trie_node *init_trie(void) {
	int i;
	struct trie_node *root;
	root = malloc(sizeof(struct trie_node));
	if (root == NULL) {
		return NULL;
	}
	root->is_word = 0;
	root->children = 0;
	for (i = 0; i < EDGES_NO; i++)
		root->edges[i] = NULL;	
	return root;
}

/** Frees a node's child. After returning, ensures that `node->edges[pos] == NULL` and that every resource previously allocated was freed.
	@param node Node that contains the child to free
	@param pos Which child to free
	@warning Assumes that `node->edges[pos]` is not `NULL`.
*/
static inline void free_child(struct trie_node *node, unsigned char pos) {
	free(node->edges[pos]);
	node->edges[pos] = NULL;
	node->children--;
}

/** Frees every allocated storage for a trie.
	@param trie A trie, as returned by `init_trie()`.
*/
void destroy_trie(struct trie_node *trie) {
	int i;
	for (i = 0; i < EDGES_NO; i++) {
		if (trie->edges[i] != NULL) {
			destroy_trie(trie->edges[i]);
		}
	}
	free(trie);
}

/** Adds a new word to a trie.
	@param root The trie's root node, as returned by `init_trie()`
	@param word The word to add. Must be a null-terminated characters sequence.
	@return `0` on success; `-1` if `word` contains invalid characters, in which case the trie remains unchanged.
*/
int add_word_trie(struct trie_node *root, char *word) {
	unsigned char pos;
	if (*word == '\0') {
		root->is_word = 1;
		return 0;
	} else {
		if (!valid_char(*word)) {
			return -1;
		}
		if (root->edges[pos = get_char_pos(*word)] == NULL) {
			root->children++;
			root->edges[pos] = init_trie();
			if (add_word_trie(root->edges[pos], word+1) == -1) {
				free_child(root, pos);
				return -1;
			}
			return 0;
		}
		return add_word_trie(root->edges[pos], word+1);
	}
}

/** Deletes a word from a trie. If no such word exists, or the word contains invalid characters, nothing happens.
	@param root The trie's root node, as returned by `init_trie()`
	@param word The word to delete. Must be a null-terminated characters sequence.
*/
void delete_word_trie(struct trie_node *root, char *word) {
	unsigned char pos;
	if (*word == '\0') {
		root->is_word = 0;
	} else {
		if (!valid_char(*word) || root->edges[pos = get_char_pos(*word)] == NULL) {
			return;
		} else {
			delete_word_trie(root->edges[pos], word+1);
			if (root->edges[pos]->children == 0 && !root->edges[pos]->is_word) {
				free_child(root, pos);
			}
		}
	}
}

/** Searches for a word in a trie.
	@param root The trie's root node, as returned by `init_trie()`
	@param word The word to search for. Must be a null-terminated characters sequence.
	@return 1 if there's a match; 0 if there's no match, or `word` contains invalid characters.
*/
int find_word_trie(struct trie_node *root, char *word) {
	struct trie_node *ptr;
	if (*word == '\0') {
		return root->is_word;
	}
	if (!valid_char(*word)) {
		return 0;
	}
	ptr = root->edges[get_char_pos(*word)];
	return ptr && find_word_trie(ptr, word+1);
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
*/
static inline void trie_push(struct trie_node_stack *st, struct trie_node *el, int depth, char letter) {
	struct trie_node_stack_elm *new_el = malloc(sizeof(struct trie_node_stack_elm));
	new_el->letter = letter;
	new_el->depth = depth;
	new_el->next = st->top;
	st->top = new_el;
	new_el->el = el;
}

/** Finds the next match for an on going search by prefix.
	@param st The stack with containing state information. It is assumed that `st != NULL`.
	@param result Buffer that stores the additional path taken by this branch after processing the prefix. For example, if `prefix` is "hel", and this branch finds a match "hello", then `result` will hold "lo".
		   It is imperative that `result` points to a memory location large enough to hold at least `st->depth` characters, of which `st->depth-1` characters will belong to the branch path.
		   When this function returns a value that is not `NULL`, it is guaranteed that `result` is null-terminated and contains a valid match for an on going prefix search.
	@return State information for the next call; `NULL` if no more matches were found. If `NULL` is returned, `result` may have been written, but its contents are meaningless.
	@warning If this function returns `NULL`, the contents of `result` are undefined.
	@warning This function does not free state information when it returns `NULL`. Thus, the caller is required to save `st` in an auxiliary variable. If the same variable is used, then the reference to the last
			 valid state is lost and it is not possible to free it anymore.
*/
static struct trie_node_stack *find_by_prefix_next_trie_n(struct trie_node_stack *st, char *result) {
	struct trie_node_stack_elm *curr;
	int i;
	while (!trie_stack_empty(st)) {
		curr = trie_pop(st);
		if (curr->depth+1 < st->depth) {
			for (i = EDGES_NO-1; i >= 0; i--) {
				if (curr->el->edges[i] != NULL) {
					trie_push(st, curr->el->edges[i], curr->depth+1, pos_to_char(i));
				}
			}
		}
		st->path[curr->depth-1] = curr->letter;
		if (curr->el->is_word) {
			st->path[curr->depth] = '\0';
			strcpy(result, st->path);
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
	@param trie The trie's root node, as returned by `init_trie()`
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
	@warning `result` may have been modified even if `NULL` was returned. In this case, its content is undefined.
	@warning `prefix` must be a characters sequence such that `strlen(prefix) <= depth-1`. Ignoring this requirement leads to buffer overflows when writing to `result`.
	@warning This function will have undefined behavior if it is called with a state `st` that holds information for a `prefix`, but in the meantime, words were removed that matched `prefix`. The caller must ensure that
			 this never happens, otherwise, the program will most likely crash for accessing invalid memory positions.
	@warning It is not allowed to call this function with old `st` values. The only valid `st` is the one that was returned by the previous call, since this function frees some of the state information as the search go along.
			 Thus, it is assumed that the search always moves forward, and never backwards. Calling this with an old value for `st` results in undefined and erratic behavior.
	@note If the caller no longer wishes to keep on searching, state information previously returned can be freed by calling `free_trie_stack()`. Again, only the last returned value can be freed.
		  It is not required to call `free_trie_stack()` after no more matches exist, and in fact it is not allowed to. After no more matches exist, this function automatically frees state information. 
*/
struct trie_node_stack *find_by_prefix_next_trie(struct trie_node *trie, struct trie_node_stack *st, const char *prefix, int depth, char *result) {
	struct trie_node *n;
	struct trie_node_stack *new_st;
	const char *ptr;
	int i;
	int size;
	if (st == NULL) {
		for (size = 0, n = trie, ptr = prefix; *ptr != '\0'; ptr++, size++) {
			if (!valid_char(*ptr) || (n = n->edges[get_char_pos(*ptr)]) == NULL) {
				return NULL;
			}
		}
		/* assert: n != NULL */
		st = malloc(sizeof(struct trie_node_stack));
		st->top = NULL;
		st->prefix = strdup(prefix);
		st->path = malloc(depth-size);
		st->depth = depth-size;
		if (st->depth > 1) {
			/* We can still write at least 1 char in result */
			for (i = EDGES_NO-1; i >= 0; i--) {
				if (n->edges[i] != NULL) {
					trie_push(st, n->edges[i], 1, pos_to_char(i));
				}
			}
		}
		if (n->is_word) {
			strcpy(result, st->prefix);
			return st;
		}
	}
	result += sprintf(result, "%s", st->prefix);
	if ((new_st = find_by_prefix_next_trie_n(st, result)) == NULL) {
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
static void print_trie_aux(struct trie_node *root, int depth);
static void print_trie(struct trie_node *root) {
	print_trie_aux(root, 0);
}
static void inline print_spaces(int d);
static void print_trie_aux(struct trie_node *root, int depth) {
	int i;
	for (i = 0; i < EDGES_NO; i++)
		if (root->edges[i] != NULL) {
			print_spaces(depth);
			printf("%c %s:\n", pos_to_char(i), (root->edges[i]->is_word ? "[WORD]" : ""));
			print_trie_aux(root->edges[i], depth+1);
		}
}

static void inline print_spaces(int d) {
	while (d--) {
		putchar(' ');
		putchar(' ');
	}
}
#define MAX_WORD_LENGTH 10
#define MAX_WORD_LENGTH_STR "%10s"
int main(void) {
	int option = -1, i;
	char word_buf[MAX_WORD_LENGTH+1];
	char prefix[MAX_WORD_LENGTH+1];
	char option_str[2], *endptr;
	struct trie_node *trie;
	struct trie_node_stack *st;
	trie = init_trie();
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
				add_word_trie(trie, word_buf);
				break;
			case 2:
				printf("Type a word (max. %d characters): ", MAX_WORD_LENGTH);
				scanf(MAX_WORD_LENGTH_STR, word_buf);
				if (find_word_trie(trie, word_buf))
					printf("Word exists.");
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
	destroy_trie(trie);
	return 0;
}
*/
