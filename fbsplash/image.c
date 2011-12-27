/*
 * image.c - Functions to load & unpack PNGs and JPEGs
 *
 * TuxOnIce userui adaptations:
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

#include <stdio.h>
#include <stdlib.h>
#include <linux/fb.h>

#include "config.h"

#ifdef CONFIG_PNG
#ifdef TARGET_KERNEL
  #include "libs/libpng-1.2.8/png.h"
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

struct fb_image verbose_img;
struct fb_image silent_img;

typedef struct {
	u8 r, g, b;
} __attribute__ ((packed)) rgbcolor;

#if 0
#undef png_sig_cmp
static int (*png_sig_cmp) PNGARG((png_bytep sig, png_size_t start,
	png_size_t num_to_check));
#endif

/* This function converts a truecolor image to whatever format the 
 * framebuffer uses */
void truecolor2fb (truecolor* data, u8* out, int len, int y, u8 alpha)
{
	int i, add = 0;
	rgbcolor* rgb = (rgbcolor*)data;
	
	add ^= (0 ^ y) & 1 ? 1 : 3;

	for (i = 0; i < len; i++) {
		if (alpha) {
			put_pixel(data->a, data->r, data->g, data->b, out, out, add);
			data++;
		} else {
			put_pixel(255, rgb->r, rgb->g, rgb->b, out, out, add);
			rgb++;
		}

		out += bytespp;
		add ^= 3;
	}
}

#ifdef CONFIG_PNG
#define PALETTE_COLORS 240
int load_png(char *filename, u8 **data, struct fb_cmap *cmap, unsigned int *width, unsigned int *height, u8 want_alpha)
{
	png_structp 	png_ptr;
	png_infop 	info_ptr, end_info;
	png_bytep 	row_pointer;
	png_colorp 	palette;
	int 		rowbytes, num_palette;
	int 		i, j, bytespp = (fb_var.bits_per_pixel + 7) >> 3;
	u8 *buf = NULL;
	u8 *t;
	FILE *fp;

	if (want_alpha)
		bytespp = 4;
	
	fp = fopen(filename,"r");
	if (!fp)
	{
		printk("Could not open file %s.\n", filename);
		return -1;
	}
	
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
		printk("Could not allocate PNG info struct. Out of memory?\n");
		return -1;
	}

	end_info = png_create_info_struct(png_ptr);
	if (!end_info)
	{
		png_destroy_read_struct(&png_ptr, (png_infopp)&info_ptr, (png_infopp)NULL);
		printk("Could not read file %s. Corrupt PNG header?\n", filename);
		return -1;
	}

	if (setjmp(png_jmpbuf(png_ptr)))
		goto failed;

	png_init_io(png_ptr, fp);
	png_read_info(png_ptr, info_ptr);

	if (cmap && png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_PALETTE) {
		printk("Could not read file %s. Not a palette-based image.\n", filename);
		goto failed;
	}

	if (png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_GRAY ||
	    png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);

	if (png_get_bit_depth(png_ptr, info_ptr) == 16)
		png_set_strip_16(png_ptr);

	if (!want_alpha && png_get_color_type(png_ptr, info_ptr) & PNG_COLOR_MASK_ALPHA)
		png_set_strip_alpha(png_ptr);

#ifndef TARGET_KERNEL	
	if (!(png_get_color_type(png_ptr, info_ptr) & PNG_COLOR_MASK_ALPHA) & want_alpha) {
		png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
	}
