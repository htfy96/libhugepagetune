#include "perf.hpp"
#include "util.hpp"
#include <iostream>
#include <vector>
#include <perfmon/pfmlib.h>
#include <cassert>
#include <linux/perf_event.h>
#include <sys/types.h>
#include <linux/hw_breakpoint.h>
#include <perfmon/pfmlib_perf_event.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <atomic>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <sys/mman.h>
#include <emmintrin.h>

using namespace std;

namespace {
template <typename T>
T read_out_of_order_atomic(const T* ptr) {
  T res;
  const char* src = reinterpret_cast<const char*>(ptr);
  char* dst = reinterpret_cast<char*>(&res);
  for (size_t i = 0; i < sizeof(T); ++i) {
    dst[i] = __atomic_load_n(&src[i], __ATOMIC_RELAXED);
  }
  return res;
}

template<typename T>
static T read_with_seqlock(T* ptr)
{
	T res;
	int lk;
	do {
		lk = __atomic_load_n(&ptr->lock, __ATOMIC_ACQUIRE);
		if ((lk & 1) != 0) {
			_mm_pause();
			continue;
		}
		atomic_thread_fence(memory_order_acq_rel);
		res = read_out_of_order_atomic(ptr);
		atomic_thread_fence(memory_order_acq_rel);
	} while (((lk & 1) != 0) || __atomic_load_n(&ptr->lock, __ATOMIC_RELAXED) != lk);
	return res;
}
}

struct __attribute__((packed)) MemSamplingRecord {
	perf_event_header hdr;
	void* addr;
};

const int buf_sz = 0x1000000;

void init_perf() 
{
	if (PFM_SUCCESS != pfm_initialize()) {
		cerr << "init failed" << endl;
		abort();
	}
}

PerfOpenResult open_perf(const char* event, int tid) {
	pfm_perf_encode_arg_t encode_arg {};
	perf_event_attr perf_attr {};
	encode_arg.attr = &perf_attr;
	encode_arg.size = sizeof(pfm_perf_encode_arg_t);
	char* str;
	encode_arg.fstr = &str;
	pfm_get_os_event_encoding(event, PFM_PLM3, PFM_OS_PERF_EVENT, &encode_arg);
	assert(encode_arg.idx > 0);
	cerr << *encode_arg.fstr << " " << endl;
	perf_attr.type = PERF_TYPE_RAW;
	perf_attr.size = sizeof(perf_event_attr);
	perf_attr.sample_period = get_env_as_int("HPT_SAMPLE_PERIOD");
	if (perf_attr.sample_period < 0)
		perf_attr.sample_period = 0x1000;
	perf_attr.sample_type = PERF_SAMPLE_ADDR;
	perf_attr.read_format = 0;
	perf_attr.wakeup_events = get_env_as_int("HPT_WAKEUP_EVENTS");
	if (perf_attr.wakeup_events == -1)
		perf_attr.wakeup_events = 0x10000;
	perf_attr.disabled = 1;
	perf_attr.exclude_kernel = 1;
	perf_attr.precise_ip = 1;
	// perf_attr.inherit = 1;
	perf_attr.pinned = 0;
	perf_attr.exclusive = 0;

	int perf_evt_fd = 
		perf_event_open(&perf_attr, tid, -1, -1, PERF_FLAG_FD_CLOEXEC);
	if (perf_evt_fd <= 0)
	{
		perror("perf_event_open");
		return PerfOpenResult{0, nullptr};
	}
	cout << perf_evt_fd << endl;
	void* dt = mmap(nullptr, 4096 + buf_sz, PROT_READ | PROT_WRITE, MAP_SHARED, perf_evt_fd, 0);
	if (dt == MAP_FAILED)
	{
		perror("mmap");
		close(perf_evt_fd);
		return PerfOpenResult{0, nullptr};
	}
	assert(dt != MAP_FAILED);
	return PerfOpenResult{perf_evt_fd, dt};
}

static void cleanup(int perf_evt_fd, void* dt) {
	close(perf_evt_fd);
	munmap(dt, 4096 + buf_sz);
}

void run_perf(int perf_evt_fd, void* dt, function<void(void*)> f)
{
	pollfd fds[1] = {{
		.fd = perf_evt_fd,
		.events = POLLIN,
		.revents = 0
	}};

	ioctl(perf_evt_fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(perf_evt_fd, PERF_EVENT_IOC_ENABLE, 0);
	std::atomic_thread_fence(memory_order_acq_rel);

		uint64_t last_head = 0;
	for (;;) {
		if (poll(fds, 1, -1) < 0) {
			perror("Poll failed");
			cleanup(perf_evt_fd, dt);
			return;
		}
		if (fds[0].revents & POLLERR)
		{
			perror("pollerr");
			cleanup(perf_evt_fd, dt);
			return;
		}
		if (fds[0].revents & POLLHUP)
		{
			perror("pollhup");
			cleanup(perf_evt_fd, dt);
			return;
		}
		if (fds[0].revents & POLLNVAL)
		{
			perror("pollinval");
			cleanup(perf_evt_fd, dt);
			return;
		}
		volatile perf_event_mmap_page* desc_page_ptr = (volatile perf_event_mmap_page*)dt;

		if (__atomic_load_n(&desc_page_ptr->data_head, __ATOMIC_ACQUIRE) == last_head) continue;
		std::atomic_thread_fence(memory_order_acquire);
		IF_DEBUG(cerr << "uoffset = " << desc_page_ptr->data_head << endl;)
			
		uint64_t i;
		for (i = last_head; i != desc_page_ptr->data_head;)
		{
			std::atomic_thread_fence(memory_order_release);
			uint64_t didx = i % buf_sz;
			void* data_begin = ((char*)dt + 4096 + didx);
			MemSamplingRecord rec = *(MemSamplingRecord*)(data_begin);
			if (rec.hdr.type != PERF_RECORD_SAMPLE) goto NXT;
			if (rec.addr)
			{
				f(rec.addr);
			}
NXT:
			i += rec.hdr.size;
			std::atomic_thread_fence(memory_order_release);
		}
		std::atomic_thread_fence(std::memory_order_acq_rel);
		desc_page_ptr->data_tail = i;
		last_head = i;
		std::atomic_thread_fence(std::memory_order_acq_rel);
	}
}
