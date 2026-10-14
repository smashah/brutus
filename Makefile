VERSION := $(shell cat VERSION)
PREFIX ?= $(HOME)/.local
CC ?= cc
CFLAGS ?= -Os
FRAMEWORKS = -framework IOKit -framework CoreFoundation

.PHONY: all install uninstall dist clean
all: brutus

brutus: brutus.c VERSION
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBRUTUS_VERSION='"$(VERSION)"' brutus.c -o $@ $(LDFLAGS) $(FRAMEWORKS)

install: brutus
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	install -S -m 755 brutus "$(DESTDIR)$(PREFIX)/bin/brutus"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/brutus"

dist:
	./scripts/package-release.sh

clean:
	rm -f brutus brutus.next
