/* usplash
 *
 * Copyright © 2006 Canonical Ltd.
 * Copyright © 2005 Matthew Garrett <mjg59@srcf.ucam.org>
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

#ifndef USPLASH_H
#define USPLASH_H

/* Directory of usplash control fifo */
#define USPLASH_DIR   "/dev/.initramfs"

/* Filename of usplash control fifo within the USPLASH_DIR directory*/
#define USPLASH_FIFO  "usplash_fifo"

/* Filename of usplash user feedback fifo within the USPLASH_DIR directory*/
#define USPLASH_OUTFIFO  "usplash_outfifo"

/* Location of usplash theme */
#define USPLASH_THEME "/usr/lib/usplash/usplash-artwork.so"

#endif				/* USPLASH_H */
