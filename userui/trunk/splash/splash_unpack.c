/*
 * splash_unpack.c - Functions to load & unpack PNGs and JPEGs
 *
 * Copyright (C) 2004, Michal Januszewski <spock@gentoo.org>
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * $Header: /srv/cvs/splash/utils/splash_unpack.c,v 1.9 2004/09/27 14:31:56 spock Exp $
 *
 */

#include <stdio.h>
#include <stdlib.h>
//#include <linux/types.h>
#include <linux/fb.h>

#include "config.h"

#ifdef CONFIG_PNG
#ifdef TARGET_KERNEL
  #include "libs/libpng-1.2.7/png.h"
#else
  #include <png.h>
#endif
#endif

#ifdef TARGET_KERNEL
  #include "libs/jpeg-6b/jpeglib.h"
#else
  #include <jpeglib.h>
#endif

#include "splash.h"

typedef struct truecolor {
	u8 r, g, b;
} __attribute__ ((packed)) truecolor;

void truecolor2fb (truecolor* data, u8* out, int len, int y)
{
	int i, add = 0, r, g, b;
	int rlen, blen, glen;
	truecolor tc;
	u32 t;
	
	if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR) {
		blen = glen = rlen = min(min(fb_var.red.length,fb_var.green.length),fb_var.blue.length);
	} else {
		rlen = fb_var.red.length;
		glen = fb_var.green.length;
		blen = fb_var.blue.length;
	}
		
	add ^= (0 ^ y) & 1 ? 1 : 3;

	for (i = 0; i < len; i++) {

		r = data[i].r;
		g = data[i].g;
		b = data[i].b;
		
		if (fb_var.bits_per_pixel < 24) {
			r = CLAMP(r + add*2 + 1);
			g = CLAMP(g + add);
			b = CLAMP(b + add*2 + 1);
		}

		tc.r = r >> (8 - rlen);
		tc.g = g >> (8 - glen);
		tc.b = b >> (8 - blen);

		t = (tc.r << fb_var.red.offset) | 
		    (tc.g << fb_var.green.offset) |
		    (tc.b << fb_var.blue.offset);

		switch (fb_var.bits_per_pixel) {

		case 32:
			*(u32*)(&out[(i * 4)]) = t;
			break;
		case 24:
			if (endianess == little) {
				*(u16*)(&out[(i*3)]) = t & 0xffff;
				*(u8*)(&out[(i*3+2)]) = t >> 16;
			} else {
				*(u16*)(&out[(i*3)]) = t >> 8;
				*(u8*)(&out[(i*3+2)]) = t & 0xff;
			}
			break;
		case 16:
		case 15:
			*(u16*)(&out[(i * 2)]) = t;
			break;
		}

		add ^= 3;
	}
}

void my_png_read_fn(png_structp png_ptr, png_bytep buf, png_size_t length)
{
	struct png_data *data = (struct png_data*)png_get_io_ptr(png_ptr);

	if (data->cur_pos + length > data->len)
		length = data->len - data->cur_pos;
	memcpy(buf, data->data+data->cur_pos, length);
	data->cur_pos += length;
}

#ifdef CONFIG_PNG
#define PALETTE_COLORS 240

