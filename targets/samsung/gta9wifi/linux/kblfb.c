// SPDX-License-Identifier: GPL-2.0-only
/* Early text output on the framebuffer inherited from the SM-X110 loader.
 * This is a diagnostic console, not a panel, backlight or GPU driver.
 */
#include <linux/console.h>
#include <linux/font.h>
#include <linux/io.h>
#include <linux/libfdt.h>
#include <linux/of_fdt.h>
#include <linux/serial_core.h>
#include <linux/unaligned.h>
#include <asm/early_ioremap.h>

static phys_addr_t fb;
static u32 width, height, stride, stored_height, planes, cursor_x, cursor_y;
static u64 reserved;
static void __iomem *mapped;
static struct console *fb_console;

static void __iomem * __ref map_line(unsigned int plane, unsigned int y)
{
	u64 offset = ((u64)plane * stride * stored_height + (u64)y * stride) * 4;
	if (mapped)
		return mapped + offset;
	return early_ioremap(fb + offset, stride * 16 * 4);
}

static void __ref unmap_line(void __iomem *line)
{
	if (!mapped)
		early_iounmap(line, stride * 16 * 4);
}

static void __ref clear_line(void)
{
	unsigned int plane, y, x;
	for (plane = 0; plane < planes; plane++) {
		void __iomem *line = map_line(plane, cursor_y);
		if (!line)
			continue;
		for (y = 0; y < 16; y++)
			for (x = 0; x < width; x++)
				writel(0xff000000, line + (y * stride + x) * 4);
		unmap_line(line);
	}
}

static void __ref next_line(void)
{
	cursor_x = 0;
	cursor_y += 16;
	if (cursor_y + 16 > height)
		cursor_y = 0;
	clear_line();
}

static void __ref write_console(struct console *con, const char *s, unsigned int count)
{
	unsigned int i, plane, y, x;
	(void)con;
	for (i = 0; i < count; i++) {
		u8 ch = s[i];
		if (ch == '\n') { next_line(); continue; }
		if (ch == '\r') { cursor_x = 0; continue; }
		if (ch < 32 || ch > 126)
			ch = '?';
		if (cursor_x + 8 > width)
			next_line();
		for (plane = 0; plane < planes; plane++) {
			void __iomem *line = map_line(plane, cursor_y);
			if (!line)
				continue;
			for (y = 0; y < 16; y++) {
				u8 row = ((const u8 *)font_vga_8x16.data)[ch * 16 + y];
				for (x = 0; x < 8; x++)
					writel(row & (0x80 >> x) ? 0xffe0e0e0 : 0xff000000,
					       line + (y * stride + cursor_x + x) * 4);
			}
			unmap_line(line);
		}
		cursor_x += 8;
	}
}

static int __init setup_console(struct earlycon_device *device, const char *options)
{
	const void *prop;
	int node, length;
	(void)options;
	node = fdt_path_offset(initial_boot_params, "/chosen");
	if (node < 0)
		return -ENODEV;
	prop = fdt_getprop(initial_boot_params, node, "kbl,framebuffer", &length);
	if (!prop || length != 36)
		return -ENODEV;
	fb = get_unaligned_be64(prop);
	width = get_unaligned_be32(prop + 8);
	height = get_unaligned_be32(prop + 12);
	stride = get_unaligned_be32(prop + 16);
	stored_height = get_unaligned_be32(prop + 20);
	planes = get_unaligned_be32(prop + 24);
	reserved = get_unaligned_be64(prop + 28);
	if (fb < 0x48000000ULL || fb > 0x140000000ULL - reserved || fb % 4096 ||
	    width != 800 || height != 1340 || stride != 800 || stored_height != 1344 ||
	    planes != 3 || reserved != 0xc50000)
		return -EINVAL;
	fb_console = device->con;
	device->con->write = write_console;
	cursor_x = cursor_y = 0;
	clear_line();
	return 0;
}
EARLYCON_DECLARE(kblfb, setup_console);

static int __init remap_console(void)
{
	if (!fb_console || !console_is_registered(fb_console))
		return 0;
	/* KBL marks this exact region reserved/no-map in the Linux-only DT copy. */
	mapped = ioremap_wc(fb, reserved);
	return mapped ? 0 : -ENOMEM;
}
early_initcall(remap_console);
