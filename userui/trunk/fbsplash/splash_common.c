/*
 * splash_common.c - Miscellaneous functions used by both the kernel helper and
 *                   user utilities.
 * 
 * Copyright (C) 2004, Michal Januszewski <spock@gentoo.org>
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * $Header: /srv/cvs/splash/utils/splash_common.c,v 1.6 2004/09/04 18:15:03 spock Exp $
 * 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <linux/fb.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "splash.h"

struct fb_var_screeninfo   fb_var;
struct fb_fix_screeninfo   fb_fix;

enum ENDIANESS endianess;
char *config_file = NULL;

enum TASK arg_task = none; 
int arg_fb = 0;
int arg_vc = 0;
char arg_mode = 'v';
char *arg_theme = NULL;
u16 arg_progress = 0;

struct fb_image pic;
char *pic_file = NULL;

void detect_endianess(void)
{
	u16 t = 0x1122;

	if (*(u8*)&t == 0x22) {
		endianess = little;
	} else {
		endianess = big;
	}

	DEBUG("This system is %s-endian.\n", (endianess == little) ? "little" : "big");
}

int get_fb_settings(int fb_num)
{
	char fn[11];
	int fb;
#ifdef TARGET_KERNEL
	char sys[128];
#endif

	sprintf(fn, "/dev/fb%d", fb_num);

	fb = open(fn, O_WRONLY, 0);

	if (fb == -1) {
#ifdef TARGET_KERNEL
		sprintf(sys, "/sys/class/graphics/fb%d/dev", fb_num);
		create_dev(fn, sys, 0x1);
		fb = open(fn, O_WRONLY, 0);
		if (fb == -1)
			remove_dev(fn, 0x1);
		if (fb == -1)
#endif
		{
			printerr("Failed to open /dev/fb%d for reading.\n", fb_num);
			return 1;
		}
	}
		
	if (ioctl(fb,FBIOGET_VSCREENINFO,&fb_var) == -1) {
		printerr("Failed to get fb_var info.\n");
		return 2;
	}

	if (ioctl(fb,FBIOGET_FSCREENINFO,&fb_fix) == -1) {
		printerr("Failed to get fb_fix info.\n");
		return 3;
	}

	close(fb);

#ifdef TARGET_KERNEL
	remove_dev(fn, 0x1);
#endif

	return 0;
}
