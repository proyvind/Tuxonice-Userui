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

#include <linux/vt.h>
#include <linux/limits.h>

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

#include "usplash_backend.h"
#include "usplash_bogl_backend.h"
#ifdef SVGA
#include "usplash_svga_backend.h"
#endif
#include "usplash.h"
#include "usplash-theme.h"

sigset_t sigs;
#define blocksig() do{ sigprocmask(SIG_BLOCK, &sigs, NULL); } while(0)
#define unblocksig() do{ sigprocmask(SIG_UNBLOCK, &sigs, NULL); } while(0)

/* Prototypes of non-static functions */
void ensure_console(void);

void switch_console(int vt, int vt_fd);

void clear_screen(void);

void clear_progressbar(void);
void draw_progressbar(int percentage);

void clear_text(void);
void draw_text(const char *string, size_t len);
void draw_status(const char *string, size_t len, int mode);
int handle_input(const char *string, size_t len, int quiet);

/* Prototypes of static functions */
static void draw_newline(void);
static void draw_chars(const char *string, size_t len);

/* Non-static so that svgalib can call it. Damned svgalib. */
void usplash_restore_console(void);

/* Default theme, used when no suitable alternative can be found */
extern struct usplash_theme testcard_theme;

/* Theme being used */
struct usplash_theme *theme;

/* Distance to themed area from the top-left corner of the screen */
int left_edge, top_edge;

/* Current text output position (i.e. where the cursor would be) */
static int text_position;

/* Coordinates of the text box */
static int text_x1, text_x2, text_y1, text_y2;

/* Size of the screen */
int usplash_xres, usplash_yres;

/* Virtual terminal we switched away from */
static int saved_vt = 0;
static int saved_vt_fd = -1;

/* Virtual terminal we switched to */
static int new_vt = 0;

/* Number of seconds to wait for a command before exiting */
static int timeout = 15;

/* Are we verbose or not? */
static int verbose = 0;

/* /dev/console */
static int console_fd = -1;

struct usplash_funcs {
	int (*usplash_setfont) (void *font);
	int (*usplash_getfontwidth) (char c);
	int (*usplash_init) (void);
	int (*usplash_set_resolution) (int x, int y);
	void (*usplash_set_palette) (int ncols,
				     unsigned char palette[][3]);
	void (*usplash_clear) (int x1, int y1, int x2, int y2, int colour);
	void (*usplash_move) (int sx, int sy, int dx, int dy, int w,
			      int h);
	void (*usplash_text) (int x, int y, const char *s, int len, int fg,
			      int bg);
	void (*usplash_done) ();
	void (*usplash_getdimensions) (int *x, int *y);
	void (*usplash_put) (int x, int y, void *pointer);
	void (*usplash_put_part) (int x, int y, int w, int h,
				  void *pointer, int x0, int y0);
};

#ifdef SVGA
struct usplash_funcs usplash_svga_funcs = {
	.usplash_setfont = usplash_svga_setfont,
	.usplash_getfontwidth = usplash_svga_getfontwidth,
	.usplash_init = usplash_svga_init,
	.usplash_set_resolution = usplash_svga_set_resolution,
	.usplash_set_palette = usplash_svga_set_palette,
	.usplash_clear = usplash_svga_clear,
	.usplash_move = usplash_svga_move,
	.usplash_text = usplash_svga_text,
	.usplash_done = usplash_svga_done,
	.usplash_getdimensions = usplash_svga_getdimensions,
	.usplash_put = usplash_svga_put,
	.usplash_put_part = usplash_svga_put_part,
};
#endif

struct usplash_funcs usplash_bogl_funcs = {
	.usplash_setfont = usplash_bogl_setfont,
	.usplash_getfontwidth = usplash_bogl_getfontwidth,
	.usplash_init = usplash_bogl_init,
	.usplash_set_resolution = usplash_bogl_set_resolution,
	.usplash_set_palette = usplash_bogl_set_palette,
	.usplash_clear = usplash_bogl_clear,
	.usplash_move = usplash_bogl_move,
	.usplash_text = usplash_bogl_text,
	.usplash_done = usplash_bogl_done,
	.usplash_getdimensions = usplash_bogl_getdimensions,
	.usplash_put = usplash_bogl_put,
	.usplash_put_part = usplash_bogl_put_part,
};

static struct usplash_funcs *usplash_operations;

int usplash_setfont(void *font)
{
	return usplash_operations->usplash_setfont(font);
}

