/*
 * mng_render.c - Functions for rendering MNG animations.
 *
 * Copyright (C) 2005 Bernard Blackham <bernard@blackham.com.au>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libmng.h>
#include <unistd.h>
#include "fbsplash_mng.h"
#include "splash.h"

static int mng_readfile(mng_handle mngh, char *filename)
{
	int fd, len;
	char *file_data;
	struct stat sb;
	mng_anim *mng = mng_get_userdata(mngh);

	if ((fd = open(filename, O_RDONLY)) < 0) {
		perror("mng_readfile: open");
		return 0;
	}
	if (fstat(fd, &sb) == -1) {
		perror("mng_readfile: stat");
		goto close_fail;
	}
	mng->len = sb.st_size;

	if ((mng->data = malloc(mng->len)) == NULL) {
		fprintf(stderr, "mng_readfile: Unable to allocate memory for MNG file\n");
		goto close_fail;
	}

	len = 0;
	file_data = mng->data;
	while (len < mng->len) {
		int ret;
		ret = read(fd, file_data, 0x10000); /* read 64KB at a time */
		switch (ret) {
			case -1:
				perror("mng_readfile: read");
				goto close_fail;
			case 0:
				fprintf(stderr, "mng_readfile: Shorter file than expected!\n");
				goto close_fail;
		}
		file_data += ret;
		len += ret;
	}

	close(fd);

	return 1;

close_fail:
	close(fd);
	return 0;
}

mng_handle mng_load(char* filename)
{
	mng_handle mngh;
	mng_anim *mng;

	mng = (mng_anim*)malloc(sizeof(mng_anim));
	if (!mng) {
		fprintf(stderr, "%s: Unable to allocate memory for MNG data\n",
				__FUNCTION__);
		return MNG_NULL;
	}

	memset(&mng, 0, sizeof(mng_anim));

	mngh = mng_initialize(mng, fbsplash_mng_memalloc, fbsplash_mng_memfree,
			MNG_NULL);
	if (mngh == MNG_NULL) {
		fprintf(stderr, "%s: mng_initialize failed\n", __FUNCTION__);
		goto freemem_fail;
	}

	if (mng_init_callbacks(mngh)) {
		print_mng_error(mngh, "mng_init_callbacks failed");
		goto cleanup_fail;
	}

	/* Load the file into memory */
	if (!mng_readfile(mngh, filename))
		goto cleanup_fail;

	/* Read and parse the file */
	if (mng_read(mngh) != MNG_NOERROR) {
		/* Do something about it. */
		print_mng_error(mngh, "mng_read failed");
		goto cleanup_fail;
	}

	return mngh;

cleanup_fail:
	mng_cleanup(&mngh);
freemem_fail:
	free(mng);
	return MNG_NULL;
}

void mng_done(mng_handle mngh)
{
	mng_cleanup(&mngh);
}

mng_retcode mng_render_next(mng_handle mngh)
{
	mng_anim *mng = mng_get_userdata(mngh);
	mng_retcode ret;

	if (!mng->displayed_first) {
		ret = mng_display(mngh);
		if (ret == MNG_NOERROR)
			mng->displayed_first = 1;
	} else
		ret = mng_display_resume(mngh);

	if (ret == MNG_NEEDTIMERWAIT || ret == MNG_NOERROR)
		return ret;

	print_mng_error(mngh, "mng_display failed");

	return ret;
}

int mng_display_next(mng_handle mngh, char* dest, int x, int y, int width, int height)
{
	truecolor *src;
	int line;
	mng_anim *mng = mng_get_userdata(mngh);
	int bpp = (fb_var.bits_per_pixel + 7) >> 3;
	int dispwidth, dispheight;

	dest += y * width * bpp;
	src = (truecolor*)mng->canvas;

	if (x + mng->canvas_w > width)
		dispwidth = width - x;
	else
		dispwidth = mng->canvas_w;

	if (y + mng->canvas_h > height)
		dispheight = height - y;
	else
		dispheight = mng->canvas_h;

	for (line = 0; line < dispheight; line++) {
		truecolor2fb(src, dest + (x * bpp), dispwidth, y + line, 1);
		dest += width * bpp;
		src  += mng->canvas_w * mng->canvas_bytes_pp;
	}

	return 1;
}

