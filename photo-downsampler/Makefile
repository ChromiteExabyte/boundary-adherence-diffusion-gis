CC      ?= cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Isrc -Ivendor
LDFLAGS := -lm
SRCS    := src/main.c src/image.c src/resample.c src/effects.c
TARGET  := photodown

.PHONY: all clean install

all: $(TARGET)

.gitignore:
	echo "$(TARGET)" > $@

$(TARGET): $(SRCS) vendor/stb_image.h vendor/stb_image_write.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)
