CC=g++
LIBSOCKET=-lnsl
CCFLAGS=-std=c++11 -pthread -Wall -Wextra -g
PROXY=httpproxy

all: build

build: $(PROXY)

$(PROXY):$(PROXY).cpp
	$(CC) -o $(PROXY) $(CCFLAGS) $(LIBSOCKET) $(PROXY).cpp

clean:
	rm -f *.o *~
	rm -f $(PROXY)
