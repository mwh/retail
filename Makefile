include Makefile.conf

all: retail
retail: retail.c Makefile.conf
	$(CC) -o retail $(DEFINES) retail.c

install: all
	install -m 0755 retail $(DESTDIR)$(PREFIX)/bin/retail

Makefile.conf:
	./configure
