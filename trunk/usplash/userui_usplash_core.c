/*
 * userui_usplash_core.c - usplash userspace user interface module.
 *
 * Copyright (C) 2005, Bernard Blackham <bernard@blackham.com.au>
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#include <linux/vt.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>

#include "bogl.h"
#include "../userui.h"

#define BACKGROUND_COLOUR 0
#define PROGRESSBAR_COLOUR 1
#define PROGRESSBAR_BACKGROUND 4
#define TEXT_BACKGROUND 0
#define TEXT_FOREGROUND 2
#define RED 13

#define TEXT_X1 (left_edge + 136)
#define TEXT_X2 (left_edge + 514)
#define TEXT_Y1 (top_edge + 235)
#define TEXT_Y2 (top_edge + 385)
#define LINE_HEIGHT 15

#define PROGRESS_BAR (top_edge + 210)

#define TEXT_WIDTH TEXT_X2-TEXT_X1
#define TEXT_HEIGHT TEXT_Y2-TEXT_Y1

static int left_edge, top_edge;

extern struct bogl_font font_helvB10;
static int usplash_ready = 0;
static int saved_vt=0;
static int new_vt=0;
static struct bogl_pixmap* pixmap_usplash_artwork;

static void switch_console(int screen) {
	char vtname[10];
	int fd;
	struct vt_stat state;

	fd = open("/dev/console", O_RDWR);
	ioctl(fd,VT_GETSTATE,&state);
	saved_vt = state.v_active;
	close(fd);

	sprintf(vtname, "/dev/tty%d",screen);
	fd = open(vtname, O_RDWR);
	ioctl(fd,VT_ACTIVATE,screen);
        new_vt = screen;
	ioctl(fd,VT_WAITACTIVE,screen);
	close(fd);

	return;
}

static void cleanup() {
	if (saved_vt!=0) {
                struct vt_stat state;
                int fd;

                fd = open("/dev/console", O_RDWR);
                ioctl(fd,VT_GETSTATE,&state);
                close(fd);

                if (state.v_active == new_vt) {
                        // We're still on the console to which we switched,
                        // so switch back
                        switch_console(saved_vt);
                }
	}
}

static void draw_progress(int percentage) {
	// Blank out the previous contents
	if (percentage < 0 || percentage > 100) {
		return;
	}
	bogl_clear(left_edge+220,PROGRESS_BAR,left_edge+420,PROGRESS_BAR+10,PROGRESSBAR_BACKGROUND);
	bogl_clear(left_edge+220,PROGRESS_BAR,(left_edge+220+2*percentage),PROGRESS_BAR+10,PROGRESSBAR_COLOUR);
	return;
}	

static void draw_text(char *string, int length) {
	/* Move the existing text up */
	bogl_move(TEXT_X1, TEXT_Y1+LINE_HEIGHT, TEXT_X1, TEXT_Y1, TEXT_X2-TEXT_X1,
		  TEXT_HEIGHT-LINE_HEIGHT);
	/* Blank out the previous bottom contents */
	bogl_clear(TEXT_X1, TEXT_Y2-LINE_HEIGHT, TEXT_X2, TEXT_Y2, 
		   TEXT_BACKGROUND);
	bogl_text (TEXT_X1, TEXT_Y2-LINE_HEIGHT, string, length, 8, 
		   TEXT_BACKGROUND, 0, &font_helvB10);
	return;
}

static void draw_image(struct bogl_pixmap *pixmap) {
	int colour_map[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	bogl_clear(0, 0, bogl_xres, bogl_yres, BACKGROUND_COLOUR);	
	bogl_put (left_edge, top_edge, pixmap, colour_map);
}
	
static void init_progressbar() {
	bogl_clear(left_edge+220,PROGRESS_BAR,left_edge+420,PROGRESS_BAR+10,PROGRESSBAR_BACKGROUND);
}

static void text_clear() {
	bogl_clear(TEXT_X1, TEXT_Y1, TEXT_X2, TEXT_Y2, TEXT_BACKGROUND);
}

static void usplash_prepare() {
    int err;
    void *handle;

    err=bogl_init();

    if (!err) {
	    fprintf(stderr,"%s\n",bogl_error());
	    cleanup();
	    exit (2);
    }

    handle = dlopen("/usr/lib/usplash/usplash-artwork.so", RTLD_NOW);
    if (!handle)
	return;

    pixmap_usplash_artwork = (struct bogl_pixmap *)dlsym(handle, "pixmap_usplash_artwork");
    
    bogl_set_palette (0, 16, pixmap_usplash_artwork->palette);

    left_edge = (bogl_xres - pixmap_usplash_artwork->width) / 2;
    top_edge  = (bogl_yres - pixmap_usplash_artwork->height) / 2;

    usplash_ready = 1;

    draw_image(pixmap_usplash_artwork);

    init_progressbar();

    text_clear();
}

static void usplash_cleanup() {
    if (!usplash_ready)
	return;

    bogl_done();

    cleanup();
}

static void usplash_message(unsigned long section, unsigned long level, int normally_logged, char *msg) {
    if (!usplash_ready)
	return;

    if (section && !((1 << section) & suspend_debug))
	    return;

    if (level > console_loglevel)
	    return;

    draw_text(msg, strlen(msg));
}

static void usplash_update_progress(unsigned long value, unsigned long maximum, char *msg) {
    if (!usplash_ready)
	return;

    if (maximum == (unsigned long)(-1))
	value = maximum = 1;

    if (value > maximum)
	value = maximum;

    if (maximum > 0)
	draw_progress(value * 100 / maximum);
    else
	draw_progress(100);
}

static void usplash_log_level_change(int loglevel) {
    if (!usplash_ready)
	return;
}

static void usplash_redraw() {
    if (!usplash_ready)
	return;
}

static void usplash_keypress(int key) {
    if (common_keypress_handler(key))
	    return;
    switch (key) {
    }
}

static struct userui_ops userui_usplash_ops = {
	.name = "usplash",
	.prepare = usplash_prepare,
	.cleanup = usplash_cleanup,
	.message = usplash_message,
	.update_progress = usplash_update_progress,
	.log_level_change = usplash_log_level_change,
	.redraw = usplash_redraw,
	.keypress = usplash_keypress,
};

struct userui_ops *userui_ops = &userui_usplash_ops;
