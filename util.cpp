#include "util.hpp"

#include <cstdlib>
#include <cstring>
// -1 as not exist
int get_env_as_int(const char* name) {
	using namespace std;
	const char* val = getenv(name);
	if (!val) return -1;
	int ret = atoi(val);
	if (strcmp(val, "0") && !ret) return -1;
	return ret;
}

bool is_dbg = getenv("HPT_DEBUG");
