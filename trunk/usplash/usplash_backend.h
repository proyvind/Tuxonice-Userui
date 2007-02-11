/* usplash
 *
 * Copyright © 2006 Canonical Ltd.
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

#ifndef USPLASH_BACKEND_H
#define USPLASH_BACKEND_H

int usplash_setfont(void *font);
int usplash_getfontwidth(char c);
int usplash_init();
int usplash_set_resolution(int x, int y);
void usplash_set_palette(int ncols, unsigned char palette[][3]);
void usplash_clear(int x1, int y1, int x2, int y2, int colour);
void usplash_move(int sx, int sy, int dx, int dy, int w, int h);
void usplash_text(int x, int y, const char *s, int len, int fg, int bg);
void usplash_done();
void usplash_getdimensions(int *x, int *y);
void usplash_put(int x, int y, void *pointer);
void usplash_put_part(int x, int y, int w, int h, void *pointer, int x0,
		      int y0);

#endif
