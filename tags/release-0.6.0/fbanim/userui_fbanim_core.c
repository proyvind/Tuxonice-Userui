#include <sys/ioctl.h>
#include <sys/mman.h>
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

#include "../userui.h"
#include "fbanim_mng.h"

#define MNG_IMAGE "/home/b/dev/swsusp/svn/suspend2-userui/trunk/fbanim/example.mng"

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
static int fb_fd;
static int num_pngs;
static int can_render;

/* Render thread necessities */
static pthread_t *render_thread = NULL;

/* This signals when a new PNG needs to be rendered. */
static pthread_mutex_t next_png_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t next_png_cond = PTHREAD_COND_INITIALIZER;

/* cur_fb_pic_cond signals when image has been rendered (cur_fb_pic_ready) */
static int cur_fb_pic_ready;
static pthread_mutex_t cur_fb_pic_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cur_fb_pic_cond = PTHREAD_COND_INITIALIZER;

struct fb_var_screeninfo fb_var;
char *frame_buffer;
static int base_image_size;

/*
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
*/

/* Caller must lock cur_fb_pic_mutex */
/*static int background_render(int png_num) {
	if (png_num < 0)
		png_num = 0;
	if (png_num >= num_pngs)
		png_num = num_pngs-1;
	return load_png(pngs[png_num], &cur_fb_pic, 's');
} */

// static void* fbanim_render_thread(void *data) {
// 	int status, my_next_png;
// 	while (1) {
// 		/* Wait for a request for a new PNG to be rendered */
// 		status = pthread_mutex_lock(&next_png_mutex);
// 		if (status != 0)
// 			err_abort(status, "Lock next_png_mutex");
// 
// 		while (next_png == -1) {
// 			status = pthread_cond_wait(&next_png_cond, &next_png_mutex);
// 			if (status != 0)
// 				err_abort(status, "Wait on next_png_cond");
// 		}
// 
// 		my_next_png = next_png;
// 		next_png = -1;
// 
// 		status = pthread_mutex_unlock(&next_png_mutex);
// 		if (status != 0)
// 			err_abort(status, "Unlock next_png_mutex");
// 
// 		/* Now render it, and signal when done */
// 		status = pthread_mutex_lock(&cur_fb_pic_mutex);
// 		if (status != 0)
// 			err_abort(status, "Lock cur_fb_pic_mutex");
// 
// 		if (background_render(my_next_png) != 0) {
// 			/* do what? */
// 		}
// 
// 		cur_fb_pic_ready = 1;
// 		status = pthread_cond_signal(&cur_fb_pic_cond);
// 		if (status != 0)
// 			err_abort(status, "Signal on cur_fb_pic_cond");
// 
// 		status = pthread_mutex_unlock(&cur_fb_pic_mutex);
// 		if (status != 0)
// 			err_abort(status, "Unlock cur_fb_pic_mutex");
// 	}
// 	return NULL;
// }

static void hide_cursor() {
	//ioctl(STDOUT_FILENO, KDSETMODE, KD_GRAPHICS);
	write(1, "\033[?1c", 5);
}

static void show_cursor() {
	//ioctl(STDOUT_FILENO, KDSETMODE, KD_TEXT);
	write(1, "\033[?0c", 5);
}

int get_fb_settings(int fb_num) {
	char fn[20];
	int fb;

	sprintf(fn, "/dev/fb/%d", fb_num);
	fb = open(fn, O_WRONLY, 0);

	if (fb == -1) {
		sprintf(fn, "/dev/fb%d", fb_num);
		fb = open(fn, O_WRONLY, 0);
	}
	
	if (fb == -1) {
		fprintf(stderr, "Failed to open /dev/fb%d or /dev/fb%d for reading.\n", fb_num, fb_num);
		return 1;
	}
		
	if (ioctl(fb,FBIOGET_VSCREENINFO,&fb_var) == -1) {
		fprintf(stderr, "Failed to get fb_var info.\n");
		close(fb);
		return 2;
	}

	// if (ioctl(fb,FBIOGET_FSCREENINFO,&fb_fix) == -1) {
	// 	fprintf(stderr, "Failed to get fb_fix info.\n");
	// 	close(fb);
	// 	return 3;
	// }

	close(fb);
	
	return 0;
}

