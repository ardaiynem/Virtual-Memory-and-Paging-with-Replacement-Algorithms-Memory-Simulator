all: memsim

memsim: memsim.c linkedList
	gcc -Wall -g -o memsim memsim.c linkedList.c linkedList.h -lm

linkedList: linkedList.c linkedList.h
	
clean:
	rm -fr memsim memsim.o *~
