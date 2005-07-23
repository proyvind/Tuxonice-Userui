#ifndef _FBANIM_MNG_H_
#define _FBANIM_MNG_H_

struct fbanim_mng_data {
	void *data;
	int len, ptr, open;

	char *canvas;
	int canvas_h, canvas_w;
	int canvas_bytes_pp;

	int wait_msecs;
	struct timeval start_time;
};

extern struct fb_var_screeninfo   fb_var;

/* mng_render.c */
extern int mng_init(char *filename, int bytes_pp);
extern void mng_done();
extern int mng_render_next();
extern int mng_display_next(char* dest, int x, int y, int width, int height);

#endif /* _FBANIM_MNG_H_ */