#endif
	png_read_update_info(png_ptr, info_ptr);

	if (!cmap && png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGB && png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGBA) {
		printk("Could not read file %s. Not an RGB image.\n", filename);
		goto failed;
	}

	if (cmap) {
		png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);
	
		if (num_palette > cmap->len) {
			printk("Could not read file %s. Too many colours in image (%d > %d).\n", filename, num_palette, cmap->len);
			goto failed;
		}
	}

	rowbytes = png_get_rowbytes(png_ptr, info_ptr);	

	if ((width && *width && png_get_image_width(png_ptr, info_ptr) != *width) || (height && *height && png_get_image_height(png_ptr, info_ptr) != *height)) {
		printk("Image size mismatch: %s.\n", filename);
		goto failed;
	} else {
		*width = png_get_image_width(png_ptr, info_ptr);
		*height = png_get_image_height(png_ptr, info_ptr);
	}

	*data = malloc(fb_var.xres * fb_var.yres * bytespp);
	if (!*data) {
		printk("Failed to allocate memory for image: %s.\n", filename);
		goto failed;
	}
	
	buf = malloc(rowbytes);	
	if (!buf) {
		printk("Failed to allocate memory for image line buffer.\n");
		free(*data);
		goto failed;
	}
	
	for (i = 0; i < png_get_image_height(png_ptr, info_ptr); i++) {
		if (cmap) {
			row_pointer = *data + png_get_image_width(png_ptr, info_ptr) * i;
		} else if (want_alpha) {
			row_pointer = *data + png_get_image_width(png_ptr, info_ptr) * i * 4;
		} else {
			row_pointer = buf;
		}
		
		png_read_row(png_ptr, row_pointer, NULL);
		
		if (cmap) {
			int h = 256 - cmap->len;
			t = *data + png_get_image_width(png_ptr, info_ptr) * i;

			if (h) {
				/* Move the colors up by 'h' offset. This is used because fbcon
				 * takes the first 16 colors. */
				for (j = 0; j < rowbytes; j++) {
					t[j] += h;
				}
			}
		
		/* We only need to convert the image if we the alpha channel is not required */	
		} else if (!want_alpha) {
			truecolor2fb((truecolor*)buf, *data + png_get_image_width(png_ptr, info_ptr) * bytespp * i, png_get_image_width(png_ptr, info_ptr), i, 0);
		}
	}

	if (cmap) {
		for (i = 0; i < cmap->len; i++) {
			cmap->red[i] = palette[i].red * 257;
			cmap->green[i] = palette[i].green * 257;
			cmap->blue[i] = palette[i].blue * 257;
		}	
	}

	free(buf);
	fclose(fp);
	
	png_destroy_read_struct(&png_ptr, (png_infopp)&info_ptr, (png_infopp)&end_info);
	return 0;
failed:
	png_destroy_read_struct(&png_ptr, (png_infopp)&info_ptr, (png_infopp)&end_info);
	return -1;
}

int is_png(char *filename)
{
	unsigned char header[8];
	FILE *fp = fopen(filename,"r");
	int result;
	
	if (!fp)
		return -1;

	result = fread(header, 1, 8, fp);
	fclose(fp);
	
	return !png_sig_cmp(header, 0, 8);
}
#endif /* PNG */

int load_jpeg(char *filename, u8 **data, int *width, int *height)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE* injpeg;

	u8 *buf = NULL;
	int i, bytespp = (fb_var.bits_per_pixel+7) >> 3;
	
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);
	
	if ((injpeg = fopen(filename,"r")) == NULL) {
		printk("Can't open file %s!\n", filename);
		return -1;	
	}

	jpeg_stdio_src(&cinfo, injpeg);
	jpeg_read_header(&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);

	if ((width && cinfo.output_width != *width) || (height && cinfo.output_height != *height)) {
		printk("Image size mismatch: %s.\n", filename);
		return -2;
	} else {
		*width = cinfo.output_width;
		*height = cinfo.output_height;
	}
	
	buf = malloc(cinfo.output_width * cinfo.output_components * sizeof(char));
	if (!buf) {
		printk("Failed to allocate JPEG decompression buffer.\n");
		return -1;
	}

	*data = malloc(cinfo.output_width * cinfo.output_height * bytespp);
	if (!*data) {
		printk("Failed to allocate memory for image: %s.\n", filename);
		return -4;
	}
	
	for (i = 0; i < cinfo.output_height; i++) {
		jpeg_read_scanlines(&cinfo, (JSAMPARRAY) &buf, 1);
		truecolor2fb((truecolor*)buf, *data + cinfo.output_width * bytespp * i, cinfo.output_width, i, 0);
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	fclose(injpeg);

	free(buf);
	return 0;
}

