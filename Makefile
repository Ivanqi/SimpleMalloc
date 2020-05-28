CC = gcc
TARGET = test
CFLAGS = -g -Wall -O0
$(TARGET): memlib.c mm.c test.c 
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(TARGET)