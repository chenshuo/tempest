CXXFLAGS=-O0 -ggdb -std=c++11
CXXFLAGS+=-Wall -Wextra
CXXFLAGS+=-I/usr/local/include
LDLIBS+=-lreadline
LDFLAGS+=-L/usr/local/lib

all: tempest

tempest: tempest.cc

clean:
	rm tempest

.PHONY: all clean

