C++ = g++

ifndef os
   os = LINUX
endif

ifndef arch
   arch = x86_64 
endif

CCFLAGS = -Wall -D$(os) -I../src -finline-functions -O3

ifeq ($(arch), IA32)
   CCFLAGS += -DIA32 #-mcpu=pentiumpro -march=pentiumpro -mmmx -msse
endif

ifeq ($(arch), POWERPC)
   CCFLAGS += -mcpu=powerpc
endif

ifeq ($(arch), IA64)
   CCFLAGS += -DIA64
endif

ifeq ($(arch), x86_64)
   CCFLAGS += -Dx86_64
endif

ifeq ($(arch), SPARC)
   CCFLAGS += -DSPARC
endif

LDFLAGS = -Wl,-rpath,../src -L../src -ludt -lstdc++ -lpthread -lm

ifeq ($(os), UNIX)
   LDFLAGS += -lsocket
endif

ifeq ($(os), SUNOS)
   LDFLAGS += -lrt -lsocket
endif

DIR = $(shell pwd)

APP = \
	appserver appclient sendfile recvfile test \
	appclient-node \
	appserver_c appclient_c \
	app2p appub appsub \
	httpp_clt \
	appserver_osfd appclient_osfd \
	appclient_hol appclient_hol1 appserver_hol

all: $(APP)

%.o: %.cpp
	$(C++) $(CCFLAGS) $< -c

appserver: appserver.o
	$(C++) $^ -o $@ $(LDFLAGS)
appclient: appclient.o
	$(C++) $^ -o $@ $(LDFLAGS)
sendfile: sendfile.o
	$(C++) $^ -o $@ $(LDFLAGS)
recvfile: recvfile.o
	$(C++) $^ -o $@ $(LDFLAGS)
test: test.o
	$(C++) $^ -o $@ $(LDFLAGS)

appclient-node: appclient-node.o
	$(C++) $^ -o $@ $(LDFLAGS)

app2p: app2p.o
	$(C++) $^ -o $@ $(LDFLAGS)

appub: appub.o
	$(C++) $^ -o $@ $(LDFLAGS)

appsub: appsub.o
	$(C++) $^ -o $@ $(LDFLAGS)

appserver_c: appserver_c.o
	$(C++) $^ -o $@ $(LDFLAGS)
appclient_c: appclient_c.o
	$(C++) $^ -o $@ $(LDFLAGS)

httpp_clt: httpp_clt.cpp
	$(C++) $^ -o $@ $(CCFLAGS) $(LDFLAGS)

appserver_osfd: appserver_osfd.o
	$(C++) $^ -o $@ $(LDFLAGS)
appclient_osfd: appclient_osfd.o
	$(C++) $^ -o $@ $(LDFLAGS)

appserver_hol: appserver_hol.o
	$(C++) $^ -o $@ $(LDFLAGS)
appclient_hol: appclient_hol.o
	$(C++) $^ -o $@ $(LDFLAGS)
appclient_hol1: appclient_hol1.o
	$(C++) $^ -o $@ $(LDFLAGS)

clean:
	rm -f *.o $(APP)

install:
	export PATH=$(DIR):$$PATH
