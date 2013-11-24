DOXYGEN_CONFIG_PATH = ../doc/Doxyfile
DOC_DIRS = ../doc/html and ../doc/latex
BINARY_NAME = yaircd.out
FILES = clients/client.c clients/client_list.c clients/client_queue.c yaircd.c msg/parsemsg.c msg/msgio.c msg/interpretmsg.c trie/trie.c cloak/cloak.c lists/list.c channel/channel.c serverinfo.c
CC = gcc
CFLAGS = -o $(BINARY_NAME) -Wall
INCLUDES = -Iinclude
LIBS = -lpthread -lev -lssl -lcrypto -lconfig
COMPILE = $(CC) $(CFLAGS) $(INCLUDES) 

all: $(FILES)
	$(COMPILE) $(FILES) $(LIBS)

doc:
	doxygen $(DOXYGEN_CONFIG_PATH)
	@echo "------------------------------------------------------------------"
	@echo "Documentation was successfully generated. Have a look at $(DOC_DIRS)"
	
clean:
	rm -f *.o
