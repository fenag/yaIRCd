DOXYGEN_CONFIG_PATH = ../doc/Doxyfile
DOC_DIRS = ../doc/html and ../doc/latex
BINARY_NAME = yaircd.out
FILES = client.c client_list.c yaircd.c
CC = gcc
CFLAGS = -o $(BINARY_NAME) -Wall
# INCLUDES = -Iinclude/ In the future
LIBS = -lpthread -lev
COMPILE = $(CC) $(CFLAGS) $(LIBS)

all: $(FILES)
	$(COMPILE) $(FILES)

doc:
	doxygen $(DOXYGEN_CONFIG_PATH)
	@echo "------------------------------------------------------------------"
	@echo "Documentation was successfully generated. Have a look at $(DOC_DIRS)"
	
clean:
	rm -f *.o