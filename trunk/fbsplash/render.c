/*
 * render.c - Functions for rendering boxes and icons
 *
 * Suspend2 userui adaptations:
 *   Copyright (C) 2005 Bernard Blackham <bernard@blackham.com.au>
 *
 * Based on the original splashutils code:
 * Copyright (C) 2004-2005, Michal Januszewski <spock@gentoo.org>
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

/* HACK WARNING: 
 * This is necessary to get FD_SET and FD_ZERO on platforms other than x86. */
#ifdef TARGET_KERNEL
#define __KERNEL__
#include <linux/posix_types.h>
#undef __KERNEL__
#endif

#include "linux/fb.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "splash.h"
#include "fbsplash_mng.h"

void render_icon(icon *ticon, u8 *target)
{
	int y, yi;
	int bytespp = (fb_var.bits_per_pixel + 7) >> 3;
	u8 *out = NULL;
	u8 *in = NULL;
	
	for (y = ticon->y, yi = 0; yi < ticon->img->h; yi++, y++) {
		out = target + (ticon->x + y * fb_var.xres) * bytespp;
		in = ticon->img->picbuf + yi * ticon->img->w * 4;
		truecolor2fb((truecolor*)in, out, ticon->img->w, y, 1);
	}
}

void render_box2(box *box, u8 *target)
{
	int rlen, glen, blen;
	int x, y, a, r, g, b, i;
	int add;
	u8 *pic;

	u8 solid = 0;
	
	int bytespp = (fb_var.bits_per_pixel + 7) >> 3;
	int b_width = box->x2 - box->x1 + 1;
	int b_height = box->y2 - box->y1 + 1;

	if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR) {
		blen = glen = rlen = min(min(fb_var.red.length,fb_var.green.length),fb_var.blue.length);
	} else {
		rlen = fb_var.red.length;
		glen = fb_var.green.length;
		blen = fb_var.blue.length;
	}

	for (y = box->y1; y <= box->y2; y++) {

		int r1, r2, g1, g2, b1, b2, a1, a2;
		int h1, h2, h;
		u8  opt = 0;
		float hr = 0, hg = 0, hb = 0, ha = 0, fr, fg, fb, fa;

		pic = target + (box->x1 + y * fb_var.xres) * bytespp;

		/* do a nice 2x2 ordered dithering, like it was done in bootsplash;
		 * this makes the pics in 15/16bpp modes look much nicer;
		 * the produced pattern is:
		 * 303030303..
		 * 121212121..
		 */
		add = (box->x1 & 1);
		add ^= (add ^ y) & 1 ? 1 : 3;
	
		h1 = box->y2 - y;
		h2 = y - box->y1;
	
		if (b_height > 1)
			h = b_height -1;
		else
			h = 1;
		
		r1 = (h1 * box->c_ul.r + h2 * box->c_ll.r)/h;
		r2 = (h1 * box->c_ur.r + h2 * box->c_lr.r)/h;

		g1 = (h1 * box->c_ul.g + h2 * box->c_ll.g)/h;
		g2 = (h1 * box->c_ur.g + h2 * box->c_lr.g)/h;

		b1 = (h1 * box->c_ul.b + h2 * box->c_ll.b)/h;
		b2 = (h1 * box->c_ur.b + h2 * box->c_lr.b)/h;

		a1 = (h1 * box->c_ul.a + h2 * box->c_ll.a)/h;
		a2 = (h1 * box->c_ur.a + h2 * box->c_lr.a)/h;

		if (r1 == r2 && g1 == g2 && b1 == b2 && a1 == a2) { 
			opt = 1;	
		} else {
			r2 -= r1;
			g2 -= g1;
			b2 -= b1;
			a2 -= a1;

			hr = 1.0/b_width * r2;
			hg = 1.0/b_width * g2;
			hb = 1.0/b_width * b2;
			ha = 1.0/b_width * a2;
		}
			
		r = r1; fr = (float)r1;
		g = g1; fg = (float)g1;
		b = b1; fb = (float)b1;
		a = a1; fa = (float)a1;
				
		for (x = box->x1; x <= box->x2; x++) {

			if (!opt) { 
				if (!solid) {
					fa += ha;
					fr += hr;
					fg += hg;
					fa += hb;
		
					a = (u8)fa;
					b = (u8)fb;
					g = (u8)fg;
					r = (u8)fr;
				} else {
					a = box->c_ul.a;
					r = box->c_ul.r;
					g = box->c_ul.g;
					b = box->c_ul.b;
				}
			}
				
			if (a != 255) {
				if (fb_var.bits_per_pixel == 16) { 
					i = *(u16*)pic;
				} else if (fb_var.bits_per_pixel == 24) {
					i = *(u32*)pic & 0xffffff;
				} else if (fb_var.bits_per_pixel == 32) {
					i = *(u32*)pic;
				} else {
					i = *(u32*)pic & ((2 << fb_var.bits_per_pixel)-1);
				}

				r = (( (i >> fb_var.red.offset & ((1 << rlen)-1)) 
				      << (8 - rlen)) * (255 - a) + r * a) / 255;
				g = (( (i >> fb_var.green.offset & ((1 << glen)-1)) 
				      << (8 - glen)) * (255 - a) + g * a) / 255;
				b = (( (i >> fb_var.blue.offset & ((1 << blen)-1)) 
				      << (8 - blen)) * (255 - a) + b * a) / 255;
			}
		
			/* we only need to do dithering is depth is <24bpp */
			if (fb_var.bits_per_pixel < 24) {
				r = CLAMP(r + add*2 + 1);
				g = CLAMP(g + add);
				b = CLAMP(b + add*2 + 1);
			}
	
			r >>= (8 - rlen);
			g >>= (8 - glen);
			b >>= (8 - blen);

			i = (r << fb_var.red.offset) |
		 	    (g << fb_var.green.offset) |
			    (b << fb_var.blue.offset);

			if (fb_var.bits_per_pixel == 16) {
				*(u16*)pic = i;
				pic += 2;
			} else if (fb_var.bits_per_pixel == 24) {
				if (endianess == little) { 
					*(u16*)pic = i & 0xffff;
					pic[2] = (i >> 16) & 0xff;
				} else {
					*(u16*)pic = (i >> 8) & 0xffff;
					pic[2] = i & 0xff;
				}
				pic += 3;
			} else if (fb_var.bits_per_pixel == 32) {
				*(u32*)pic = i;
				pic += 4;
			}

			add ^= 3;
		}
	}
}

