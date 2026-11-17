#pragma once
#include "kbl/linux_boot.hpp"

// Optional OS-project extension, linked only when --linux-adapter is supplied.
// It may validate the input and customize LinuxTree; it must not modify input.
extern "C" bool kbl_prepare_linux(const kbl::Fdt& input, kbl::LinuxTree& options,
                                   bool vm_test, const char*& error);
