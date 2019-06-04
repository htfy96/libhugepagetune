CXX ?= g++
LIBS ?= -lpfm -pthread
DBGFLAGS ?= -g3
OPTFLAGS ?= -O3 -march=native
CXXFLAGS ?= $(DBGFLAGS) $(OPTFLAGS)

test-record-data: test-record-data.o perf.o
	$(CXX) $^ -o $@ $(CXXFLAGS) $(LIBS)

perf.o: perf.cpp perf.hpp
	$(CXX) perf.cpp -c -o $@ $(CXXFLAGS)

clean:
	rm -rf *.o test-record-data

.PHONY: clean
