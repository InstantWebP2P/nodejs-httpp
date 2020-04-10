C++ = g++

ifndef os
   os = LINUX
endif

ifndef arch
   arch = x86_64 
endif

CCFLAGS = -fPIC -Wall -Wextra -D$(os) -finline-functions -O3 -fno-strict-aliasing -fvisibility=hidden -DEVPIPE_OSFD

ifeq ($(arch), IA32)
   CCFLAGS += -DIA32
endif

ifeq ($(arch), POWERPC)
   CCFLAGS += -mcpu=powerpc
endif

ifeq ($(arch), SPARC)
   CCFLAGS += -DSPARC
endif

ifeq ($(arch), IA64)
   CCFLAGS += -DIA64
endif

ifeq ($(arch), AMD64)
   CCFLAGS += -DAMD64
endif

ifeq ($(arch), x86_64)
   CCFLAGS += -Dx86_64
endif

OBJS = api.o buffer.o cache.o ccc.o channel.o common.o udt_core.o epoll.o list.o md5.o packet.o queue.o window.o udtc.o
DIR = $(shell pwd)

all: libudt.so libudt.a udt

udt_core.o: udt_core.cpp core.h udt.h
	$(C++) $(CCFLAGS) $< -c

%.o: %.cpp %.h udt.h
	$(C++) $(CCFLAGS) $< -c

libudt.so: $(OBJS)
ifneq ($(os), OSX)
	$(C++) -shared -o $@ $^
else
	$(C++) -dynamiclib -o libudt.dylib -lstdc++ -lpthread -lm $^
endif

libudt.a: $(OBJS)
	ar -rcs $@ $^

udt:
	cp udt.h udt

clean:
	rm -f *.o *.so *.dylib *.a udt

install:
	export LD_LIBRARY_PATH=$(DIR):$$LD_LIBRARY_PATH
