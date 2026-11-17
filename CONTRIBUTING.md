# Contributing

Build the host tools and run CTest before submitting core or adapter changes.
For firmware changes, also run the relevant ARM64 VM fixture. Build commands
are in [README.md](README.md); hardware-specific checks are in the device guides.

Report the device model, firmware baseline, KBL version, selected entry and
observed result. Distinguish file inspection, VM execution and hardware testing.
Do not include raw device dumps, credentials, device identifiers or signing keys.

Keep the menu policy and bounded image/tree handling in the portable core.
Device-register access belongs in its target adapter. A passing VM fixture does
not establish compatibility with a new device or firmware build.

New target ports should document their memory ownership, entry state, display,
input, Android handoff and recovery route. Preserve applicable source and asset
license notices. Proprietary firmware inputs remain outside the source tree.