int load_bg_images(char mode)
{
	struct fb_image *img = (mode == 'v') ? &verbose_img : &silent_img;
	char *pic;
	int i;
	
	img->width = fb_var.xres;
	img->height = fb_var.yres;
	img->depth = fb_var.bits_per_pixel;

	/* Deal with 8bpp modes. Only PNGs can be loaded, and pic256
	 * option has to be used to specify the filename of the image */
	if (fb_var.bits_per_pixel == 8) {
		pic = (mode == 'v') ? cf_pic256 : cf_silentpic256;
		
		if (!pic) {
			printk("No 8bpp %s picture specified in the theme config.\n", (mode == 'v') ? "verbose" : "silent" );
			return -1;
		}

#ifdef CONFIG_PNG
		if (!is_png(pic)) {
			printk("Unrecognized format of the verbose 8bpp background image.\n");
			return -1;	
		}

		/* We have a palette of 256 colors, but fbcon takes 16 of these for 
		 * font colors in verbose mode, so we have 240 left for the picture */
		if (mode != 's') {
			i = PALETTE_COLORS;
			img->cmap.start = 16;
		} else {
			i = 256;
			img->cmap.start = 0;
		}
		
		img->cmap.transp = NULL;
		img->cmap.red = malloc(i * 3 * 2);
	
		if (!img->cmap.red) {
			printk("Failed to allocate memory for the image palette.\n");
			return -4;
		}
					
		img->cmap.green = img->cmap.red + i;
		img->cmap.blue = img->cmap.green + i;
		img->cmap.len = i;
		
		if (load_png(pic, (u8**)&img->data, &img->cmap, &img->width, &img->height, 0)) {
			printk("Failed to load PNG file %s.\n", pic);
			return -1;
		}	
#else
		printk("This version of splashutils has been compiled without support for 8bpp modes.\n");
		return -1;
#endif
	/* Deal with 15, 16, 24 and 32bpp modes */
	} else {
		pic = (mode == 'v') ? cf_pic : cf_silentpic;

		if (!pic) {
			if (mode == 's') /* only complain about the silent pic */
				printk("No silent picture specified in the theme config.\n");
			return -1;
		}
		
#ifdef CONFIG_PNG
		if (is_png(pic)) {
			i = load_png(pic, (u8**)&img->data, NULL, &img->width, &img->height, 0);
		} else
#endif
		{
			i = load_jpeg(pic, (u8**)&img->data, &img->width, &img->height);
		}
		
		if (i) {
			printk("Failed to load image %s.\n", pic);
			return -1;
		}
	}

	return 0;
}


int load_images(char mode)
{
	item *i;
	
	if (!config_file) {
		printk("No config file specified.\n");
		return -1;
	}
	
	if (mode == 'v' || mode == 'a')
		if (load_bg_images('v'))
			return -2;

	if (mode == 's' || mode == 'a') {
		if (load_bg_images('s'))
			return -2;

#ifdef CONFIG_PNG
		for (i = icons.head; i != NULL; i = i->next) {
			icon_img *ii = (icon_img*) i->p;
			ii->w = ii->h = 0;
			
			if (!is_png(ii->filename)) {
				printk("Icon %s is not a PNG file.\n", ii->filename);
				continue;
			}
			
			if (load_png(ii->filename, &ii->picbuf, NULL, &ii->w, &ii->h, 1)) {
				printk("Failed to load icon %s.\n", ii->filename);
				ii->picbuf = NULL;
				ii->w = ii->h = 0;
				continue;
			}
		}
#endif
	}

	return 0;
}


