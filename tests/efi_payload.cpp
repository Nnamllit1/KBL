#include "../platforms/uefi/efi.hpp"
extern "C" efi::Status EFIAPI efi_main(efi::Handle, efi::System* system) {
    system->output->write(system->output, u"KBL_CHAINLOAD_OK\r\n");
    return efi::success;
}

