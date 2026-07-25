CC = gcc
CFLAGS = -Wall -Iinclude
LDLIBS = -lpdh -liphlpapi -luser32 -lpsapi -lgdi32 -lshell32

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

monitor.exe: $(OBJ)
	$(CC) $(OBJ) -o monitor.exe $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o monitor.exe
