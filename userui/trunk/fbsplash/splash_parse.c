/*
 * splash_parse.c - Functions for parsing bootsplash config files
 * 
 * Copyright (C) 2004, Michal Januszewski <spock@gentoo.org>
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * $Header: /srv/cvs/splash/utils/splash_parse.c,v 1.8 2004/09/04 18:15:03 spock Exp $
 * 
 */

#include <stdlib.h>
#include <string.h>
//#include <linux/types.h>
#include <stdio.h>
//#include <ctype.h>
#include <linux/fb.h>
#include "splash.h"

/* $Header: /srv/cvs/splash/utils/splash_parse.c,v 1.8 2004/09/04 18:15:03 spock Exp $ */

struct config_opt {
	char *name;
	enum { t_int, t_str, t_box } type;
	void *val;
};

char *cf_silentpic = NULL;
char *cf_pic = NULL;
char *cf_silentpic256 = NULL;	/* these are pictures for 8bpp modes */
char *cf_pic256 = NULL;

struct splash_box cf_boxes[MAX_BOXES];
int cf_boxes_cnt = 0;
struct splash_config cf;

int line = 0;

/* note that pic256 and silentpic256 have to be located before pic and 
 * silentpic or we are gonna get a parse error @ pic256/silentpic256. */

struct config_opt opts[] =
{
	{	.name = "jpeg",
		.type = t_str,
		.val = &cf_pic		},

	{	.name = "pic256",	
		.type = t_str,
		.val = &cf_pic256	},

	{	.name = "silentpic256",	
		.type = t_str,
		.val = &cf_silentpic256	},
	
	{	.name = "silentjpeg",
		.type = t_str,
		.val = &cf_silentpic	},

	{	.name = "pic",
		.type = t_str,
		.val = &cf_pic		},

	{	.name = "silentpic",
		.type = t_str,
		.val = &cf_silentpic	},
	
	{ 	.name = "bg_color",
		.type = t_int,
		.val = &cf.bg_color	},

	{	.name = "tx",
		.type = t_int,
		.val = &cf.tx		},

	{	.name = "ty",
		.type = t_int,
		.val = &cf.ty		},

	{	.name = "tw",
		.type = t_int,
		.val = &cf.tw		},

	{	.name = "th",
		.type = t_int,
		.val = &cf.th		},
	
	{	.name = "box",
		.type = t_box,
		.val = NULL		}
};


int isdigit(char c) {
	return (c >= '0' && c <= '9') ? 1 : 0;
}

int ishexdigit(char c) {
	return (isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) ? 1 : 0;
}

void skip_whitespace(char **buf) {

	while (**buf == ' ' || **buf == '\t')
		(*buf)++;
		
	return;
}

void parse_int(char *t, struct config_opt opt)
{
	if (*t != '=') {
		fprintf(stderr, "parse error @ line %d\n", line);
		return;
	}

	t++; skip_whitespace(&t);
	*(unsigned int*)opt.val = strtol(t,NULL,0);
}

void parse_string(char *t, struct config_opt opt)
{
	if (*t != '=') {
		fprintf(stderr, "parse error @ line %d\n", line);
		return;
	}

	t++; skip_whitespace(&t);
	*(char**)opt.val = strdup(t);
}

int parse_color(char **t, struct color *cl) 
{
	u32 h, len = 0;
	char *p;
	
	if (**t != '#') {
		return -1;
	}

	(*t)++;

	for (p = *t; ishexdigit(*p); p++, len++);

	p = *t;
	h = strtoul(*t, &p, 16);
	if (*t == p)
		return -2;

	if (len > 6) {
		cl->a = h & 0xff;
		cl->r = (h >> 24) & 0xff;
		cl->g = (h >> 16) & 0xff;
		cl->b = (h >> 8)  & 0xff;
	} else {
		cl->a = 0xff;
		cl->r = (h >> 16) & 0xff;
		cl->g = (h >> 8 ) & 0xff;
		cl->b = h & 0xff;
	}
	
	*t = p;

	return 0;
}

