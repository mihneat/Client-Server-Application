DEFAULT_PORT=23356
OBJ_FILES=server.o client_tcp.o utils.o
CPPFLAGS=-Wall -Wextra

all: build

build: $(OBJ_FILES) bs bc

bs: 
	g++ server.o utils.o -o server -Wall -Wextra

bc:
	g++ client_tcp.o utils.o  -o subscriber -Wall -Wextra


server:
	g++ server.cpp utils.cpp -o server -Wall -Wextra

subscriber:
	g++ client_tcp.cpp utils.cpp -o subscriber -Wall -Wextra


rs:
	./server $(DEFAULT_PORT)

rc1:
	./subscriber ID_CL1 127.0.0.1 $(DEFAULT_PORT)

rc2:
	./subscriber ID_CL2 127.0.0.1 $(DEFAULT_PORT)

rc3:
	./subscriber ID_CL3 127.0.0.1 $(DEFAULT_PORT)


clean:
	rm -f *.o server subscriber
