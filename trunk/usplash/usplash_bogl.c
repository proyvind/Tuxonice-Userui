/* usplash
 *
 * Copyright © 2006 Canonical Ltd.
 * Copyright © 2006 Dennis Kaarsemaker <dennis@kaarsemaker.net>
 * Copyright © 2005 Matthew Garrett <mjg59@srcf.ucam.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "usplash_backend.h"
#include "usplash_bogl_backend.h"
#include "bogl.h"
#include "usplash-theme.h"
#include <stdio.h>
#include <string.h>

/* Font structure built-in to bogl */
extern struct bogl_font font_helvB10;
struct bogl_font *usplash_bogl_font = &font_helvB10;

static int usplash_bogl_colour_map[256];

int usplash_bogl_setfont(void *font)
{
	usplash_bogl_font = (struct bogl_font *) font;
	return 0;
}

int usplash_bogl_getfontwidth(char c)
{
	return bogl_metrics(&c, 1, usplash_bogl_font);
}

int usplash_bogl_init()
{
	int i;
	if (!bogl_init()) {
		fprintf(stderr, "bogl_init failed: %s\n", bogl_error());
		return 1;
	}
	for (i = 0;
	     i <
	     sizeof(usplash_bogl_colour_map) /
		    sizeof(usplash_bogl_colour_map[0]); ++i)
		usplash_bogl_colour_map[i] = i;
	return 0;
}

void usplash_bogl_done()
{
	bogl_done();
}

int usplash_bogl_set_resolution(int xres, int yres)
{
	if (!bogl_set_resolution(xres, yres))
		return 1;
	return 0;
}

void usplash_bogl_set_palette(int ncols, unsigned char palette[][3])
{
	bogl_set_palette(0, ncols, palette);
}

void usplash_bogl_clear(int x1, int y1, int x2, int y2, int colour)
{
	bogl_clear(x1, y1, x2, y2, colour);
}

void usplash_bogl_move(int sx, int sy, int dx, int dy, int w, int h)
{
	bogl_move(sx, sy, dx, dy, w, h);
}

void
usplash_bogl_text(int x, int y, const char *s, int len, int fg, int bg)
{
	bogl_text(x, y, s, len, fg, bg, 0, usplash_bogl_font);
}

void usplash_bogl_getdimensions(int *x, int *y)
{
	*x = bogl_xres;
	*y = bogl_yres;
}

void usplash_bogl_put(int x, int y, void *pixmap)
{
	bogl_put(x, y, pixmap, usplash_bogl_colour_map);
}

void
usplash_bogl_put_part(int x, int y, int width, int height,
		      void *pointer, int x0, int y0)
{
	struct usplash_pixmap *pixmap = pointer;
	struct bogl_pixmap bp = {
		.width = width,
		.height = height,
		.ncols = pixmap->ncols,
		.transparent = pixmap->transparent,
		.palette = pixmap->palette,
	};
	unsigned char *part =
	    (unsigned char *) malloc(width * height *
				     sizeof(unsigned char));
	int i;
	unsigned char *start = pixmap->data + y0 * pixmap->width + x0;
	for (i = 0; i < height; i++) {
		memcpy(part + i * width, start + i * pixmap->width, width);
	}
	/* Fill other members of struct */
	bp.data = part;
	bogl_put(x, y, &bp, usplash_bogl_colour_map);
	free(part);
}