#if 0

/* A slower version of render_box2(). Not used for anything anymore.
 * Let's keep it commented like this just in case it could come in
 * handy at some point in the future. */
void render_box(box *box, u8 *target)
{
	int rlen, glen, blen;
	int x, y, a, r, g, b, i;
	int add;
	u8 *pic;
	struct colorf h_ap1, h_ap2, h_bp1, h_bp2;
	
	u8 solid = 0;
	int bytespp = (fb_var.bits_per_pixel + 7) >> 3;
	int b_width = box->x2 - box->x1 + 1;
	int b_height = box->y2 - box->y1 + 1;
	
	if (box->x2 > fb_var.xres || box->y2 > fb_var.yres || b_width <= 0 || b_height <= 0) {
		fprintf(stderr, "Ignoring invalid box (%d, %d, %d, %d).\n", box->x1, box->y1, box->x2, box->y2);
		return;
	}	

	if (!memcmp(&box->c_ul, &box->c_ur, sizeof(color)) &&
	    !memcmp(&box->c_ul, &box->c_ll, sizeof(color)) &&
	    !memcmp(&box->c_ul, &box->c_lr, sizeof(color))) {
		solid = 1;
	} else {
		h_ap1.r = (float)box->c_ul.r / b_height;
		h_ap1.g = (float)box->c_ul.g / b_height;
		h_ap1.b = (float)box->c_ul.b / b_height;
		h_ap1.a = (float)box->c_ul.a / b_height;

		h_ap2.r = (float)(box->c_ur.r - box->c_ul.r) / (b_width * b_height);
		h_ap2.g = (float)(box->c_ur.g - box->c_ul.g) / (b_width * b_height);
		h_ap2.b = (float)(box->c_ur.b - box->c_ul.b) / (b_width * b_height);
		h_ap2.a = (float)(box->c_ur.a - box->c_ul.a) / (b_width * b_height);

		h_bp1.r = (float)box->c_ll.r / b_height;
		h_bp1.g = (float)box->c_ll.g / b_height;
		h_bp1.b = (float)box->c_ll.b / b_height;
		h_bp1.a = (float)box->c_ll.a / b_height;

		h_bp2.r = (float)(box->c_lr.r - box->c_ll.r) / (b_width * b_height);
		h_bp2.g = (float)(box->c_lr.g - box->c_ll.g) / (b_width * b_height);
		h_bp2.b = (float)(box->c_lr.b - box->c_ll.b) / (b_width * b_height);
		h_bp2.a = (float)(box->c_lr.a - box->c_ll.a) / (b_width * b_height);
	}
		
	if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR) {
		blen = glen = rlen = min(min(fb_var.red.length,fb_var.green.length),fb_var.blue.length);
	} else {
		rlen = fb_var.red.length;
		glen = fb_var.green.length;
		blen = fb_var.blue.length;
	}

	for (y = box->y1; y <= box->y2; y++) {

		pic = target + (box->x1 + y * fb_var.xres) * bytespp;

		/* do a nice 2x2 ordered dithering, like it was done in bootsplash;
		 * this makes the pics in 15/16bpp modes look much nicer;
		 * the produced pattern is:
		 * 303030303..
		 * 121212121..
		 */
		add = (box->x1 & 1);
		add ^= (add ^ y) & 1 ? 1 : 3;

		for (x = box->x1; x <= box->x2; x++) {

			int t1 = b_height - (y-box->y1);
			int t2 = y - box->y1;	
			int t3 = x - box->x1;

			if (!solid) {
				a = t1 * (h_ap1.a + t3*h_ap2.a) + t2 * (h_bp1.a + t3*h_bp2.a);
				r = t1 * (h_ap1.r + t3*h_ap2.r) + t2 * (h_bp1.r + t3*h_bp2.r);
				g = t1 * (h_ap1.g + t3*h_ap2.g) + t2 * (h_bp1.g + t3*h_bp2.g);
				b = t1 * (h_ap1.b + t3*h_ap2.b) + t2 * (h_bp1.b + t3*h_bp2.b);
			} else {
				a = box->c_ul.a;
				r = box->c_ul.r;
				g = box->c_ul.g;
				b = box->c_ul.b;
			}
	
			if (a != 255) {
				if (fb_var.bits_per_pixel == 16) { 
					i = *(u16*)pic;
				} else if (fb_var.bits_per_pixel == 24) {
					i = *(u32*)pic & 0xffffff;
				} else if (fb_var.bits_per_pixel == 32) {
					i = *(u32*)pic;
				} else {
					i = *(u32*)pic & ((2 << fb_var.bits_per_pixel)-1);
				}

				r = (( (i >> fb_var.red.offset & ((1 << rlen)-1)) 
				      << (8 - rlen)) * (255 - a) + r * a) / 255;
				g = (( (i >> fb_var.green.offset & ((1 << glen)-1)) 
				      << (8 - glen)) * (255 - a) + g * a) / 255;
				b = (( (i >> fb_var.blue.offset & ((1 << blen)-1)) 
				      << (8 - blen)) * (255 - a) + b * a) / 255;
			}
		
			/* we only need to do dithering if depth is <24bpp */
			if (fb_var.bits_per_pixel < 24) {
				r = CLAMP(r + add*2 + 1);
				g = CLAMP(g + add);
				b = CLAMP(b + add*2 + 1);
			}
	
			r >>= (8 - rlen);
			g >>= (8 - glen);
			b >>= (8 - blen);

			i = (r << fb_var.red.offset) |
		 	    (g << fb_var.green.offset) |
			    (b << fb_var.blue.offset);

			if (fb_var.bits_per_pixel == 16) {
				*(u16*)pic = i;
				pic += 2;
			} else if (fb_var.bits_per_pixel == 24) {
				if (endianess == little) { 
					*(u16*)pic = i & 0xffff;
					pic[2] = (i >> 16) & 0xff;
				} else {
					*(u16*)pic = (i >> 8) & 0xffff;
					pic[2] = i & 0xff;
				}
				pic += 3;
			} else if (fb_var.bits_per_pixel == 32) {
				*(u32*)pic = i;
				pic += 4;
			}

			add ^= 3;
		}
	}
}
#endif

