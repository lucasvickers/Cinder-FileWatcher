#pragma once

#include "basic_file_monitor.hpp"
#include "basic_file_monitor_service.hpp"

namespace filemonitor {

typedef basic_file_monitor< basic_file_monitor_service <> > file_monitor;

}
