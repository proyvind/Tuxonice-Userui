#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>
#include <linux/kd.h>

#include "splash.h"
#include "../userui.h"

#define FB_IMAGE_DIR "/etc/suspend_userui/images/"

#define RED ({struct color c = { 255, 0, 0, 255 }; c;})

#define err_abort(code, text) do { \
		fprintf(stderr, "%s at \"%s\":%d: %s\n", \
				text, __FILE__, __LINE__, strerror(code)); \
		abort(); \
	} while (0)

#define errno_abort(text) do { \
		fprintf(stderr, "%s at \"%s\":%d: %s\n", \
				text, __FILE__, __LINE__, strerror(errno)); \
		abort(); \
	} while (0)

/* These are globals that are setup before a second thread in initialised, hence
 * require no locking  */
static struct png_data **pngs; /* Array of (struct png_datas *)'s */
static int fb_fd;
static int num_pngs;
static int rendered_png, displayed_png;
static int can_render;

/* Render thread necessities */
static pthread_t *render_thread = NULL;

/* This signals when a new PNG needs to be rendered. */
static int next_png;
static pthread_mutex_t next_png_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t next_png_cond = PTHREAD_COND_INITIALIZER;

/* cur_fb_pic_cond signals when image has been rendered (cur_fb_pic_ready) */
static struct fb_image cur_fb_pic;
static int cur_fb_pic_ready;
static pthread_mutex_t cur_fb_pic_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cur_fb_pic_cond = PTHREAD_COND_INITIALIZER;

static void make_pic_cur() {
	int status;
	int data_len = fb_var.xres * fb_var.yres * (fb_var.bits_per_pixel >> 3);

	status = pthread_mutex_lock(&cur_fb_pic_mutex);
	if (status != 0)
		err_abort(status, "Lock cur_fb_pic_mutex");

	lseek(fb_fd, 0, SEEK_SET);
	write(fb_fd, cur_fb_pic.data, data_len);

	displayed_png = rendered_png;

	status = pthread_mutex_unlock(&cur_fb_pic_mutex);
	if (status != 0)
		err_abort(status, "Unlock cur_fb_pic_mutex");
}

/* Caller must lock cur_fb_pic_mutex */
static int background_render(int png_num) {
	if (png_num < 0)
		png_num = 0;
	if (png_num >= num_pngs)
		png_num = num_pngs-1;
	return load_png(pngs[png_num], &cur_fb_pic, 's');
}