/* Interpolates two boxes, based on the value of the arg_progress variable.
 * This is a strange implementation of a progress bar, introduced by the
 * authors of Bootsplash. */
void interpolate_box(box *a, box *b)
{
	int h = PROGRESS_MAX - arg_progress;

	if (arg_progress == 0)
		return;
	
#define inter_color(cl1, cl2) 					\
{								\
	cl1.r = (cl1.r * h + cl2.r * arg_progress) / PROGRESS_MAX; 	\
	cl1.g = (cl1.g * h + cl2.g * arg_progress) / PROGRESS_MAX;	\
	cl1.b = (cl1.b * h + cl2.b * arg_progress) / PROGRESS_MAX;	\
	cl1.a = (cl1.a * h + cl2.a * arg_progress) / PROGRESS_MAX; 	\
}
	
	a->x1 = (a->x1 * h + b->x1 * arg_progress) / PROGRESS_MAX;
	a->x2 = (a->x2 * h + b->x2 * arg_progress) / PROGRESS_MAX;
	a->y1 = (a->y1 * h + b->y1 * arg_progress) / PROGRESS_MAX;
	a->y2 = (a->y2 * h + b->y2 * arg_progress) / PROGRESS_MAX;

	inter_color(a->c_ul, b->c_ul);
	inter_color(a->c_ur, b->c_ur);
	inter_color(a->c_ll, b->c_ll);
	inter_color(a->c_lr, b->c_lr);
}

