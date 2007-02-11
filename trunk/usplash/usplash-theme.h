/* usplash
 *
 * Copyright © 2006 Canonical Ltd.
 * Copyright © 2006 Dennis Kaarsemaker <dennis@kaarsemaker.net>
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

#ifndef USPLASH_THEME_H
#define USPLASH_THEME_H

#include <sys/types.h>
#include <stdlib.h>

/* Current theme version */
#define THEME_VERSION 2

typedef enum { USPLASH_4_3, USPLASH_16_9 } usplash_ratio;

/* Theme structure definition */
struct usplash_theme {
	int version;		/* Always THEME_VERSION */

	struct usplash_theme *next;

	usplash_ratio ratio;
	struct usplash_pixmap *pixmap;	/* Background image */
	struct usplash_font *font;	/* Font for writing text */

	/* Palette indexes */
	short background;	/* General background colour */
	short progressbar_background;	/* Colour of unreached progress */
	short progressbar_foreground;	/* Colour of current progress */
	short text_background;	/* Colour behind text */
	short text_foreground;	/* Normal colour of text */
	short text_success;	/* Colour of success highlight */
	short text_failure;	/* Colour of failure highlight */

	/* Progress bar position and size in pixels */
	short progressbar_x;
	short progressbar_y;
	short progressbar_width;
	short progressbar_height;

	/* Text box position and size in pixels */
	short text_x;
	short text_y;
	short text_width;
	short text_height;

	/* Text details */
	short line_height;	/* Height of line in pixels */
	short line_length;	/* Length of line in characters */
	short status_width;	/* Number of RHS pixels for status */

	/* Custom draw functions */
	void (*init) (struct usplash_theme * theme);
	void (*clear_screen) (struct usplash_theme * theme);
	void (*clear_progressbar) (struct usplash_theme * theme);
	void (*clear_text) (struct usplash_theme * theme);
	void (*animate_step) (struct usplash_theme * theme, int pulsating);
	void (*draw_progressbar) (struct usplash_theme * theme,
				  int percentage);
	void (*draw_text) (struct usplash_theme * theme, const char *text,
			   size_t len);
	void (*draw_status) (struct usplash_theme * theme,
			     const char *string, size_t len, int mode);
};

/* Copied from bogl.h to avoid including it */
/* Proportional font structure definition. */
struct usplash_font {
	char *name;		/* Font name. */
	int height;		/* Height in pixels. */
	int index_mask;		/* ((1 << N) - 1). */
	int *offset;		/* (1 << N) offsets into index. */
	int *index;
	/* An index entry consists of ((wc & ~index_mask) | width) followed
	   by an offset into content. A list of such entries is terminated
	   by the value 0. */
	u_int32_t *content;
	/* 32-bit right-padded bitmap array. The bitmap for a single glyph
	   consists of (height * ((width + 31) / 32)) values. */
	wchar_t default_char;
};

/* Pixmap structure definition. */
struct usplash_pixmap {
	int width, height;	/* Width, height in pixels. */
	int ncols;		/* Number of colors. */
	int transparent;	/* Transparent color or -1 if none. */
	unsigned char (*palette)[3];	/* Palette. */
	unsigned char *data;	/* Run-length compressed data. */
};

#endif				/* USPLASH_THEME_H */
