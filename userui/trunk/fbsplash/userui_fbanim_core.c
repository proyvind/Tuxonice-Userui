#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>
#include <linux/kd.h>

#include "splash.h"
#include "../userui.h"

#define FB_IMAGE_DIR "/etc/suspend_userui/images/"

static struct fb_image cur_fb_pic;
static struct png_data **pngs; /* Array of (struct png_datas *)'s */
static int fb_fd;
static int num_pngs, cur_png;

static void make_pic_cur(int png_num) {
	int data_len = fb_var.xres * fb_var.yres * (fb_var.bits_per_pixel >> 3);

	if (load_png(pngs[png_num], &cur_fb_pic, 's')) {
	}

	if (cur_fb_pic.data && fb_fd >= 0) {
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
		char *s = NULL;
		struct png_data *png_data = NULL;
		struct stat sb;
		int fd = -1, bytes_to_go;
		char* p;

		s = (char*) malloc(strlen(namelist[i]->d_name)+strlen(FB_IMAGE_DIR)+2);
		if (!s)
			goto next;
		sprintf(s, "%s/%s", FB_IMAGE_DIR, namelist[i]->d_name);
		if (strcmp(s+strlen(s)-4, ".png") != 0)
			goto next;

		if (stat(s, &sb) == -1)
			goto next;

		if (!S_ISREG(sb.st_mode))
			goto next;

		usable++;

		if (!for_real)
			goto next;

		if (num_pngs >= max_to_load)
			goto next;

		if (!(png_data = (struct png_data *)malloc(sizeof(struct png_data))))
			goto next;

		png_data->data = NULL;

		if ((fd = open(s, O_RDONLY)) == -1)
			goto next_bail;

		if (!(png_data->data = (u8*)malloc(sb.st_size)))
			goto next_bail;

		bytes_to_go = sb.st_size;
		p = png_data->data;
		while (bytes_to_go > 0) {
			int res;
			res = read(fd, p, bytes_to_go);
			if (res == -1)
				goto next_bail;
			if (res == 0) /* Unexpected EOF */
				goto next_bail;
			p += res;
			bytes_to_go -= res;
		}

		png_data->len = sb.st_size;

		pngs[num_pngs++] = png_data;

		goto next;

next_bail:
		if (png_data && png_data->data)
			free(png_data->data);
		if (png_data)
			free(png_data);

next:
		if (fd >= 0)
			close(fd);
		if (s)
			free(s);
		free(namelist[i]);
	}
	free(namelist);
	return usable;
}

static void hide_cursor() {
	//ioctl(STDOUT_FILENO, KDSETMODE, KD_GRAPHICS);
	write(1, "\033[?1c", 5);
}

static void show_cursor() {
	//ioctl(STDOUT_FILENO, KDSETMODE, KD_TEXT);
	write(1, "\033[?0c", 5);
}

static void fbsplash_prepare() {
	int n;

	hide_cursor();

	num_pngs = 0;
	cur_png = -1;

	fb_fd = -1;

	if (get_fb_settings(0))
		return;

	cur_fb_pic.width = fb_var.xres;
	cur_fb_pic.height = fb_var.yres;
	cur_fb_pic.depth = fb_var.bits_per_pixel;
	cur_fb_pic.data = malloc(fb_var.xres * fb_var.yres * (fb_var.bits_per_pixel >> 3));
	if (!cur_fb_pic.data)
		return;

	n = read_pics(0, 0);
	if (n < 0)
		return;

	if (!(pngs = (struct png_data**)malloc(n*sizeof(struct png_data*))))
		return;
	read_pics(1, n);

	fb_fd = open("/dev/fb0", O_WRONLY);
	if (fb_fd == -1)
		perror("open(\"/dev/fb0\")");
}

static void fbsplash_cleanup() {
	int i;
	for (i = 0; i < num_pngs; i++) {
		if (!pngs[i])
			continue;

		if (pngs[i]->data)
			free(pngs[i]->data);

		free(pngs[i]);
	}
	free(pngs);

	show_cursor();

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

	image_num = value * num_pngs / maximum;

	if (image_num < 0)
		image_num = 0;
	if (image_num >= num_pngs)
		image_num = num_pngs-1;

	if (cur_png != image_num) {
		make_pic_cur(image_num);
		cur_png = image_num;
	}

	box.x1 = fb_var.xres/10;
	box.x2 = (fb_var.xres/10)+((fb_var.xres*8/10)*value/maximum);
	box.y1 = fb_var.yres*17/20;
	box.y2 = fb_var.yres*18/20;
	box.c_ul = box.c_ur = box.c_ll = box.c_lr = ({struct color c = { 255, 0, 0, 255 }; c;});

	draw_box((u8*)cur_fb_pic.data, box, fb_fd);
}

static void fbsplash_log_level_change(int loglevel) {
}

static void fbsplash_redraw() {
}

static void fbsplash_keypress(int key) {
}

static unsigned long fbsplash_memory_required() {
	/* Reserve enough for another frame buffer */
	return (fb_var.xres * fb_var.yres * (fb_var.bits_per_pixel >> 3));
}

static struct userui_ops userui_fbsplash_ops = {
	.name = "fbsplash",
	.prepare = fbsplash_prepare,
	.cleanup = fbsplash_cleanup,
	.message = fbsplash_message,
	.update_progress = fbsplash_update_progress,
	.log_level_change = fbsplash_log_level_change,
	.redraw = fbsplash_redraw,
	.keypress = fbsplash_keypress,
	.memory_required = fbsplash_memory_required,
};

struct userui_ops *userui_ops = &userui_fbsplash_ops;
