#pragma once

#include "basic_file_monitor.hpp"

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  include "polling/basic_file_monitor_service.hpp"
#elif defined(__APPLE__) && defined(__MACH__)
#  include "kqueue/basic_file_monitor_service.hpp"
#else
// fallback method
#  include "polling/basic_file_monitor_service.hpp"
#endif




namespace filemonitor {

typedef basic_file_monitor< basic_file_monitor_service <> > file_monitor;

}