int usplash_getfontwidth(char c)
{
	return usplash_operations->usplash_getfontwidth(c);
}

int usplash_init()
{
	return usplash_operations->usplash_init();
}

int usplash_set_resolution(int x, int y)
{
	return usplash_operations->usplash_set_resolution(x, y);
}

void usplash_set_palette(int ncols, unsigned char palette[][3])
{
	usplash_operations->usplash_set_palette(ncols, palette);
}

void usplash_clear(int x1, int y1, int x2, int y2, int colour)
{
	usplash_operations->usplash_clear(x1, y1, x2, y2, colour);
}

void usplash_move(int sx, int sy, int dx, int dy, int w, int h)
{
	usplash_operations->usplash_move(sx, sy, dx, dy, w, h);
}

void usplash_text(int x, int y, const char *s, int len, int fg, int bg)
{
	usplash_operations->usplash_text(x, y, s, len, fg, bg);
}

void usplash_done()
{
	usplash_operations->usplash_done();
}

void usplash_getdimensions(int *x, int *y)
{
	usplash_operations->usplash_getdimensions(x, y);
}

void usplash_put(int x, int y, void *pointer)
{
	usplash_operations->usplash_put(x, y, pointer);
}

void usplash_put_part(int x, int y, int w, int h, void *pointer, int x0,
		      int y0)
{
	usplash_operations->usplash_put_part(x, y, w, h, pointer, x0, y0);
}

void usplash_setup_funcs()
{
#ifdef SVGA
	/* Check which set of functions we should be using */
	int fd;
	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) {
		usplash_operations = &usplash_svga_funcs;
		return;
	}
	close(fd);
#endif
	usplash_operations = &usplash_bogl_funcs;
}

void ensure_console(void)
{
	if (console_fd == -1) {
		console_fd = open("/dev/console", O_RDWR);
		if (console_fd == -1) {
			fprintf(stderr,
				"usplash: cannot open /dev/console: %s\n",
				strerror(errno));
			exit(1);
		}
	}
}

void switch_console(int vt, int vt_fd)
{
	char saved_vtname[10];
	struct vt_stat state;

	ensure_console();
	ioctl(console_fd, VT_GETSTATE, &state);

	saved_vt = state.v_active;
	assert((saved_vt >= 0) && (saved_vt < 10));
	sprintf(saved_vtname, "/dev/tty%d", saved_vt);
	/* This may fail when restoring the console before exit, since the
	 * initramfs has gone away; but that's OK.
	 */
	saved_vt_fd = open(saved_vtname, O_RDWR);
	new_vt = vt;

	ioctl(vt_fd, VT_ACTIVATE, vt);
	/* Note that, when using SVGA, we may be interrupted around here by
	 * a signal and never come back. See __svgalib_releasevt_signal.
	 */
	ioctl(vt_fd, VT_WAITACTIVE, vt);

	close(STDIN_FILENO);
	dup2(vt_fd, 0);
}

void usplash_restore_console(void)
{
	struct vt_stat state;

	if (saved_vt != 0 && saved_vt_fd != -1) {
		ensure_console();
		ioctl(console_fd, VT_GETSTATE, &state);

		/* Switch back if we're still on the console we switched to */
		if (state.v_active == new_vt)
			switch_console(saved_vt, saved_vt_fd);
	}
}

