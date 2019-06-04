#include <unistd.h>
#include <sys/syscall.h>
#include "perf.hpp"
#include <thread>
#include <iostream>
#include <vector>

using namespace std;



int gettid() {
	return syscall(__NR_gettid);
}

int main()
{
	int tid =gettid();

#ifndef NO_SAMPLING
	thread t([tid]() {
			// auto res = open_perf("MEM_INST_RETIRED:ALL_STORES", tid);
			 auto res = open_perf("MEM_INST_RETIRED:ALL_LOADS", tid);
			 // auto res = open_perf("MEM_INST_RETIRED:STLB_MISS_STORES", tid);
			 run_perf(res.fd, res.mmap_buf, [](void* addr) {
					    static volatile uint64_t cnt = 0;
					    cnt++;
					    if (cnt % 1000000 == 0) cout << cnt << endl;
					 });
			});
	t.detach();
#endif
	vector<int> a;
	a.resize(0x1234567);
	for (int i=0; i<100000000; ++i)
	{
		for (unsigned int j=i % 0x1234567; a[j] < 100; j = ((unsigned long)j * 131UL + 25) % 0x1234567)
			++a[j];
	}
	return a[0x234567];
}
