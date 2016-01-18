#pragma once

#include "BasicFileMonitor.hpp"
#include "BasicFileMonitorService.hpp"

namespace filemonitor {

typedef BasicFileMonitor< BasicFileMonitorService <> > FileMonitor;

}
