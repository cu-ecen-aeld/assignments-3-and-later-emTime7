CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt
all:
	$(CC) $(CFLAGS) $(LDFLAGS) $(TARGET).c -o $(TARGET)

clean:
	rm -f $(TARGET) *.o

