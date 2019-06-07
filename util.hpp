#pragma once

#include <cstdlib>

int get_env_as_int(const char* name); // -1 on non-exist

extern bool is_dbg;

#define IF_DEBUG(stmt) if (is_dbg) { stmt }
