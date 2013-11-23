#ifndef __YAIRCD_GENERIC_LIST_GUARD__
#define __YAIRCD_GENERIC_LIST_GUARD__

/** Constant value used to indicate that it is desired to free a node's data when invoking `destroy_word_list()` */
#define LIST_FREE_NODE_DATA 1

/** Constant value used to indicate that it a node's data shall not be freed when invoking `destroy_word_list()` */
#define LIST_NO_FREE_NODE_DATA 0

/** Used by list_add() to indicate that a word is invalid */
#define LST_INVALID_WORD 1

/** Used by list_add() to indicate that there isn't enough memory for a new entry */
#define LST_NO_MEM 2

/** Used by list_add() to indicate that an entry already exists */
#define LST_ALREADY_EXISTS 3

/** Opaque type for a words list that shall be used by the rest of the code */
typedef struct yaircd_list *Word_list_ptr;

/* Documented in .c source file */
Word_list_ptr init_word_list(void (*free_function)(void *), int (*is_valid)(char), char (*pos_to_char)(int), int (*char_to_pos)(char), int charcount);
void destroy_word_list(Word_list_ptr list, int free_data);
void *list_find_word(Word_list_ptr list, char *word);
void *list_find_word_nolock(Word_list_ptr list, char *word);
void *list_find_and_execute(Word_list_ptr list, char *word, void *(*match_fun)(void *, void *), void *(*nomatch_fun)(void *), void *match_fargs, void *nomatch_fargs, int *success);
void *list_find_and_execute_globalock(Word_list_ptr list, char *word, void *(*match_fun)(void *, void *), void *(*nomatch_fun)(void *), void *match_fargs, void *nomatch_fargs, int *success);
int list_add(Word_list_ptr list, void *data, char *word);
int list_add_nolock(Word_list_ptr list, void *data, char *word);
void *list_delete(Word_list_ptr list, char *word);
void *list_delete_nolock(Word_list_ptr list, char *word);

#endif /* __YAIRCD_GENERIC_LIST_GUARD__ */
