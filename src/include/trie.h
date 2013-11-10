#ifndef TRIE_GUARD
#define TRIE_GUARD

/** A node in a trie. */
struct trie_node {
	char is_word; /**<Indicates if the path from root down to this node denotes a word */
	int children; /**<Says how many children are present in `edges[]` */
	struct trie_node **edges; /**<Edges pointing to this node's children. This is a dynamically allocated array. Each `edges[i]` represents the character returned by `(*trie->pos_to_char)(i)`. */
	void *data; /**<Pointer to arbitrary data associated with this node. This is valid only if `is_word` is true, and it is used by the client code to associate data with words. */
};

/** A trie */
struct trie_t {
	struct trie_node *root; /**<Root node */
	void (*free_f)(void *args); /**<A pointer to a function that is responsible for free'ing a node's `data` when it is about to be destroyed. 
									It is only called by `destroy_trie()`, and only if the caller intends so - this decision is controlled by an argument to `destroy_trie()`.
									This function will be passed the `data` pointer stored in a trie node. */
	int (*is_valid)(char s); /**<A pointer to a function that returns `1` if `s` is a valid character (considered part of a word), and `0` otherwise. */
	char (*pos_to_char)(int p); /**<A pointer to a function that converts an index position from `edges` back to its character representation. */
	int (*char_to_pos)(char s); /**<A pointer to a function that converts a character `s` into a valid, unique position that is used to index `edges`. */
	int edges_no; /**<Number of edges. It is valid to reference any position in `edges[0 .. edges_no-1]`. */
};

/** A stack element describing a node in a path of a prefix search. */
struct trie_node_stack_elm {
	struct trie_node *el; /**<Pointer to the node that this element describes */
	struct trie_node_stack_elm *next; /**<Pointer to the next stack element */
	char letter; /**<Letter that was last used to reach this node, namely, the result of calling `(*trie->pos_to_char)(i)` for an edge `i` such that `edges[i] == `el`. */
	int depth; /**<Depth of this element on the stack. Starts counting from 1. */
};

/** A stack used to maintain state between different calls to `find_by_prefix_next_trie()`. */
struct trie_node_stack {
	char *path; /**<A characters sequence describing the path from the root node down to the current node. Each `trie_node_stack_elm` in the stack writes to `path[element->depth-1]`. */
	char *prefix; /**<The prefix originally passed to `find_by_prefix_next_trie()` in the first call that started this search. */
	int depth; /**<Max. depth allowed. Only character sequences of at most `depth-1` will be reported and written to `path`. When a match is found, `path` is null terminated; it must hold enough space for at least `depth` characters. */
	struct trie_node_stack_elm *top; /**<The top of the stack */
};

/* These functions are documented in the C file that implements them */
struct trie_t *init_trie(void (*free_function)(void *), int (*is_valid)(char), char (*pos_to_char)(int), int (*char_to_pos)(char), int edges);
int add_word_trie(struct trie_t *trie, char *word, void *data);
void *delete_word_trie(struct trie_t *trie, char *word);
void *find_word_trie(struct trie_t *trie, char *word);
struct trie_node_stack *find_by_prefix_next_trie(struct trie_t *trie, struct trie_node_stack *st, const char *prefix, int depth, char *result);
void free_trie_stack(struct trie_node_stack *st);
#endif /* TRIE_GUARD */
