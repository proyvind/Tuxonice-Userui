/* usplash
 *
 * Copyright © 2006 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

void switch_console(int vt, int vt_fd);

void clear_screen(void);

void clear_progressbar(void);
void draw_progressbar(int percentage);

void clear_text(void);
void draw_text(const char *string, size_t len);
void draw_text_urgent(const char *string, size_t len);
void draw_line(const char *string, size_t len);
void draw_status(const char *string, size_t len, int mode);
void animate_step(int pulsating);
int usplash_setup(int xres, int yres, int verbose);
void usplash_restore_console(void);
int strncspn(const char *s, size_t n, const char *reject);
int handle_input(const char *string, size_t len, int quiet);

extern struct usplash_theme testcard_theme;
extern struct usplash_theme *theme;
extern int usplash_xres, usplash_yres;
extern int top_edge, left_edge;
