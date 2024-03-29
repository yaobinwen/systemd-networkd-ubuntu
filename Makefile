all:
	@if [ -n "$(NINJA_VERBOSE)" ]; then \
		ninja -C build -v; \
	else \
		ninja -C build; \
	fi

install:
	DESTDIR=$(DESTDIR) ninja -C build install
