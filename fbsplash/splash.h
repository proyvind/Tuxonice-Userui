#ifndef __SPLASH_H__
#define __SPLASH_H__

#include "config.h"
#include <stdio.h>
#include <linux/fb.h>
#include <linux/types.h>

#if defined(CONFIG_MNG) && !defined(TARGET_KERNEL)
	#include "mng_splash.h"
#endif

#if !defined(CONFIG_FBSPLASH)
	#define FB_SPLASH_IO_ORIG_USER 	 0
	#define FB_SPLASH_IO_ORIG_KERNEL 1
#endif

/* Adjustable settings */
#define MAX_RECTS 	32
#define MAX_BOXES 	256
#define MAX_ICONS 	512
#define PATH_DEV	"/dev"
#define PATH_PROC	"/proc"
#define PATH_SYS	"/sys"
#define SPLASH_DEV	PATH_DEV "/fbsplash"

#define TTY_SILENT 	8
#define TTY_VERBOSE 	1

#define DEFAULT_MESSAGE "Initializing the kernel..."
#define DEFAULT_FONT 	"luxisri.ttf"
#define DEFAULT_THEME	"tuxonice"
#define TTF_DEFAULT	THEME_DIR "/" DEFAULT_FONT

/* Settings that shouldn't be changed */
#define PROGRESS_MAX 	0xffff

#define u8 __u8
#define u16 __u16
#define u32 __u32

#define printerr(args...)	fprintf(stderr, ## args);
#define printwarn(args...)	fprintf(stderr, ## args);
#define min(a,b)		((a) < (b) ? (a) : (b))
#define max(a,b)		((a) > (b) ? (a) : (b))
#define CLAMP(x) 		((x) > 255 ? 255 : (x))
#define DEBUG(x...)
						    
/* ************************************************************************
 * 				Lists 
 * ************************************************************************ */
typedef struct item {
	void *p;
	struct item *next;
} item;

typedef struct {
	item *head, *tail; 
} list;

#define list_init(list)		{ list.head = list.tail = NULL; }

/* ************************************************************************
 * 				Enums 
 * ************************************************************************ */

enum ENDIANESS { little, big };
enum TASK { setpic, init, on, off, setcfg, getcfg, getstate, none, paint, 
	    setmode, getmode, repaint, start_daemon };
enum ESVC { e_display, e_svc_inact_start, e_svc_inact_stop, e_svc_start, 
	    e_svc_started, e_svc_stop, e_svc_stopped, e_svc_stop_failed, 
	    e_svc_start_failed };

/* ************************************************************************
 * 				Structures 
 * ************************************************************************ */

typedef struct {
	char *filename;
	u32 w, h;
	u8 *picbuf;
} icon_img;

typedef struct {
	int x, y;
	icon_img *img;
	char *svc;
	enum ESVC type;
	u8 status;
} icon;

typedef struct obj {
	enum { o_box, o_icon, o_text, o_anim } type;
	void *p;
} obj;

typedef struct color {
	u8 r, g, b, a;
} __attribute__ ((packed)) color;

struct colorf {
	float r, g, b, a;
};

typedef struct {
	int x1, x2, y1, y2;
} rect;

#if defined(CONFIG_MNG) && !defined(TARGET_KERNEL)
#define F_ANIM_SILENT		1
#define F_ANIM_VERBOSE		2

#define F_ANIM_METHOD_MASK	12
#define F_ANIM_ONCE		0
#define F_ANIM_LOOP		4
#define F_ANIM_PROPORTIONAL	8

#define F_ANIM_STATUS_DONE 1

typedef struct {
	int x, y;
	mng_handle mng;
	char *svc;
	enum ESVC type;
	u8 status;
	u8 flags;
} anim;
#endif	/* CONFIG_MNG */

#define F_TXT_SILENT  	1
#define F_TXT_VERBOSE	2
#define F_TXT_EXEC 	4
#define F_TXT_EVAL	8	

#define F_HS_HORIZ_MASK	7
#define F_HS_VERT_MASK	56

#define F_HS_TOP	8
#define F_HS_VMIDDLE	16
#define F_HS_BOTTOM	32

#define F_HS_LEFT	1
#define F_HS_HMIDDLE	2
#define F_HS_RIGHT	4

#if (defined(CONFIG_TTY_KERNEL) && defined(TARGET_KERNEL)) || defined(CONFIG_TTF)
#include "ttf.h"
typedef struct {
	char *file;
	int size;
	TTF_Font *font;
} font_e;

