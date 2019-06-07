#include "perf.hpp"
#include "unistd.h"
#include "sys/syscall.h"
#include "sys/types.h"
#include "sys/mman.h"

#include "util.hpp"

#include <iostream>
#include <iomanip>
#include <mutex>
#include <string>
#include <atomic>
#include <algorithm>
#include <thread>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <experimental/filesystem>

#include "tbb/concurrent_unordered_map.h"
#include "tbb/concurrent_unordered_set.h"

namespace fs = std::experimental::filesystem;

using namespace std::chrono_literals;

namespace {
	int gettid() {
		return syscall(__NR_gettid);
	}


	// 4K = 2^12
	static constexpr uintptr_t PAGE_MASK = ~0xfffULL, PAGE_SHIFT = 12;

	static constexpr uintptr_t HUGEPAGE_MASK = ~0x1fffffULL, HUGEPAGE_SHIFT = 21;

	const char* NAME_ALL_STORES = "MEM_INST_RETIRED:ALL_STORES",
		  *NAME_ALL_LOADS = "MEM_INST_RETIRED:ALL_LOADS",
		  *NAME_TLB_MISS_STORES = "MEM_INST_RETIRED:STLB_MISS_STORES",
		  *NAME_TLB_MISS_LOADS = "MEM_INST_RETIRED:STLB_MISS_LOADS";

	class Tracer {
		int fd;
		void* mmap_ptr;
		std::thread scanner;
		std::mutex blacklist_mutex;
		std::vector<pid_t> blacklist;
		std::mutex monitoring_tids_mutex;
		std::vector<pid_t> monitoring_tids;
		tbb::concurrent_unordered_map<std::uintptr_t, std::atomic<std::uint64_t>>
			m_read,
			m_write,
			m_read_tlb_miss,
			m_write_tlb_miss,
			m_all,
			m_all_tlb_miss;
		tbb::concurrent_unordered_map<std::uintptr_t, std::atomic<std::uint64_t>>
			m_huge_read,
			m_huge_write,
			m_huge_read_tlb_miss,
			m_huge_write_tlb_miss,
			m_huge_all,
			m_huge_all_tlb_miss;
		tbb::concurrent_unordered_set<std::uintptr_t> promoted_huge_pages;

		template<typename KT, typename VT>
		static void add_atomic_map(tbb::concurrent_unordered_map<KT, VT>& m,
								   KT k) {
			auto it = m.find(k);
			if (it == m.end()) {
				m.emplace(k, 1);
			} else {
				it->second.fetch_add(1, std::memory_order_relaxed);
			}
		}
		
		void reset_all_map()
		{
			m_write.clear();
			m_read.clear();
			m_all_tlb_miss.clear();
			m_huge_all_tlb_miss.clear();
			m_all.clear();
			m_huge_read.clear();
			m_huge_write.clear();
			m_huge_read_tlb_miss.clear();
			m_huge_write_tlb_miss.clear();
		}

		void handle_result(const char* evt_name, void* addr) {
			using namespace std;
			uintptr_t addr_v = (uintptr_t)addr;
			uintptr_t vpn = addr_v >> PAGE_SHIFT;
			uintptr_t hvpn = addr_v >> HUGEPAGE_SHIFT;
			add_atomic_map(m_all, vpn);
			add_atomic_map(m_huge_all, hvpn);
			if (evt_name == NAME_ALL_LOADS) {
				add_atomic_map(m_read, vpn);
				add_atomic_map(m_huge_read, hvpn);
			} else if (evt_name == NAME_ALL_STORES) {
				add_atomic_map(m_write, vpn);
				add_atomic_map(m_huge_write, hvpn);
			} else if (evt_name == NAME_TLB_MISS_LOADS) {
				add_atomic_map(m_read_tlb_miss, vpn);
				add_atomic_map(m_all_tlb_miss, vpn);
				add_atomic_map(m_huge_read_tlb_miss, hvpn);
				add_atomic_map(m_huge_all_tlb_miss, hvpn);
			} else if (evt_name == NAME_TLB_MISS_STORES) {
				add_atomic_map(m_write, vpn);
				add_atomic_map(m_all_tlb_miss, vpn);
				add_atomic_map(m_huge_write, hvpn);
				add_atomic_map(m_huge_all_tlb_miss, hvpn);
			}
		}

		void processing_routine(pid_t tid, const char* evt_name) {
			using namespace std;
			{
				lock_guard<mutex> lk(blacklist_mutex);
				if (find(blacklist.begin(), blacklist.end(), gettid()) == blacklist.end())
					blacklist.push_back(gettid());
			}
			auto res = open_perf(evt_name, tid);
			IF_DEBUG(cerr << "Open result = " << res.fd << " " << res.mmap_buf << endl;)
			if (!res.fd) return;
			run_perf(res.fd, res.mmap_buf, [evt_name, this](void* addr) {
				handle_result(evt_name, addr);
			});
		}

