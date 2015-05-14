CPLUSPLUS?=g++
CPPFLAGS=-std=c++11 -Wall -W
ifeq ($(NDEBUG),1)
CPPFLAGS+= -O3
else
#CPPFLAGS+=
endif
LDFLAGS=-pthread

PWD=$(shell pwd)
SRC=$(wildcard *.cc)
BIN=$(SRC:%.cc=.noshit/%)

OS=$(shell uname)
ifeq ($(OS),Darwin)
  CPPFLAGS+= -stdlib=libc++ -x objective-c++ -fobjc-arc
  LDFLAGS+= -framework Foundation
endif

.PHONY: all clean update indent

LOGS_FILENAME="/var/log/current.jsonlines"

all: build/code
	[ -f ${LOGS_FILENAME} ] && tail -n +1 -f ${LOGS_FILENAME} | ./build/code || echo "Build successful."

build/code: build code.cc

build:
	mkdir -p build

clean:
	rm -rf build

build/%: %.cc
	${CPLUSPLUS} ${CPPFLAGS} -o $@ $< ${LDFLAGS}

update:
	(cd .. ; git submodule update --init --recursive)

indent:
	../../Bricks/KnowSheet/scripts/indent.sh