int usplash_setup(int xres, int yres, int v)
{
	int ret;
	short ncolors;
	void *theme_handle;
	struct usplash_theme *htheme;
	int maxarea;
	usplash_ratio ratio;

	usplash_setup_funcs();
	ensure_console();

	verbose = v;
	theme_handle = dlopen(USPLASH_THEME, RTLD_LAZY);
	if (theme_handle) {
		theme = dlsym(theme_handle, "usplash_theme");
		if ((theme == NULL) || (theme->version != THEME_VERSION)) {
			dlclose(theme_handle);
			theme = &testcard_theme;
		}
	} else {
		theme = &testcard_theme;
	}

	/* If xres or yres is 0, pick the lowest resolution theme. */
	if (xres == 0 || yres == 0) {
		xres = theme->pixmap->width;
		yres = theme->pixmap->height;
	}

	ret = usplash_set_resolution(xres, yres);
	if (ret)
		return ret;
	ret = usplash_init();
	if (ret)
		return ret;
	/* usplash_init might have changed the resolution */
	usplash_getdimensions(&xres, &yres);
	usplash_xres = xres;
	usplash_yres = yres;

	/* Select theme from linked list */
	htheme = NULL;
	maxarea = 0;
	ratio =
	    (float) xres / (float) yres >
	    1.55 ? USPLASH_16_9 : USPLASH_4_3;
	while (theme) {
		if (theme->pixmap->height <= yres
		    && theme->pixmap->width <= xres
		    && theme->pixmap->height *
		    theme->pixmap->width > maxarea
		    && theme->ratio == ratio) {
			maxarea =
			    theme->pixmap->height *
			    theme->pixmap->width;
			htheme = theme;
		}
		theme = theme->next;
	}
	theme = htheme;
	if (!theme) {
		fprintf(stderr,
			"usplash: No usable theme found for %dx%d\n",
			xres, yres);
		return 1;
	}

	ncolors = theme->pixmap->ncols;
	if (theme->init)
		theme->init(theme);

	left_edge = (usplash_xres - theme->pixmap->width) / 2;
	top_edge = (usplash_yres - theme->pixmap->height) / 2;
	text_x1 = left_edge + theme->text_x;
	text_y1 = top_edge + theme->text_y;
	text_x2 = text_x1 + theme->text_width;
	text_y2 = text_y1 + theme->text_height;
	text_position = text_x1;

	if (theme->font)
		usplash_setfont(theme->font);

	usplash_set_palette(theme->pixmap->ncols, theme->pixmap->palette);

	sigemptyset(&sigs);
	sigaddset(&sigs, SIGALRM);
	return 0;
}

size_t strncspn(const char *s, size_t n, const char *reject)
{
	register size_t l;

	for (l = 0; l < n; l++)
		if (strchr(reject, s[l]))
			break;

	return l;
}

void clear_screen(void)
{
	blocksig();
	if (theme->clear_screen)
		theme->clear_screen(theme);
	else {
		usplash_clear(0, 0, usplash_xres, usplash_yres,
			      theme->background);
		usplash_put(left_edge, top_edge, theme->pixmap);
	}
	unblocksig();
}


void clear_progressbar(void)
{
	if (theme->clear_progressbar)
		theme->clear_progressbar(theme);
	else {
		int x1, y1, x2, y2;

		x1 = left_edge + theme->progressbar_x;
		y1 = top_edge + theme->progressbar_y;

		x2 = x1 + theme->progressbar_width;
		y2 = y1 + theme->progressbar_height;

		usplash_clear(x1, y1, x2, y2,
			      theme->progressbar_background);
	}
}

void draw_progressbar(int percentage)
{
	if (percentage > 100 || percentage < -100)
		return;

	blocksig();
	if (theme->draw_progressbar)
		theme->draw_progressbar(theme, percentage);
	else {
		int x1, y1, x2, y2, xx, bg, fg;

		if (percentage < 0) {
			bg = theme->progressbar_foreground;
			fg = theme->progressbar_background;
			percentage = -percentage;
		} else {
			bg = theme->progressbar_background;
			fg = theme->progressbar_foreground;
		}

		x1 = left_edge + theme->progressbar_x;
		y1 = top_edge + theme->progressbar_y;

		x2 = x1 + theme->progressbar_width;
		y2 = y1 + theme->progressbar_height;

		xx = x1 + ((theme->progressbar_width * percentage) / 100);

		usplash_clear(x1, y1, xx, y2, fg);
		usplash_clear(xx, y1, x2, y2, bg);
	}
	unblocksig();
}


void clear_text(void)
{
	if (!verbose)
		return;
	blocksig();
	if (theme->clear_text)
		theme->clear_text(theme);
	else {
		int x1, y1, x2, y2;

		x1 = left_edge + theme->text_x;
		y1 = top_edge + theme->text_y;

		x2 = x1 + theme->text_width;
		y2 = y1 + theme->text_height;

		usplash_clear(x1, y1, x2, y2, theme->text_background);
	}
	unblocksig();
}

void draw_text_urgent(const char *string, size_t len)
{
	static int inited = 0;
	int ov = verbose;

	verbose = 1;

	if (inited == 0 && ov == 0)
		clear_text();
	inited = 1;

	draw_text(string, len);
	verbose = ov;
}

/* Moves all text one line up and positions cursor at beginning of line */
static void draw_newline(void)
{
	/* Move existing text up */
	usplash_move(text_x1,
		     top_edge + theme->text_y + theme->line_height,
		     text_x1, top_edge + theme->text_y, theme->text_width,
		     theme->text_height - theme->line_height);

	usplash_clear(text_x1, text_y2 - theme->line_height,
		      text_x2, text_y2, theme->text_background);

	/* Reset "cursor" position */
	text_position = text_x1;
}