typedef struct {
	int x, y;
	u8 hotspot;
	color col;
	u8 flags;
	u8 style;
	char *val;
	font_e *font;
} text;
#endif /* TTF */

typedef struct {
	int x1, x2, y1, y2;
	struct color c_ul, c_ur, c_ll, c_lr; 	/* upper left, upper right, 
						   lower left, lower right */
	u8 attr;
} box;

typedef struct truecolor {
	u8 r, g, b, a;
} __attribute__ ((packed)) truecolor;

#define BOX_NOOVER 0x01
#define BOX_INTER 0x02
#define BOX_SILENT 0x04

struct splash_config {
	u8 bg_color;
	u16 tx;
	u16 ty;
	u16 tw;
	u16 th;
	u16 text_x, text_y;
	u16 text_size;
	color text_color;
	char *text_font;
} __attribute__ ((packed));

/* ************************************************************************
 * 				Functions 
 * ************************************************************************ */

/* common.c */
void detect_endianess(void);
int get_fb_settings(int fb_num);
char *get_cfg_file(char *theme);
int do_getpic(unsigned char, unsigned char, char);
int do_config(unsigned char);
char *get_filepath(char *path);
void vt_cursor_enable(int fd);
void vt_cursor_disable(int fd);
int open_fb();
int open_tty(int);
int tty_unset_silent(int fd);
int tty_set_silent(int tty, int fd);

/* parse.c */
int parse_cfg(char *cfgfile);
int parse_svc_state(char *t, enum ESVC *state);

/* dev.c */
int create_dev(char *fn, char *sys, int flag);
int remove_dev(char *fn, int flag);

#define open_cr(fd, dev, sysfs, outlabel, flag)	\
	create_dev(dev, sysfs, flag);		\
	fd = open(dev, O_RDWR);			\
	if (fd == -1) {				\
		remove_dev(dev, flag);		\
		goto outlabel;			\
	}					

#define close_del(fd, dev, flag)		\
	close(fd);				\
	remove_dev(dev, flag);

/* render.c */
void render_objs(u8 *target, u8 *bgnd, char mode, unsigned char origin, int progress_only);
inline void put_pixel (u8 a, u8 r, u8 g, u8 b, u8 *src, u8 *dst, u8 add);

/* image.c */
int load_images(char mode);
void truecolor2fb (truecolor* data, u8* out, int len, int y, u8 alpha);

/* cmd.c */
void cmd_setstate(unsigned int state, unsigned char origin);
void cmd_setpic(struct fb_image *img, unsigned char origin);
void cmd_setcfg(unsigned char origin);
void cmd_getcfg();

/* daemon.c */
void daemon_start();
void do_paint(u8 *dst, u8 *src);
void do_repaint(u8 *dst, u8 *src);
	
/* list.c */
void list_add(list *l, void *obj);

/* effects.c */
void put_img(u8 *dst, u8 *src);
void fade_in(u8 *dst, u8 *image, struct fb_cmap cmap, u8 bgnd, int fd);
void set_directcolor_cmap(int fd);

extern char *cf_pic;
extern char *cf_silentpic;
extern char *cf_pic256;
extern char *cf_silentpic256;

extern struct fb_var_screeninfo   fb_var;
extern struct fb_fix_screeninfo   fb_fix;

extern enum ENDIANESS endianess;
extern enum TASK arg_task;
extern int arg_fb;
extern int arg_vc;
extern char *arg_theme;
extern char arg_mode;
extern u16 arg_progress;
extern u8 arg_kdmode;

#ifndef TARGET_KERNEL
extern char *arg_export;
extern u8 theme_loaded;
#endif 

extern char *config_file;

extern list icons;
extern list objs;
extern list rects;
extern list fonts;

extern u8 *bg_buffer;
extern int bytespp;

extern struct fb_image verbose_img;
extern struct fb_image silent_img;

extern struct splash_config cf;

/* common.c */
extern u8 fb_opt;
extern u8 fb_ro, fb_go, fb_bo;
extern u8 fb_rlen, fb_glen, fb_blen;

extern int fb_fd, fbsplash_fd;
extern char *progress_text;

/* Added for use in dynamically loaded functions */
//void (*png_sig_cmp)(png_bytep sig, png_size_t start, png_size_t num_to_check);
#endif /* __SPLASH_H__ */
