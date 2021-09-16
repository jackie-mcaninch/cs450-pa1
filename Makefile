make: lab.c
	gcc -Wall lab.c -o run

test: lab.c
	gcc -Wall lab.c -o run
	./run