static void* fbanim_render_thread(void *data) {
	int status, my_next_png;
	while (1) {
		/* Wait for a request for a new PNG to be rendered */
		status = pthread_mutex_lock(&next_png_mutex);
		if (status != 0)
			err_abort(status, "Lock next_png_mutex");

		while (next_png == -1) {
			status = pthread_cond_wait(&next_png_cond, &next_png_mutex);
			if (status != 0)
				err_abort(status, "Wait on next_png_cond");
		}

		my_next_png = next_png;
		next_png = -1;

		status = pthread_mutex_unlock(&next_png_mutex);
		if (status != 0)
			err_abort(status, "Unlock next_png_mutex");

		/* Now render it, and signal when done */
		status = pthread_mutex_lock(&cur_fb_pic_mutex);
		if (status != 0)
			err_abort(status, "Lock cur_fb_pic_mutex");

		if (background_render(my_next_png) != 0) {
			/* do what? */
		}

		cur_fb_pic_ready = 1;
		status = pthread_cond_signal(&cur_fb_pic_cond);
		if (status != 0)
			err_abort(status, "Signal on cur_fb_pic_cond");

		status = pthread_mutex_unlock(&cur_fb_pic_mutex);
		if (status != 0)
			err_abort(status, "Unlock cur_fb_pic_mutex");
	}
	return NULL;
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

static void fbanim_prepare() {
	int n, status;

	hide_cursor();

	num_pngs = 0;
	can_render = 0;
	rendered_png = -1;
	displayed_png = -1;
	next_png = -1;

	fb_fd = -1;

	if (get_fb_settings(0))
		return;

	cur_fb_pic_ready = 0;
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
	if (fb_fd == -1) {
		fprintf(stderr, "open(\"/dev/fb0\"): %s\n", strerror(errno));
		return;
	}

	render_thread = (pthread_t*)malloc(sizeof(pthread_t));
	if (render_thread == NULL)
		return;

	/* Allow for the widest progress bar we might have */
	set_progress_granularity(fb_var.xres);

	can_render = 1;

	status = pthread_create(render_thread, NULL, fbanim_render_thread, NULL);
	if (status != 0)
		err_abort(status, "Creating thread");
}

static void fbanim_cleanup() {
	int i;
	if (pngs) {
		for (i = 0; i < num_pngs; i++) {
			if (!pngs[i])
				continue;

			if (pngs[i]->data)
				free(pngs[i]->data);

			free(pngs[i]);
		}
		free(pngs);
	}

	show_cursor();

	if (fb_fd >= 0)
		close(fb_fd);

	if (render_thread)
		pthread_cancel(*render_thread);
}

static void fbanim_message(unsigned long type, unsigned long level, int normally_logged, char *fbanim) {
}

static void wait_for_render() {
	int status;

	status = pthread_mutex_lock(&cur_fb_pic_mutex);
	if (status != 0)
		err_abort(status, "Lock cur_fb_pic_mutex");

	while (!cur_fb_pic_ready) {
		status = pthread_cond_wait(&cur_fb_pic_cond, &cur_fb_pic_mutex);
		if (status != 0)
			err_abort(status, "Wait on cur_fb_pic_cond");
	}
	
	status = pthread_mutex_unlock(&cur_fb_pic_mutex);
	if (status != 0)
		err_abort(status, "Unlock cur_fb_pic_mutex");

}

static void request_rendering(int png_num) {
	int status;
	
	status = pthread_mutex_lock(&next_png_mutex);
	if (status != 0)
		err_abort(status, "Lock next_png_mutex");

	status = pthread_mutex_lock(&cur_fb_pic_mutex);
	if (status != 0)
		err_abort(status, "Lock cur_fb_pic_mutex");

	cur_fb_pic_ready = 0;
	next_png = png_num;

	status = pthread_cond_signal(&next_png_cond);
	if (status != 0)
		err_abort(status, "Signal on next_png_cond");

	status = pthread_mutex_unlock(&cur_fb_pic_mutex);
	if (status != 0)
		err_abort(status, "Unlock cur_fb_pic_mutex");

	status = pthread_mutex_unlock(&next_png_mutex);
	if (status != 0)
		err_abort(status, "Unlock next_png_mutex");

	rendered_png = png_num;
}

static void fbanim_update_progress(unsigned long value, unsigned long maximum, char *fbanim) {
	struct splash_box box;
	int image_num;
	int status;

	if (!can_render)
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

	/* See if we've already had the image (or a close one) rendered.
	 * Otherwise, render it from scratch
	 */
	if (image_num == displayed_png) {
		/* We're good */
	} else if (rendered_png != -1 && image_num >= rendered_png) {
		/* Just need to display this PNG and get the next one going */
		wait_for_render();
		make_pic_cur();
	} else {
		/* Wrong PNG. Render it right now. */
		request_rendering(image_num);
		wait_for_render();
		make_pic_cur();
	}

	status = pthread_mutex_lock(&cur_fb_pic_mutex);
	if (status != 0)
		err_abort(status, "Lock cur_fb_pic_mutex");

	box.x1 = fb_var.xres/10;
	box.x2 = (fb_var.xres/10)+((fb_var.xres*8/10)*value/maximum);
	box.y1 = fb_var.yres*17/20;
	box.y2 = fb_var.yres*18/20;
	box.c_ul = box.c_ur = box.c_ll = box.c_lr = RED;

	draw_box((u8*)cur_fb_pic.data, box, fb_fd);

	status = pthread_mutex_unlock(&cur_fb_pic_mutex);
	if (status != 0)
		err_abort(status, "Unlock cur_fb_pic_mutex");

	if (image_num+1 < num_pngs && rendered_png != image_num+1)
		request_rendering(image_num+1);
}

static void fbanim_log_level_change(int loglevel) {
}

static void fbanim_redraw() {
}

static void fbanim_keypress(int key) {
	switch (key) {
		case 48:
		case 49:
		case 50:
		case 51:
		case 52:
		case 53:
		case 54:
		case 55:
		case 56:
		case 57:
			console_loglevel = key - 48;
			send_message(USERUI_MSG_SET_LOGLEVEL, &console_loglevel, sizeof(console_loglevel));
			break;
	}
}

static unsigned long fbanim_memory_required() {
	/* Reserve enough for another frame buffer */
	return (fb_var.xres * fb_var.yres * (fb_var.bits_per_pixel >> 3));
}

static struct userui_ops userui_fbanim_ops = {
	.name = "fbanim",
	.prepare = fbanim_prepare,
	.cleanup = fbanim_cleanup,
	.message = fbanim_message,
	.update_progress = fbanim_update_progress,
	.log_level_change = fbanim_log_level_change,
	.redraw = fbanim_redraw,
	.keypress = fbanim_keypress,
	.memory_required = fbanim_memory_required,
};

struct userui_ops *userui_ops = &userui_fbanim_ops;