static void fbanim_prepare() {
	hide_cursor();

	can_render = 0;
	fb_fd = -1;

	if (get_fb_settings(0))
		return;

	if (fb_var.bits_per_pixel != 24 && fb_var.bits_per_pixel != 32) {
		fprintf(stderr, "Only 24bpp and 32bpp framebuffers are currently supported.\n");
		return;
	}

	if (!mng_init(MNG_IMAGE, fb_var.bits_per_pixel >> 3))
		return;
	
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd == -1) {
		fprintf(stderr, "open(\"/dev/fb0\"): %s\n", strerror(errno));
		return;
	}

	base_image_size = fb_var.xres * fb_var.yres * (fb_var.bits_per_pixel >> 3);

	fprintf(stderr, "mmaping %d bytes of framebuffer.\n", base_image_size);
	frame_buffer = mmap(NULL, base_image_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fb_fd, 0);
	if (frame_buffer == MAP_FAILED) {
		perror("Framebuffer mmap() failed");
		frame_buffer = NULL;
		return;
	}
	/* render_thread = (pthread_t*)malloc(sizeof(pthread_t));
	if (render_thread == NULL)
		return; */

	/* Allow for the widest progress bar we might have */
	set_progress_granularity(fb_var.xres);

	can_render = 1;

	/* status = pthread_create(render_thread, NULL, fbanim_render_thread, NULL);
	if (status != 0)
		err_abort(status, "Creating thread"); */
	fprintf(stderr, "Trying next...\n");
	if (!mng_render_next()) {
		return;
	}
}

static void fbanim_cleanup() {
	show_cursor();
	mng_done();

	if (frame_buffer)
		munmap(frame_buffer, base_image_size);

	if (fb_fd >= 0)
		close(fb_fd);

	/* if (render_thread)
		pthread_cancel(*render_thread); */
}

static void fbanim_message(unsigned long type, unsigned long level, int normally_logged, char *fbanim) {
}

/*static void wait_for_render() {
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

} */

/* static void request_rendering(int png_num) {
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
 */

static void fbanim_update_progress(unsigned long value, unsigned long maximum, char *fbanim) {
	int image_num;

	if (!can_render)
		return;

	//fprintf(stderr, "calling next\n");
	//mng_display_next(frame_buffer, 200, 200, 1024, 768);
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

//	/* See if we've already had the image (or a close one) rendered.
//	 * Otherwise, render it from scratch
//	 */
//	if (image_num == displayed_png) {
//		/* We're good */
//	} else if (rendered_png != -1 && image_num >= rendered_png) {
//		/* Just need to display this PNG and get the next one going */
//		wait_for_render();
//		make_pic_cur();
//	} else {
//		/* Wrong PNG. Render it right now. */
//		request_rendering(image_num);
//		wait_for_render();
//		make_pic_cur();
//	}
//
//	status = pthread_mutex_lock(&cur_fb_pic_mutex);
//	if (status != 0)
//		err_abort(status, "Lock cur_fb_pic_mutex");
//
//	box.x1 = fb_var.xres/10;
//	box.x2 = (fb_var.xres/10)+((fb_var.xres*8/10)*value/maximum);
//	box.y1 = fb_var.yres*17/20;
//	box.y2 = fb_var.yres*18/20;
//	box.c_ul = box.c_ur = box.c_ll = box.c_lr = RED;
//
//	draw_box((u8*)cur_fb_pic.data, box, fb_fd);
//
//	status = pthread_mutex_unlock(&cur_fb_pic_mutex);
//	if (status != 0)
//		err_abort(status, "Unlock cur_fb_pic_mutex");
//
//	if (image_num+1 < num_pngs && rendered_png != image_num+1)
//		request_rendering(image_num+1);
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
