# Linux desktop bring-up

The diagnostic kernel boots on SM-X110 and runs its own PID 1. The next target
is a native ARM64 desktop with a browser, USB keyboard/mouse and touchscreen.
Windows bring-up follows a usable Linux system.

The current probe deliberately omits storage, USB, networking and touch drivers.
Its working text framebuffer does not establish DRM modesetting, GPU acceleration
or desktop readiness. A full root filesystem also needs a storage path larger
than the existing 64 MiB BOOT image; the diagnostic boot does not add partitions.

## Hardware foundation

Two kernel paths need evaluation: extending the upstream probe with MT8781/MT6789
platform support, or running Linux userspace on the matched Samsung kernel with
its existing drivers. The existing recovery port already demonstrates display,
touchscreen and USB device operation with the latter kernel. That makes its
driver/module inventory useful input; it does not establish a working desktop.

The first desktop milestone covers:

1. A native ARM64 root filesystem and a reliable boot/return path.
2. USB role switching, host-mode power and keyboard/mouse input.
3. Touchscreen events exposed through normal Linux input interfaces.
4. A display compositor using software rendering and a browser showing a page.
5. Networking, beginning with a verified interface, then onboard Wi-Fi.

Root filesystem placement and persistent storage are separate from the BOOT
loader. Neither repartitioning nor a PIT update is assumed by this design.

## Graphics and games

Start with software rendering for the compositor/browser. GPU acceleration is
a later, separately verified layer. Mesa's [Panfrost documentation](https://docs.mesa3d.org/drivers/panfrost.html)
lists Mali-G57 OpenGL support. That does not establish this tablet's display,
power/clock or kernel-driver integration. Vulkan support must also be checked
for the particular GPU; the current Mesa table does not list Vulkan for G57.

Reuse Steam's compatibility stack when the system can support it. Valve's
[Proton changelog](https://github.com/ValveSoftware/Proton/wiki/Changelog)
documents FEX integration for ARM64EC builds. A working browser or software
renderer is not a game-compatibility or performance result. Steam and individual
games need their own tests after graphics and networking work.

## Touch behavior

Keep the kernel driver focused on reporting touch. A userspace input mapper
can provide direct touch on the desktop/browser and relative mouse movement
for games. Linux [uinput](https://www.kernel.org/doc/html/latest/input/uinput.html)
provides the virtual-input interface for this. Per-application switching needs
desktop/compositor integration and a manual override. It is not implemented yet.

USB host mode, touch, Wi-Fi, GPU acceleration and the desktop remain unverified
until their individual device tests pass.