		static void promote_to_hugepage(void* addr) {
			using namespace std;
			int ret = madvise(addr, 1 << HUGEPAGE_SHIFT, MADV_HUGEPAGE);
			IF_DEBUG(cerr << "Merging " << hex << addr << " |> " << dec << ret << endl;)
			IF_DEBUG(if (ret < 0) perror("madvise:");)
		}

		static void unmerge_hugepage(void* addr) {
			madvise(addr, 1 << HUGEPAGE_SHIFT, MADV_NOHUGEPAGE);
		}

		enum class MergePolicy {
			ABOVE_THRESHOLD
		} merge_policy;

		static MergePolicy get_merge_policy_from_env() {
			const char* name = getenv("HPT_MERGE_POLICY");
			if (!name || !strcmp(name, "ABOVE_THRESHOLD"))
				return MergePolicy::ABOVE_THRESHOLD;
			return MergePolicy::ABOVE_THRESHOLD;
		}

		const char* current_merge_policy_str() {
			switch (merge_policy) {
				case MergePolicy::ABOVE_THRESHOLD:
					return "ABOVE_THRESHOLD";
				default:
					return "UNKNOWN";
			}
		}

		void analyze_and_promote_above_threshold() {
			using namespace std;
			int max_unpromoted = 0;
			uintptr_t max_unpromoted_hugepage = -1;
			
			int merge_threshold = get_env_as_int("HPT_THRESHOLD");
			if (merge_threshold == -1)
				merge_threshold = 100000;
			for (auto&& pg : m_huge_all) {
				if (pg.second > max_unpromoted && promoted_huge_pages.find(pg.first) == promoted_huge_pages.end())
				{
					max_unpromoted = pg.second;
					max_unpromoted_hugepage = pg.first;
				}
			}
			if (max_unpromoted_hugepage != -1) {
				if (max_unpromoted <= merge_threshold) return;
				IF_DEBUG(cerr << "Merging " << hex << max_unpromoted_hugepage << " with usage cnt " << dec << max_unpromoted << endl;)
				for (int i = 0; i < 2 * 1024 * 1024 / (4 * 1024); ++i)
				{
					promote_to_hugepage((void*)((max_unpromoted_hugepage << HUGEPAGE_SHIFT) + (i << PAGE_SHIFT)));
				}
				promoted_huge_pages.insert(max_unpromoted_hugepage);
			}
		}

		void analyze_and_promote() {
			if (merge_policy == MergePolicy::ABOVE_THRESHOLD)
				analyze_and_promote_above_threshold();
			reset_all_map();
		}

		// scan 
		void scanner_routine() {
			using namespace std;
			cerr << " Scanner routine launched!" << endl;
			blacklist.push_back(gettid());
			for (;;) {
				int sleep_time = get_env_as_int("HPT_INTERVAL");
				if (sleep_time == -1)
					sleep_time = 1000;
				std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
				for (auto&& p : fs::directory_iterator("/proc/self/task/"))
				{
					std::string name = p.path().filename();
					int ch_tid = std::atoi(name.c_str());
					bool found = true;
					{
						std::lock_guard<std::mutex> lk(blacklist_mutex);
						found = std::find(blacklist.begin(), blacklist.end(), ch_tid) != blacklist.end();
					}
					if (!found) {
						std::lock_guard<std::mutex> lk(monitoring_tids_mutex);
						if (std::find(monitoring_tids.begin(), monitoring_tids.end(), ch_tid) == monitoring_tids.end()) {
							monitoring_tids.push_back(ch_tid);
							cerr << "Adding tid = " << ch_tid << " to monitored routines" << endl;
							{
								thread* t = new thread([ch_tid, this]() {
									processing_routine(ch_tid, NAME_TLB_MISS_LOADS);
								});
								t->detach();
								t = new thread([ch_tid, this]() {
									processing_routine(ch_tid, NAME_TLB_MISS_STORES);
								});
								t->detach();
								t = new thread([ch_tid, this]() {
									processing_routine(ch_tid, NAME_ALL_LOADS);
								});
								t->detach();
								t = new thread([ch_tid, this]() {
									processing_routine(ch_tid, NAME_ALL_STORES);
								});
								t->detach();
							}
						}
					}
				}

				analyze_and_promote();
			}
		}

		public:
		Tracer() {
			init_perf();
			merge_policy = get_merge_policy_from_env();
			std::cerr << "Current merge policy: " << current_merge_policy_str() << std::endl;
			std::thread t([this]() { scanner_routine(); });
			t.detach();
		}
	};

	static Tracer tracer_s;
}
