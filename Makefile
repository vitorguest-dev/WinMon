CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
LDFLAGS ?= -mwindows
LDLIBS = -lpdh -liphlpapi -lws2_32 -luser32 -lpsapi -lgdi32 -lshell32 -lcomctl32 -lole32 -loleaut32 -lwbemuuid -ladvapi32 -lcomdlg32 -lwindowscodecs -ldnsapi -luuid
TARGET = build/WinMon.exe
SOURCE = src/main.c

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(SOURCE) $(wildcard src/modules/*.inc)
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCE) $(LDLIBS)

clean:
	rm -rf build
