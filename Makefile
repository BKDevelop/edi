edi: editor.c
	clang -o edi.o -Wall -Wextra -pedantic editor.c

format:
	clang-format -i editor.c

.PHONY: clean

clean: 
	rm edi.o

