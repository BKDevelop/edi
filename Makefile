edi: editor.c
	clang -o edi.o -Wall -Wextra -pedantic editor.c

.PHONY: clean

clean: 
	rm -r target
