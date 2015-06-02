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

all: build build/browser build/gen_insights build/v2

serve: build build/v2
	[ -f ${LOGS_FILENAME} ] && tail -n +1 -f ${LOGS_FILENAME} | ./build/v2 || echo "Build successful."

s: serve

browse: build build/browser
	./build/browser

b: browse

build/v2: build v2.cc

build:
	mkdir -p build

clean:
	rm -rf build

build/%: %.cc *.h
	${CPLUSPLUS} ${CPPFLAGS} -o $@ $< ${LDFLAGS}

update:
	(cd .. ; git submodule update --init --recursive)

indent:
	../../Current/scripts/indent.sh

check:
	../../Current/scripts/check-all-headers.sh
