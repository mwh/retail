include Makefile.conf

all: retail
retail: retail.c plat.c
	$(CC) -o retail retail.c

install: all
	install -m 0755 retail $(DESTDIR)$(PREFIX)/bin/retail

Makefile.conf:
	./configure
