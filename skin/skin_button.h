/*
 * Skin button handling header
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

#ifndef SKIN_BUTTON_H__
#define SKIN_BUTTON_H__

static inline int skin_button_mouse_over(SkinKey* button, int x, int y ) {
    return (x > button->posx &&
            y > button->posy &&
            x < button->posx+button->width &&
            y < button->posy+button->height);
}

int skin_button_handle_mouse(SkinKey* button, int mousebtn);
int skin_button_handle_mouseleave(SkinKey* button);
int skin_button_handle_key(SkinKey* button, int keypressed);

void skin_button_checkswitch(SkinScreen* skin, SkinButton* button);

#endif /* SKIN_BUTTON_H__ */
