#pragma once

#include "basic_file_monitor.hpp"

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  error "Platform not supported."
//#  include "windows/basic_file_monitor_service.hpp"
#elif defined(linux) || defined(__linux) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)
#  error "Platform not supported."
//#  include "inotify/basic_file_monitor_service.hpp"
#elif defined(__APPLE__) && defined(__MACH__)
#  include "kqueue/basic_file_monitor_service.hpp"
#elif defined(__FreeBSD__)
// NOT tested
#  include "kqueue/basic_file_monitor_service.hpp"
#else
#  error "Platform not supported."
#endif

namespace boost {
namespace asio {

typedef basic_file_monitor< basic_file_monitor_service <> > file_monitor;

}
}
