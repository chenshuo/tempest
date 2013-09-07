CXXFLAGS=-O0 -ggdb
CXXFLAGS+=-Wall -Wextra
LDLIBS+=-lreadline

all: tempest

tempest: tempest.cc

clean:
	rm tempest

.PHONY: all clean

