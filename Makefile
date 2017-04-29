# Copyright (C) 2016, All rights reserved.
# Author: contem

BIN = pud pudclient

COMMON_SRCS = bignum.cc getopt.cc node.cc peer.cc proto.cc relay.cc \
              rsa.cc server.cc sha256.cc socket.cc util.cc
COMMON_OBJS = $(COMMON_SRCS:.cc=.o)
COMMON_DEPS = $(COMMON_SRCS:.cc=.d)

DAEMON_SRCS = pud.cc
DAEMON_OBJS = $(DAEMON_SRCS:.cc=.o)
DAEMON_DEPS = $(DAEMON_SRCS:.cc=.d)

CLIENT_SRCS = pudclient.cc
CLIENT_OBJS = $(CLIENT_SRCS:.cc=.o)
CLIENT_DEPS = $(CLIENT_SRCS:.cc=.d)

LINK_FLAGS = -pthread
COMP_FLAGS = --std=c++14 -iquote . 

all: ${BIN}

${COMMON_OBJS}: ${COMMON_DEPS}
${DAEMON_OBJS}: ${DAEMON_DEPS}
${CLIENT_OBJS}: ${CLIENT_DEPS}

pud: ${COMMON_OBJS} ${DAEMON_OBJS}
	${CXX} -o $@ ${LINK_FLAGS} $^

pudclient: ${COMMON_OBJS} ${CLIENT_OBJS}
	${CXX} -o $@ ${LINK_FLAGS} $^

%.d: %.cc
	${CXX} -M $< ${COMP_FLAGS} > $@

%.o: %.cc
	${CXX} -c $< -o $@ ${COMP_FLAGS}
       
clean:
	rm -f ${BIN} ${COMMON_OBJS} ${COMMON_DEPS} \
	    ${DAEMON_OBJS} ${DAEMON_DEPS} \
	    ${CLIENT_OBJS} ${CLIENT_DEPS}

