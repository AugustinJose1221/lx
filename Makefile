# Convenience wrapper; the real build lives in source/Makefile.
all:
	$(MAKE) -C source

test:
	$(MAKE) -C source test

install:
	$(MAKE) -C source install

clean:
	$(MAKE) -C source clean

.PHONY: all test install clean
