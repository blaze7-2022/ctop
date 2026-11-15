CC = clang
CFLAGS = -O2 -Wall $(shell pkg-config --cflags ncursesw)
LDFLAGS = $(shell pkg-config --libs ncursesw)
TARGET = ctop
PREFIX = /usr/local

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) main.c -o $(TARGET) $(LDFLAGS)

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)