/* Continues to draw text at current position */
static void draw_chars(const char *string, size_t len)
{
	int i, slen;
	size_t drawn;

	drawn = 0;
	while (drawn < len) {
		/* See how many characters we can draw on this line */
		slen = 0;
		for (i = 0; i + drawn < len; i++) {
			slen +=
			    usplash_getfontwidth(*(string + i + drawn));
			if (text_position + slen > text_x2)
				break;
		}

		/* Ok, draw them and move on to the next line */
		usplash_text(text_position, text_y2 - theme->line_height,
			     string + drawn, i,
			     theme->text_foreground,
			     theme->text_background);

		text_position += slen;
		drawn += i;
		if (drawn == len)
			break;
		else
			draw_newline();
	}
}

/* Adds a newline and draws one line of text */
void draw_text(const char *string, size_t len)
{
	if (!verbose)
		return;

	blocksig();
	if (theme->draw_text) {
		theme->draw_text(theme, string, len);
	} else {
		draw_newline();
		draw_chars(string, len);
	}
	unblocksig();
}

int handle_input(const char *string, const size_t len, const int quiet)
{
	int i;
	char input;
	ssize_t wlen;
	int fifo_outfd;
	char inputbuf[PIPE_BUF] = "";
	int reset_verbose = 0;

	/* Initialize text area if not running verbose yet */
	if (!verbose) {
		reset_verbose = 1;
		verbose = 1;
		clear_text();
	}

	/* draw the prompt */
	draw_text(string, len);

	/* Get user input */
	for (i = 0; i < PIPE_BUF - 1; i++) {
		input = getchar();
		if (input == '\n' || input == '\r' || input == '\0')
			break;

		if (quiet == 2) {
			i--;
			continue;
		}

		inputbuf[i] = input;

		if (quiet)
			input = '*';

		draw_chars(&input, 1);
	}
	inputbuf[i] = '\0';

	/* Reset the verbose flag */
	if (reset_verbose)
		verbose = 0;

	/* With INPUTENTER we're not really interested in the user input */
	if (quiet == 2)
		return 0;

	/* We wait for timeout seconds for someone to read the user input */
	for (i = 1; i != timeout + 1; i++) {
		fifo_outfd = open(USPLASH_OUTFIFO, O_WRONLY | O_NONBLOCK);
		if (fifo_outfd < 0)
			sleep(1);
		else
			break;
	}

	if (fifo_outfd < 0)
		return 1;

	wlen = write(fifo_outfd, inputbuf, strlen(inputbuf) + 1);
	if (wlen < 0)
		return 1;

	close(fifo_outfd);
	memset(inputbuf, 0, PIPE_BUF);
	return 0;
}

void draw_status(const char *string, size_t len, int mode)
{
	if (!verbose)
		return;

	blocksig();
	if (theme->draw_status) {
		theme->draw_status(theme, string, len, mode);
	} else {
		int x1, y1, fg;

		if (mode < 0)
			fg = theme->text_failure;
		else if (mode > 0)
			fg = theme->text_success;
		else
			fg = theme->text_foreground;

		x1 = text_x2 - theme->status_width;
		y1 = text_y2 - theme->line_height;

		usplash_clear(x1, y1, text_x2, text_y2,
			      theme->text_background);
		usplash_text(x1, y1, string, len, fg,
			     theme->text_background);
	}
	unblocksig();
}

void animate_step(pulsating)
{
	static int pulsate_step = 0;
	static int num_steps = 37;
	int x1, y1, x2, y2;
	if (theme->animate_step)
		theme->animate_step(theme, pulsating);
	else {
		if (pulsating) {
			clear_progressbar();

			if (pulsate_step < 19)
				x1 = left_edge + theme->progressbar_x +
				    (theme->progressbar_width / 20) *
				    pulsate_step;
			else
				x1 = left_edge + theme->progressbar_x +
				    (theme->progressbar_width / 20) * (36 -
								       pulsate_step);

			y1 = top_edge + theme->progressbar_y;

			x2 = x1 + (theme->progressbar_width / 10);
			y2 = y1 + theme->progressbar_height;
			usplash_clear(x1, y1, x2, y2,
				      theme->progressbar_foreground);

			pulsate_step = (pulsate_step + 1) % num_steps;
		}
	}
}