int load_png(struct png_data *data, struct fb_image *img, char mode)
{
	png_structp 	png_ptr;
	png_infop 	info_ptr;
	png_bytep 	row_pointer;
	png_colorp 	palette;
	int 		num_palette;
	int 		rowbytes;
	int 		i, j, bytespp = fb_var.bits_per_pixel >> 3;
	u8 *buf = NULL;
	u8 *t;
	int 		pal_len;

	if (mode != 's')
		pal_len = PALETTE_COLORS;
	else
		pal_len = 256;
		
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	info_ptr = png_create_info_struct(png_ptr);

	if (setjmp(png_jmpbuf(png_ptr))) {
		return -1;
	}

	data->cur_pos = 0;
	png_set_read_fn(png_ptr, data, my_png_read_fn);
	png_read_info(png_ptr, info_ptr);

	if (fb_var.bits_per_pixel == 8 && info_ptr->color_type != PNG_COLOR_TYPE_PALETTE)
		return -2;

	if (info_ptr->bit_depth == 16)
		png_set_strip_16(png_ptr);

	if (info_ptr->color_type & PNG_COLOR_MASK_ALPHA)
		png_set_strip_alpha(png_ptr);

	if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY ||
	    info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);

	if (fb_var.bits_per_pixel == 8) {
		png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);
	
		/* we have a palette of 256 colors, but fbcon takes 16 of these for 
		 * font colors, so we have 240 left for the picture */
		if (num_palette > pal_len)
			return -3;
	}

	rowbytes = png_get_rowbytes(png_ptr, info_ptr);	
	
	if (!img->data)
		img->data = malloc(fb_var.xres * fb_var.yres * bytespp);
	if (!img->data)
		return -4;
	
	img->cmap.transp = NULL;
	if (fb_var.bits_per_pixel == 8) {
		img->cmap.red = malloc(pal_len * 3 * 2);
	
		if (!img->cmap.red) {
			free((void*)img->data);
			return -4;
		}
		
		img->cmap.green = img->cmap.red + 2 * pal_len;
		img->cmap.blue = img->cmap.green + 2 * pal_len;
		img->cmap.len = pal_len;
		
		if (mode == 'v')
			img->cmap.start = 16;
		else
			img->cmap.start = 0;
	} else {
		img->cmap.len = 0;
		img->cmap.red = NULL;
	}
	
	buf = malloc(rowbytes);	
	if (!buf) {
		free((void*)img->data);
		if (img->cmap.red)
			free((void*)img->cmap.red);
		return -4;
	}
	
	for (i = 0; i < info_ptr->height; i++) {
		if (fb_var.bits_per_pixel > 8) {
			row_pointer = buf;
		} else {
			row_pointer = (u8*)img->data + info_ptr->width * bytespp * i;
		}
		
		png_read_row(png_ptr, row_pointer, NULL);
		
		if (fb_var.bits_per_pixel > 8) {
			truecolor2fb((truecolor*)buf, (u8*)img->data + info_ptr->width * bytespp * i, info_ptr->width, i);
		} else {
			t = (u8*)img->data + info_ptr->width * bytespp * i;

			/* first 16 colors are taken by fbcon */
			if (mode == 'v') {
				for (j = 0; j < rowbytes; j++) {
					t[j] += 16;
				}
			}
		}
	}

	if (fb_var.bits_per_pixel == 8) {
	
		for (i = 0; i < num_palette; i++) {
			img->cmap.red[i] = palette[i].red * 257;
			img->cmap.green[i] = palette[i].green * 257;
			img->cmap.blue[i] = palette[i].blue * 257;
		}	
	}
	
	free(buf);
	
	return 0;
}
#endif /* PNG */

int decompress_jpeg(char *filename, struct fb_image *img)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE* injpeg;

	u8 *buf = NULL;
	int i, bytespp = fb_var.bits_per_pixel >> 3;
	
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);
	
	if ((injpeg = fopen(filename,"r")) == NULL) {
		fprintf(stderr, "Can't open file %s!\n", filename);
		return -1;	
	}

	jpeg_stdio_src(&cinfo, injpeg);
	jpeg_read_header(&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);

	buf = malloc(cinfo.output_width * cinfo.output_components * sizeof(char));
	img->data = malloc(cinfo.output_width * cinfo.output_height * bytespp);
	img->cmap.red = NULL;
	img->cmap.len = 0;
	
	for (i = 0; i < cinfo.output_height; i++) {
		jpeg_read_scanlines(&cinfo, (JSAMPARRAY) &buf, 1);
		truecolor2fb((truecolor*)buf, (u8*)img->data + cinfo.output_width * bytespp * i, cinfo.output_width, i);
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	fclose(injpeg);

	free(buf);
	return 0;
}

#ifdef CONFIG_PNG
int is_png(char *filename)
{
	char header[8];
	FILE *fp = fopen(filename,"r");
	
	if (!fp)
		return -1;

	fread(header, 1, 8, fp);
	fclose(fp);
	
	return !png_sig_cmp(header, 0, 8);
}
#endif

