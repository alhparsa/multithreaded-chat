all:
	gcc -Wall -Werror -pthread main.c receiver.c sender.c instructorList.o -o s-talk 
cleanup:
	rm s-talk
bidirectional:
	./s-talk 3000 localhost 3000
valgrind:
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./s-talk 3000 localhost 3000