void parse_box(char *t)
{
	char *p;	
	int ret;
	
	struct splash_box cbox;
	
	skip_whitespace(&t);
	cbox.attr = 0;

	while (!isdigit(*t)) {
	
		if (!strncmp(t,"noover",6)) {
			cbox.attr |= BOX_NOOVER;
			t += 6;
		} else if (!strncmp(t, "inter", 5)) {
			cbox.attr |= BOX_INTER;
			t += 5;
		} else if (!strncmp(t, "silent", 6)) {
			cbox.attr |= BOX_SILENT;
			t += 6;
		} else {
			fprintf(stderr, "parse error @ line %d\n", line);
			return;
		}

		skip_whitespace(&t);
	}	
	
	cbox.x1 = strtol(t,&p,0);
	if (t == p)
		return;
	t = p; skip_whitespace(&t);
	cbox.y1 = strtol(t,&p,0);
	if (t == p)
		return;
	t = p; skip_whitespace(&t);
	cbox.x2 = strtol(t,&p,0);
	if (t == p)
		return;
	t = p; skip_whitespace(&t);
	cbox.y2 = strtol(t,&p,0);
	if (t == p)
		return;
	t = p; skip_whitespace(&t);

#define zero_color(cl) *(u32*)(&cl) = 0;
#define is_zero_color(cl) (*(u32*)(&cl) == 0)
#define assign_color(c1, c2) *(u32*)(&c1) = *(u32*)(&c2);
	
	zero_color(cbox.c_ul);
	zero_color(cbox.c_ur);
	zero_color(cbox.c_ll);
	zero_color(cbox.c_lr);
	
	if (parse_color(&t, &cbox.c_ul)) 
		goto pb_err;

	skip_whitespace(&t);

	ret = parse_color(&t, &cbox.c_ur);
	
	if (ret == -1) {
		assign_color(cbox.c_ur, cbox.c_ul);
		assign_color(cbox.c_lr, cbox.c_ul);
		assign_color(cbox.c_ll, cbox.c_ul);
		goto pb_end;
	} else if (ret == -2)
		goto pb_err;

	skip_whitespace(&t);

	if (parse_color(&t, &cbox.c_ll))
		goto pb_err;
	
	skip_whitespace(&t);

	if (parse_color(&t, &cbox.c_lr))
		goto pb_err;
pb_end:	
	cf_boxes[cf_boxes_cnt] = cbox;
	cf_boxes_cnt++;
	return;
pb_err:
	fprintf(stderr, "parse error @ line %d\n", line);
	return;
}

int parse_cfg(char *cfgfile)
{
	FILE* cfg;
	char buf[1024];
	char *t;
	int len, i;

	if ((cfg = fopen(cfgfile,"r")) == NULL) {
		fprintf(stderr, "Can't open config file %s.\n", cfgfile);
		return 1;
	}
	
	while (fgets(buf, sizeof(buf), cfg)) {

		line++;

		len = strlen(buf);
			
		if (len == 0 || len == sizeof(buf)-1)
			continue;

		buf[len-1] = 0;		/* get rid of \n */
		
		t = buf;	
		skip_whitespace(&t);
		
		/* skip comments */
		if (*t == '#')
			continue;
			
		for (i = 0; i < sizeof(opts) / sizeof(struct config_opt); i++) 
		{
			if (!strncmp(opts[i].name, t, strlen(opts[i].name))) {
	
				t += strlen(opts[i].name); 
				skip_whitespace(&t);

				switch(opts[i].type) {
				
				case t_str:
					parse_string(t, opts[i]);
					break;

				case t_int:
					parse_int(t, opts[i]);
					break;
				
				case t_box:
					parse_box(t);
					break;
				}
			}
		}
	}

	fclose(cfg);
	return 0;
}


