// The packager leaves linux_banner untouched. Only executing this C++ function
// changes the banner that the original kernel later prints into its log.
extern "C" void entry_control_cpp(volatile char* banner, volatile char* proc_version_word) {
    constexpr char marker[] = "KBL bootstrap ";
    static_assert(sizeof(marker) - 1 == 14);
    for (unsigned i = 0; i < sizeof(marker) - 1; ++i) banner[i] = marker[i];
    constexpr char readable_marker[] = "KBL_CPP";
    for (unsigned i = 0; i < sizeof(readable_marker) - 1; ++i) proc_version_word[i] = readable_marker[i];
}