char *get_program_output(char *prg, unsigned char origin)
{
	char *buf = malloc(1024);
	fd_set rfds;
	struct timeval tv;
	int pfds[2];
	pid_t pid;
	int i;
	
	if (!buf)
		return NULL;

        pipe(pfds);
	pid = fork();
	buf[0] = 0;
	
	if (pid == 0) {
		if (origin != FB_SPLASH_IO_ORIG_KERNEL) {
			/* Only play with stdout if we are NOT the kernel helper.
			 * Otherwise, things will break horribly and we'll end up
			 * with a deadlock. */
			close(1);
		}
		dup(pfds[1]);
	 	close(pfds[0]);
		execlp("sh", "sh", "-c", prg, NULL);
	} else {
		FD_ZERO(&rfds);
		FD_SET(pfds[0], &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 250000;
		i = select(pfds[0]+1, &rfds, NULL, NULL, &tv);
		if (i != -1 && i != 0) {	
			i = read(pfds[0], buf, 1024);
			if (i > 0) 
				buf[i] = 0;
		}
		
		close(pfds[0]);
		close(pfds[1]);
	}

	return buf;
}

char *eval_text(char *txt)
{
	char *p, *t, *ret, *d;
	int len, i, subst_len, need_subst;

	i = len = strlen(txt);
	p = txt;

	subst_len = progress_text?strlen(progress_text):0;
	need_subst = 0;
	
	while ((t = strstr(p, "$progress")) != NULL) { 
		len += subst_len;
		p = t+1;
		need_subst = 1;
	}

	ret = malloc(len+1);

	if (!need_subst) {
		strcpy(ret, txt);
		return ret;
	}
	
	p = txt;
	d = ret;
	
	while ((t = strstr(p, "$progress")) != NULL) {
		strncpy(d, p, t - p);
		d += (t-p);
		
		if (t > txt && *(t-1) == '\\') {
			*(d-1) = '$';
			p = t+1;
			continue;
		}

		if (progress_text)
			strcpy(d, progress_text);
		d += subst_len;
		p = t;
		p += 9;

		if (*p == '%')
			p++;
	}

	strcpy(d, p);	
	
	return ret;	
}

void prep_bgnd(u8 *target, u8 *src, int x, int y, int w, int h)
{
	char *t, *s;
	int j, i;

	t = target + (y * fb_var.xres + x) * bytespp;
	s = src    + (y * fb_var.xres + x) * bytespp;
	j = w * bytespp;	
	i = fb_var.xres * bytespp;
	
	for (y = 0; y < h; y++) {
		memcpy(t, s, j);
		t += i;
		s += i;
	}
}

/* Prepares the backgroud underneath objects that will be rendered in
 * render_objs() */
void prep_bgnds(u8 *target, u8 *bgnd, char mode)
{
	item *i;
	obj *o;
	icon *c;
	box *b, *n;

	for (i = objs.head; i != NULL; i = i->next) {
		o = (obj*)i->p;	

		if (o->type == o_box) {
			b = (box*)o->p;
				
			if (b->attr & BOX_SILENT && mode != 's')
				continue;

			if (!(b->attr & BOX_SILENT) && mode != 'v')
				continue;

			if ((b->attr & BOX_INTER) && i->next != NULL) {
				if (((obj*)i->next->p)->type == o_box) {
					n = (box*)((obj*)i->next->p)->p;
					prep_bgnd(target, bgnd, n->x1, n->y1, n->x2 - n->x1 + 1, n->y2 - n->y1 + 1);
				}
			}
		} else if (o->type == o_icon && mode == 's') {
			c = (icon*)o->p;

			if (c->status == 0)
				continue;

			if (!c->img)
				continue;

			if (!c->img->picbuf)
				continue;
			
			if (c->img->w > fb_var.xres - c->x || c->img->h > fb_var.yres - c->y) {
				continue;				
			}

			prep_bgnd(target, bgnd, c->x, c->y, c->img->w, c->img->h); 
		}	
#if (defined(CONFIG_TTY_KERNEL) && defined(TARGET_KERNEL)) || (defined(CONFIG_TTF) && !defined(TARGET_KERNEL))
		else if (o->type == o_text) {
			text *ct = (text*)o->p;
								
			if (mode == 's' && !(ct->flags & F_TXT_SILENT))
				continue;

			if (mode == 'v' && !(ct->flags & F_TXT_VERBOSE))
				continue;
		
			if (!ct->font || !ct->font->font)
				continue;

			prep_bgnd(target, bgnd, ct->x, ct->y, fb_var.xres - ct->x, ct->font->font->height);
		}
#endif
	}

#if (defined(CONFIG_TTF_KERNEL) && defined(TARGET_KERNEL)) || (!defined(TARGET_KERNEL) && defined(CONFIG_TTF))
	if (mode == 's') {
		prep_bgnd(target, bgnd, cf.text_x, cf.text_y, fb_var.xres - cf.text_x, global_font->height);
	}
#endif
}

void render_objs(u8 *target, u8 *bgnd, char mode, unsigned char origin, int progress_only)
{
	item *i;
	obj *o;
	icon *c;
	anim *a;
	box tmp, *b, *n;

	if (fb_var.bits_per_pixel == 8)
		return;

	if (bgnd)
		prep_bgnds(target, bgnd, mode);
	
	for (i = objs.head; i != NULL; i = i->next) {
		o = (obj*)i->p;	

		if (o->type == o_box) {
			b = (box*)o->p;

			if (progress_only && (b->attr & BOX_NOOVER))
				continue;

			if (b->attr & BOX_SILENT && mode != 's')
				continue;

			if (!(b->attr & BOX_SILENT) && mode != 'v')
				continue;
			
			if ((b->attr & BOX_INTER) && i->next != NULL) {
				if (((obj*)i->next->p)->type == o_box) {
					n = (box*)((obj*)i->next->p)->p;
					tmp = *b;
					interpolate_box(&tmp, n);
					render_box2(&tmp, target);
					i = i->next;
				}
			} else {
				render_box2(b, target);
			}
		} else if (o->type == o_icon && mode == 's') {
			if (progress_only)
				continue;

			c = (icon*)o->p;

			if (c->status == 0)
				continue;

			if (!c->img)
				continue;

			if (!c->img->picbuf)
				continue;
			
			if (c->img->w > fb_var.xres - c->x || c->img->h > fb_var.yres - c->y) {
				printwarn("Icon %s does not fit on the screen - ignoring it.", c->img->filename);
				continue;				
			}

			render_icon(c, target);
		} 
#if (defined(CONFIG_TTY_KERNEL) && defined(TARGET_KERNEL)) || (defined(CONFIG_TTF) && !defined(TARGET_KERNEL))
		else if (o->type == o_text) {

			text *ct = (text*)o->p;
			char *txt;
					
			if (progress_only && !(ct->flags & F_TXT_EVAL))
				continue;

			if (mode == 's' && !(ct->flags & F_TXT_SILENT))
				continue;

			if (mode == 'v' && !(ct->flags & F_TXT_VERBOSE))
				continue;
		
			if (!ct->font || !ct->font->font)
				continue;

			if (ct->flags & F_TXT_EXEC) {
				txt = get_program_output(ct->val, origin);
			} else if (ct->flags & F_TXT_EVAL) {
				txt = eval_text(ct->val);
			} else {
				txt = ct->val;
			}
			
			if (txt) {
				TTF_Render(target, txt, ct->font->font, ct->style, ct->x, ct->y, ct->col, ct->hotspot);
				if ((ct->flags & F_TXT_EXEC) || (ct->flags & F_TXT_EVAL))
					free(txt);
			}
		}
#endif
	}

#if (defined(CONFIG_TTF_KERNEL) && defined(TARGET_KERNEL)) || (!defined(TARGET_KERNEL) && defined(CONFIG_TTF))
	if (mode == 's' && !progress_only) {
		if (!boot_message)
			TTF_Render(target, DEFAULT_MESSAGE, global_font, TTF_STYLE_NORMAL, cf.text_x, cf.text_y, cf.text_color, F_HS_LEFT | F_HS_TOP);
		else {
			char *t;
			t = eval_text(boot_message);
			TTF_Render(target, t, global_font, TTF_STYLE_NORMAL, cf.text_x, cf.text_y, cf.text_color, F_HS_LEFT | F_HS_TOP);
			free(t);
		}
	}
#endif

	for (i = anims.head; i != NULL; i = i->next) {
		a = (anim*)i->p;
		if (!mng_render_next(a->mng)) {
			mng_display_restart(a->mng);
			mng_render_next(a->mng);
		}
		mng_display_next(a->mng, target, a->x, a->y);
	}
}

