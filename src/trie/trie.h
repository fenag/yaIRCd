#ifndef TRIE_GUARD
#define TRIE_GUARD

#define ALPHABET_SIZE 26
#define SPECIAL_CHARS_SIZE 9
#define EDGES_NO ALPHABET_SIZE+SPECIAL_CHARS_SIZE

struct trie_node {
	char is_word;
	int children;
	struct trie_node *edges[EDGES_NO];
};

struct trie_node_stack_elm {
	struct trie_node *el;
	struct trie_node_stack_elm *next;
	char letter;
	int depth;
};

struct trie_node_stack {
	char *path;
	char *prefix;
	int depth;
	struct trie_node_stack_elm *top;
};

struct trie_node *init_trie(void);
int add_word_trie(struct trie_node *trie, char *word);
void delete_word_trie(struct trie_node *trie, char *word);
int find_word_trie(struct trie_node *trie, char *word);
struct trie_node_stack *find_by_prefix_next_trie(struct trie_node *trie, struct trie_node_stack *st, const char *prefix, int depth, char *result);
void free_trie_stack(struct trie_node_stack *st);
#endif /* TRIE_GUARD */
