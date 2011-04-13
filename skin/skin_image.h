/*
 * Skin image drawing header
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "skin.h"

#ifndef SKIN_IMAGE_H__
#define SKIN_IMAGE_H__

SkinImage* skin_image_createtext( SkinScreen* skin, char* text);

void skin_rotate_buffer(SkinScreen* skin, DisplayState* ds, int x, int y, int w, int h);

void skin_fill_color( SkinScreen* skin, SkinArea* area, int r, int g, int b);
void skin_fill_background(SkinScreen* skin, SkinArea* area);
int skin_draw_button(SkinScreen* skin, SkinButton* button, int state);
int skin_highlight_key(SkinScreen* skin, SkinKey* key, int state);
int skin_draw_image(SkinScreen* skin, SkinImage* image, SkinArea* area);
int skin_draw_animated_keyboard(SkinScreen* skin, SkinImage* image, int state);

SkinArea skin_cliparea(SkinImage* image, SkinArea* area);

int skin_load_image_data(SkinImage* image, char* file);
SkinImage* skin_load_image(char* file);

void *skin_loadpng(const char *fn, unsigned *w, unsigned *h);

#endif /* SKIN_IMAGE_H__ */
