#pragma once

#include <functional>

struct PerfOpenResult {
	int fd;
	void* mmap_buf;
};

PerfOpenResult open_perf(const char* event, int tid = -1);
void run_perf(int perf_evt_fd, void* mmap_buf, std::function<void(void*)>);
