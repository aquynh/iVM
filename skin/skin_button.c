/*
 * Skin button handling
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
#include <stdio.h>
#include "console.h"

#include "skin_button.h"
#include "skin_switchstate.h"

typedef struct SkinButtonStateCallback {
    switchstate_callback *callback;
    void *opaque;
    // Next callback
    struct SkinButtonStateCallback *next;
} SkinButtonStateCallback;

static SkinButtonStateCallback *switchcb = NULL;

int skin_button_handle_mouse(SkinKey* button, int mousebtn)
{
    button->event.mouseover = 1;
    if(button->isswitch) {
        int newstate = button->state;
        if(!mousebtn && button->event.mousedown) {
            // Trigger the switch
            if(newstate == ESkinBtn_Highlighted)
                newstate = ESkinBtn_ActiveHighlighted;
            else
                newstate = ESkinBtn_Highlighted;
            if (button->keycode & 0x80) {
                kbd_put_keycode(0xe0);
                kbd_put_keycode(button->keycode);
            }
            else {
                kbd_put_keycode(button->keycode | 0x80);
            }
        }

        if(newstate == ESkinBtn_Idle)   newstate = ESkinBtn_Highlighted;
        if(newstate == ESkinBtn_Active) newstate = ESkinBtn_ActiveHighlighted;

        button->event.mousedown = mousebtn != 0;
        return newstate;
    } else {
        if(mousebtn && !button->event.mousedown) {
            if (button->keycode & 0x80) {
                kbd_put_keycode(0xe0);
                kbd_put_keycode(button->keycode & 0x7F);
            }
            else {
                kbd_put_keycode(button->keycode);
            }
        }
        if(!mousebtn && button->event.mousedown) {
            if (button->keycode & 0x80) {
                kbd_put_keycode(0xe0);
                kbd_put_keycode(button->keycode);
            }
            else {
                kbd_put_keycode(button->keycode | 0x80);
            }
        }
        button->event.mousedown = mousebtn != 0;
        return ESkinBtn_Highlighted;
    }
}

int skin_button_handle_mouseleave(SkinKey* button)
{
    if(button->isswitch) {
        int newstate = button->event.keydown == 0 ? ESkinBtn_Idle : ESkinBtn_Highlighted;
        button->event.mousedown = 0;
        button->event.mouseover = 0;
        if(button->state == ESkinBtn_Active ||
           button->state == ESkinBtn_ActiveHighlighted ) {
            if(!button->event.keydown) newstate = ESkinBtn_Active;
            else newstate = ESkinBtn_ActiveHighlighted;
        }
        return newstate;
    } else {
        if(button->event.mousedown) {
            if (button->keycode & 0x80) {
                kbd_put_keycode(0xe0);
                kbd_put_keycode(button->keycode);
            }
            else {
                kbd_put_keycode(button->keycode | 0x80);
            }
        }
        button->event.mousedown = 0;
        button->event.mouseover = 0;
        if(button->event.keydown) return ESkinBtn_Highlighted;
        return ESkinBtn_Idle;
    }
}

int skin_button_handle_key(SkinKey* button, int keypressed)
{
    int newstate = button->state;
    if(keypressed) {
        button->event.keydown = 1;
        switch(button->state) {
            case ESkinBtn_Idle:
                newstate = ESkinBtn_Highlighted;
                break;
            case ESkinBtn_Active:
                newstate = ESkinBtn_ActiveHighlighted;
                break;
            case ESkinBtn_Highlighted:
            case ESkinBtn_ActiveHighlighted:
                break;
            default:
                fprintf(stderr, "skin_key_handler, wrong state for switch having key: %d\n", button->keycode);
                break;
        }
    } else {
        button->event.keydown = 0;
        switch(button->state) {
            case ESkinBtn_Idle:
            case ESkinBtn_Active:
                break;
            case ESkinBtn_Highlighted:
                if(!button->event.mouseover) newstate = ESkinBtn_Idle;
                break;
            case ESkinBtn_ActiveHighlighted:
                if(!button->event.mouseover) newstate = ESkinBtn_Active;
                break;
            default:
                fprintf(stderr, "skin_key_handler, wrong state for switch having key: %d\n", button->keycode);
                break;
        }
    }

    if(!keypressed && button->isswitch) {
        switch(newstate) {
            case ESkinBtn_Idle:
            case ESkinBtn_Highlighted:
                if(!button->event.mouseover) newstate = ESkinBtn_Active;
                else newstate = ESkinBtn_ActiveHighlighted;
                break;
            case ESkinBtn_Active:
            case ESkinBtn_ActiveHighlighted:
                if(!button->event.mouseover) newstate = ESkinBtn_Idle;
                else newstate = ESkinBtn_Highlighted;
                break;
            default:
                fprintf(stderr, "skin_key_handler, wrong state for switch having key: %d\n", button->keycode);
                break;
        }
    }
    return newstate;
}

void skin_button_checkswitch(SkinScreen* skin, SkinButton* button)
{
    //printf(">> skin_button_checkswitch\n");
    if (button->key.isswitch) {
        int hwstate = unknown;
        SkinButtonStateCallback *cb = switchcb;
        while (cb && hwstate == unknown) {
            hwstate = cb->callback(cb->opaque, button->key.keycode);
            if (cb->next) cb = cb->next;
            else break;
        }        
        
        if (button->key.defaultstate == on) {
            // If default switch state is on, it overrides the hardware state. 
            // Make sure the hardware state is in sync also.
            //printf("skin_button_checkswitch, button=%d, defaultstate=on\n", button->key.keycode);
            if (hwstate == inactive) {
                //printf("hwstate=inactive, defaultstate alreadyset for button %d\n", button->key.keycode);
                button->key.defaultstate = undefined;
                kbd_put_keycode(button->key.keycode | 0x80);
                hwstate = cb->callback(cb->opaque, button->key.keycode);
                //printf("defaultstate=on, hwstate=%d\n", hwstate);
            }
        }
        
        if (button->key.defaultstate == off) {
            // If default switch state is off, it overrides the hardware state. 
            // Make sure the hardware state is in sync also.
            //printf("skin_button_checkswitch, button=%d, defaultstate=off\n", button->key.keycode);
            if (hwstate == active) {
                //printf("hwstate=active, defaultstate alreadyset for button %d\n", button->key.keycode);
                button->key.defaultstate = undefined;
                kbd_put_keycode(button->key.keycode | 0x80);
                hwstate = cb->callback(cb->opaque, button->key.keycode);
                //printf("defaultstate=%d, hwstate=%d for button %d\n", button->key.defaultstate, hwstate, button->key.keycode);
            }
        }
        
        // If we retrieved a valid state, update the button accordingly
        if (hwstate == inactive) {
            if (button->key.state == ESkinBtn_Active) button->key.state = ESkinBtn_Idle;
            if (button->key.state == ESkinBtn_ActiveHighlighted) button->key.state = ESkinBtn_Highlighted;
        }
        if (hwstate == active) {
            if (button->key.state == ESkinBtn_Idle) button->key.state = ESkinBtn_Active;
            if (button->key.state == ESkinBtn_Highlighted) button->key.state = ESkinBtn_ActiveHighlighted;
        }
        
        // If the hardware state is not yet retrieved, go with the defaultstate
        if (hwstate == unknown && button->key.defaultstate == off) {
            button->key.state = ESkinBtn_Idle;
        }
        //printf("keycode: %d, hwstate: %d\n", button->key.keycode, hwstate);
    }
    //printf("skin_button_checkswitch >>\n"); 
}

void qemu_skin_add_switchstate_callback(switchstate_callback *callback,
                                        void *opaque)
{
    SkinButtonStateCallback *cb = switchcb;
    if (!switchcb) {
        switchcb = (SkinButtonStateCallback*)qemu_mallocz(sizeof(SkinButtonStateCallback));
        cb = switchcb;
    }
    else {
        while (cb) cb = cb->next;
        cb = (SkinButtonStateCallback*)qemu_mallocz(sizeof(SkinButtonStateCallback));
    }

    if (cb) {
        // Store the parameters for later use
        cb->callback = callback;
        cb->opaque = opaque;
        
      /*  if (cb->callback(opaque, 59) == active) {
            printf("qemu_skin_add_switchstate_callback() registered keyboard: %d\n", cb->callback(opaque, 59));
        }*/
    }
}

