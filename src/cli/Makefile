CFLAGS := -std=gnu99 -Wall -I../library/inc -I../library/inc/index -I../kernelmod -Iinc
LDFLAG := -lpthread -lanything

all: release debug

release: bin/release/anything_cli

bin/release/anything_cli: src/*.c
	mkdir -p $(shell dirname $@)
	rm -f $(shell dirname $@)/*.so
	ln -s $(shell realpath ../library/bin/release/libanything.so) $(shell dirname $@)/libanything.so
	gcc ${CFLAGS} -L$(shell dirname $@) -O3 $^ -o $@ ${LDFLAG}

debug: bin/debug/anything_cli

bin/debug/anything_cli: src/*.c
	mkdir -p $(shell dirname $@)
	rm -f $(shell dirname $@)/*.so
	ln -s $(shell realpath ../library/bin/debug/libanything.so) $(shell dirname $@)/libanything.so
	gcc ${CFLAGS} -L$(shell dirname $@) -DIDX_DEBUG -g $^ -o $@ ${LDFLAG}

clean:
	rm -Rf bin

.PHONY: all release debug clean
