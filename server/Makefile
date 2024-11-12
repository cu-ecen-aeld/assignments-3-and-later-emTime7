CC ?= gcc
CFLAGS ?= -Wall -Werror

all:
	$(CROSS_COMPILE)$(CC) aesdsocket.c $(CFLAGS) -lpthread -g -o aesdsocket

clean:
	rm -f aesdsocket *.o

