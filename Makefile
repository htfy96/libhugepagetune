CXX ?= g++
LIBS ?= -lpfm -pthread
DBGFLAGS ?= -g3
OPTFLAGS ?= -O3 -march=native
CXXFLAGS ?= $(DBGFLAGS) $(OPTFLAGS)

test-record-data: test-record-data.o
	$(CXX) $^ -o $@ $(CXXFLAGS) $(LIBS)

clean:
	rm -rf *.o test-record-data

.PHONY: clean
