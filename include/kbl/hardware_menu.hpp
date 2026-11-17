#pragma once
#include "kbl/core.hpp"

namespace kbl {
// The embedded stage has one preloaded Linux payload and no filesystem driver.
bool parse_hardware_config(Text source, bool linux_embedded, Config& out, Error& error);
}
