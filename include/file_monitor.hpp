#pragma once

#include "basic_file_monitor.hpp"
/*
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  include "windows/basic_file_monitor_service.hpp"
//#elif defined(linux) || defined(__linux) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)
//#  include "inotify/basic_file_monitor_service.hpp"
#elif defined(__APPLE__) && defined(__MACH__)
#  include "kqueue/basic_file_monitor_service.hpp"
#elif defined(__FreeBSD__)
// NOT tested on FreeBSD yet
#  include "kqueue/basic_file_monitor_service.hpp"
#else
// dumb polling, fallback method
#  include "polling/basic_file_monitor_service.hpp"
#endif
*/


//#  include "kqueue/basic_file_monitor_service.hpp"
#  include "polling/basic_file_monitor_service.hpp"



namespace filemonitor {

typedef basic_file_monitor< basic_file_monitor_service <> > file_monitor;

}
