/*
 * Skin button slider switchstate handling header
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
 
 #ifndef SKIN_SWITCHSTATE_H
 #define SKIN_SWITCHSTATE_H
 
 typedef enum SkinSwitchState {
    unknown = -1,
    inactive = 0,
    active
 } SkinSwitchState;

typedef int switchstate_callback(void *opaque, const int keycode);
 
 void qemu_skin_add_switchstate_callback(switchstate_callback *callback, 
            void *opaque);
            
#endif
