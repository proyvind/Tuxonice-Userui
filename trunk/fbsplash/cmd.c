/*
 * splash_cmd.c - Functions for handling communication with the kernel
 *
 * Copyright (C) 2004-2005 Michael Januszewski <spock@gentoo.org>
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/fb.h>
#include <linux/console_splash.h>

#include "splash.h"

void cmd_setstate(unsigned int state, unsigned char origin)
{
	int fd;
	struct fb_splash_iowrapper wrapper = {
		.vc = arg_vc,
		.origin = origin,
		.data = &state,
	};
	
	/* As the kernel fbsplash patch is optional, we won't care about errors here. */

	fd = open(SPLASH_DEV, O_WRONLY);
	if (fd == -1)
		return;

	ioctl(fd, FBIOSPLASH_SETSTATE, &wrapper);

	close(fd);
}

void cmd_setpic(struct fb_image *img, unsigned char origin)
{
}

void cmd_setcfg(unsigned char origin)
{
	int fd;
	struct vc_splash vc_cfg;
	struct fb_splash_iowrapper wrapper = {
		.vc = arg_vc,
		.origin = origin,
		.data = &vc_cfg,
	};
	
	fd = open(SPLASH_DEV, O_WRONLY);
	if (fd == -1)
		return;

	vc_cfg.tx = cf.tx;
	vc_cfg.ty = cf.ty;
	vc_cfg.twidth = cf.tw;
	vc_cfg.theight = cf.th;
	vc_cfg.bg_color = cf.bg_color;
	vc_cfg.theme = arg_theme;
	
	if (ioctl(fd, FBIOSPLASH_SETCFG, &wrapper))
		printerr("FBIOSPLASH_SETCFG failed, error code %d.\n", errno);
	close(fd);
}

void cmd_getcfg()
{
	struct vc_splash vc_cfg;

	vc_cfg.theme = malloc(FB_SPLASH_THEME_LEN);
	if (!vc_cfg.theme)
		return;

	if (vc_cfg.theme[0] == 0) {
		strcpy(vc_cfg.theme, "<none>");
	} 
		
	printf("Splash config on console %d:\n", arg_vc);
	printf("tx:       %d\n", vc_cfg.tx);
	printf("ty:	  %d\n", vc_cfg.ty);
	printf("twidth:	  %d\n", vc_cfg.twidth);
	printf("theight:  %d\n", vc_cfg.theight);
	printf("bg_color: %d\n", vc_cfg.bg_color);
	printf("theme:    %s\n", vc_cfg.theme);

	free(vc_cfg.theme);
	return;		
}

