#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>

#include "splash.h"
#include "../userui.h"

#define FB_IMAGE_DIR "/etc/suspend_userui/images/"

static struct fb_image cur_fb_pic;
static struct fb_image **fb_pics; /* Array of (struct fb_image *)'s */
static int fb_fd;
static int num_fb_pics;

static void make_pic_cur(struct fb_image *src) {
	int data_len = src->width * src->height * src->depth;

	cur_fb_pic.width = src->width;
	cur_fb_pic.height = src->height;
	cur_fb_pic.depth = src->depth;

	if (cur_fb_pic.data && fb_fd >= 0) {
		memcpy((void*)cur_fb_pic.data, src->data, data_len);

		lseek(fb_fd, 0, SEEK_SET);
		write(fb_fd, cur_fb_pic.data, data_len);
	}
}

static int read_pics(int for_real, int max_to_load) {
	struct dirent **namelist;
	int n, i, usable = 0;

	n = scandir(FB_IMAGE_DIR, &namelist, 0, alphasort);
	if (n < 0)
		return -1;

	for (i = 0; i < n; i++) {
		char *s;
		struct fb_image *fb_pic = NULL;
		s = namelist[i]->d_name;
		if (strcmp(s+strlen(s)-4, ".png") != 0)
			goto next;

		usable++;

		if (!for_real)
			goto next;

		if (num_fb_pics >= max_to_load)
			goto next;

		if (!(fb_pic = (struct fb_image *)malloc(sizeof(struct fb_image))))
			goto next;

		fb_pic->width  = fb_var.xres; /* FIXME */
		fb_pic->height = fb_var.yres; /* FIXME */
		fb_pic->depth  = fb_var.bits_per_pixel; /* FIXME */

		if (load_png(s, fb_pic, 'v')) {
			free(fb_pic);
			goto next;
		}
		fb_pics[num_fb_pics++] = fb_pic;
next:
		free(namelist[i]);
	}
	free(namelist);
	return usable;
}

static void fbsplash_prepare() {
	int n;

	num_fb_pics = 0;

	fb_fd = -1;

	cur_fb_pic.width = fb_var.xres;
	cur_fb_pic.height = fb_var.yres;
	cur_fb_pic.depth = fb_var.bits_per_pixel;
	cur_fb_pic.data = malloc(fb_var.xres * fb_var.yres * fb_var.bits_per_pixel);
	if (!cur_fb_pic.data)
		return;

	n = read_pics(0, 0);
	if (n < 0)
		return;

	if (!(fb_pics = (struct fb_image**)malloc(n*sizeof(struct fb_image*))))
		return;
	read_pics(1, n);

	fb_fd = open("/dev/fb0", O_WRONLY);
	if (fb_fd == -1)
		perror("open(\"/dev/fb0\")");
}

static void fbsplash_cleanup() {
	int i;
	for (i = 0; i < num_fb_pics; i++) {
		if (!fb_pics[i])
			continue;

		if (fb_pics[i]->data)
			free((char*)fb_pics[i]->data);

		free(fb_pics[i]);
	}
	free(fb_pics);

	if (fb_fd >= 0)
		close(fb_fd);
}

static void fbsplash_message(unsigned long type, unsigned long level, int normally_logged, char *fbsplash) {
}

static void fbsplash_update_progress(unsigned long value, unsigned long maximum, char *fbsplash) {
	struct splash_box box;
	int image_num;

	if (!cur_fb_pic.data)
		return;

	if (maximum <= 0)
		maximum = 1;
	if (value < 0)
		value = 0;
	if (value > maximum)
		value = maximum;

	image_num = value * num_fb_pics / maximum;

	if (image_num < 0 || image_num >= num_fb_pics)
		return;

	make_pic_cur(fb_pics[image_num]);

	box.x1 = fb_var.xres/10;
	box.x2 = (fb_var.xres/10)+((fb_var.xres*8/10)*value/maximum);
	box.y1 = fb_var.yres*8/10;
	box.y2 = fb_var.yres*9/10;
	box.c_ul = box.c_ur = box.c_ll = box.c_lr = ({struct color c = { 1, 0, 0, 0 }; c;});

	draw_box((u8*)cur_fb_pic.data, box, fb_fd);
}

static void fbsplash_log_level_change(int loglevel) {
}

static void fbsplash_redraw() {
}

static void fbsplash_keypress(int key) {
}

struct userui_ops userui_fbsplash_ops = {
	.name = "fbsplash",
	.prepare = fbsplash_prepare,
	.cleanup = fbsplash_cleanup,
	.message = fbsplash_message,
	.update_progress = fbsplash_update_progress,
	.log_level_change = fbsplash_log_level_change,
	.redraw = fbsplash_redraw,
	.keypress = fbsplash_keypress,
};
