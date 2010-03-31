CXXFLAGS=-O0 -ggdb
CXXFLAGS+=-Wall -Wextra
HEADERS=$(wildcard *.h)
BINARY=tempest

all: $(BINARY)

$(BINARY): $(BINARY).cc $(HEADERS)
	g++ $(CXXFLAGS) -o $@ $<

clean:
	rm $(BINARY)

.PHONY: all clean

