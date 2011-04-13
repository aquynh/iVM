/*
 * Qemu main skinning
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
#include <console.h>

#include "skin.h"
#include "skin_config.h"
#include "skin_image.h"
#include "skin_button.h"
#include "qemu-timer.h"
#include "qemu_socket.h"
#include "console.h"
#include <SDL/SDL.h>

struct SkinScreen *skin = NULL;
// Timer for animated keyboard
static QEMUTimer *keyboard_timer = NULL;
static int keyboard_animation_phase = 0;
#define ANIM_NOT_USED 13 // Defined instead of const int because of case-usage
static int no_keyboard_anim = 0;
const int ZOOM_STEP = 10;
const int ZOOM_INDICATOR_POS = 26;
const int ZOOM_MIN_FACTOR = 50;
const int ZOOM_MAX_FACTOR = 130;
static int zoom_factor = 100;
static int first_keyboard_check = 1;
static int startup = 1;
static int rct_sock = -1;

// #jun:hacking
DisplayState *es_ds = NULL;
int es_posx, es_posy;

// Local prototypes
static void setup_keyboard_animation_timer(void);
static void skin_update_keyboard(void *opaque);
static void skin_draw_skin(SkinArea* area);
static void skin_cleartooltip(void *opaque);
static void skin_handle_zooming(void);
static void skin_position_items(int move);
static void skin_rct_serve(void *opaque);
static int skin_rct_initialize(int rctport);

// Local prototypes used externally
int skinning_init(char* skin_file, int portrait, int rctport);
void skin_toggle_full_screen(DisplayState *ds);
// Overruled functions from console.c
DisplayState *qemu_graphic_console_init(vga_hw_update_ptr update,
                                        vga_hw_invalidate_ptr invalidate,
                                        vga_hw_screen_dump_ptr screen_dump,
                                        vga_hw_text_update_ptr text_update,
                                        void *opaque);

void original_qemu_console_resize(DisplayState *ds, int width, int height);

static void skin_handle_rotation(void)
{
    if (skin->rotation != skin->rotation_req) {
        skin_cleartooltip(NULL);
        skin->rotation = skin->rotation_req;
        skin_activate_layout(skin, skin->rotation);
        SkinButton* b = skin->buttons;
        while (b) {
            b->key.defaultstate = undefined;
            b = b->next;
        }
        no_keyboard_anim = 1;
        // Update keyboard if needed
        if (skin->keyboard.button &&
            (skin->keyboard.button->key.state == ESkinBtn_Active ||
             skin->keyboard.button->key.state == ESkinBtn_ActiveHighlighted)) {
            // Check if the skin offset should be applied
            if (skin->rotation == on && skin->keyboard.offset != 0) {
                skin_position_items(1);
            }
            // Resizing screen and updating keyboard without animation
            if (keyboard_timer) qemu_del_timer(keyboard_timer);
            original_qemu_console_resize(skin->ds, skin->keyboard.screenwidth,
                                         skin->keyboard.screenheight);
            skin_update_keyboard(NULL);
            skin_handle_zooming();
        } 
        else {
            // No keyboard needs to be drawn
            if (keyboard_timer) qemu_del_timer(keyboard_timer);
            original_qemu_console_resize(skin->ds, skin->width, skin->height);
            skin_handle_zooming();
        }
        if (skin->rotation) {
            kbd_put_keycode(0x4f); // x -= axis_max
            kbd_put_keycode(0x4c); // y += axis_max
            kbd_put_keycode(0x4f | 0x80);
            kbd_put_keycode(0x4c | 0x80);
        } else {
            kbd_put_keycode(0x50); // x += axis_max
            kbd_put_keycode(0x4b); // y -= axis_max
            kbd_put_keycode(0x50 | 0x80);
            kbd_put_keycode(0x4b | 0x80);
        }
    }
}

static int skin_overlaps(SkinImage* image, SkinArea* area)
{
    if (image->posx + image->width < area->x) return 0;
    if (image->posy + image->height < area->y) return 0;
    if (area->x + area->width < image->posx) return 0;
    if (area->y + area->height < image->posy) return 0;
    return 1;
}

static void calculate_tooltip_position( SkinButton* button,
                                        SkinImage* tooltip )
{
    // Determine the tooltip position
    // Best would be in the center below the button
    int x = 0, y = 0;
    x = button->image.posx + button->image.width / 2 - tooltip->width / 2;
    y = button->image.posy + button->image.height;
    // If it moves out top or left, move it to the edge
    if (x < 0) x = 2;
    if (y < 0) y = 2;
    // If it moves out bottom or right, move it to the edge
    if (x + tooltip->width > ds_get_width(skin->ds))
        x = ds_get_width(skin->ds) - (tooltip->width + 2);
    if (y + tooltip->height > ds_get_height(skin->ds))
        y = ds_get_height(skin->ds) - (tooltip->height + 2);
    // Put the tooltip position to the tooltip image
    tooltip->posx = x;
    tooltip->posy = y;
}

static void skin_draw_tooltip(SkinArea *clip)
{
    if (skin->tooltip.image &&
        skin_overlaps(skin->tooltip.image, clip) ) {
        skin_draw_image(skin, skin->tooltip.image, clip);
    }
}

static void skin_cleartooltip(void *opaque)
{
    // Clear the timer and tooltip
    if (skin->tooltip.timer) {
        qemu_del_timer(skin->tooltip.timer);
    }
    skin->tooltip.button = NULL;
    if (skin->tooltip.image) {
        SkinArea clip = { skin->tooltip.image->posx,
                          skin->tooltip.image->posy,
                          skin->tooltip.image->width,
                          skin->tooltip.image->height };
        if (skin->tooltip.image->data) qemu_free(skin->tooltip.image->data);
        qemu_free(skin->tooltip.image);
        skin->tooltip.image = NULL;
        // Redraw the skin
        skin_draw_skin(&clip);
        // Make sure the emulated screen is redrawn if needed
        if (skin->es) {
            vga_hw_invalidate();
            vga_hw_update();
        }
        dpy_update(skin->ds, clip.x, clip.y, clip.width, clip.height);
    }
}

static void skin_handle_tooltip_timeout(void *opaque)
{
    // Time to show the tooltip
    SkinTooltip* tooltip = (SkinTooltip*)opaque;
    if (tooltip->button) {
        // Create the correct text image for the button
        SkinImage *image =
            skin_image_createtext(skin, tooltip->button->tooltip);
        if (image) {
            // Find the correct location to draw the tooltip
            calculate_tooltip_position(tooltip->button, image);
            // Everything set, store the image and have it drawn
            tooltip->image = image;
            struct SkinArea clip = { image->posx, image->posy,
                                     image->width, image->height };
            skin_draw_tooltip(&clip);
            dpy_update(skin->ds, image->posx, image->posy,
                       image->width, image->height);
        }
    }
}

static void skin_mouse_event(void *opaque,
                             int x, int y, int z, int buttons_state)
{
    if (!is_graphic_console()) return;

    static int in_screen = 0;

    skin->mouse_event = on;
    // Mouse position mx/my in range 0-32767, equals pixel 0 to width/height-1
    // actual mouse x and y on the application
    int mx = x;
    int my = y;
    x = (mx - skin->es->posx) * 0x7FFF / ds_get_width(skin->es->ds);
    y = (my - skin->es->posy) * 0x7FFF / ds_get_height(skin->es->ds);

//    printf("skin_mouse_event:  %d, %d btn: %d (w:%d, h:%d)\n", x, y, buttons_state,
//                    ds_get_width(skin->ds), ds_get_height(skin->ds));
    QEMUPutMouseEntry *child = (QEMUPutMouseEntry *)opaque;
//    printf("skin_mouse_event: mx:%d, my:%d, posx:%d, posy:%d, relw: %d, relh: %d, w:%d, h:%d\n", mx, my, skin->es->posx, skin->es->posy, ds_get_width(skin->es->ds), ds_get_height(skin->es->ds), skin->es->width, skin->es->height);
    if (skin->rotation == off) {
        if (mx > skin->es->posx &&
            mx < skin->es->posx + ds_get_width(skin->es->ds) /*skin->es->width*/ &&
            my > skin->es->posy &&
            my < skin->es->posy + ds_get_height(skin->es->ds) /*skin->es->height*/ ) {
//            printf("skin_mouse_event: report x: %d, y: %d\n", mx - skin->es->posx, my - skin->es->posy);

            if (!in_screen) {
                in_screen = 1;
                SDL_ShowCursor(0);
            }

            child->qemu_put_mouse_event( child->qemu_put_mouse_event_opaque,
                    (mx - skin->es->posx) * 0x7FFF / ds_get_width(skin->es->ds),
                    (my - skin->es->posy) * 0x7FFF / ds_get_height(skin->es->ds),
                    z, buttons_state);
        } else {
            if (in_screen) {
                in_screen = 0;
                SDL_ShowCursor(1);
            }

        }
    }
    else {
        // We have to change the coordinates, since we draw rotated
        if (mx > skin->es->posx &&
            mx < skin->es->posx + skin->es->height &&
            my > skin->es->posy &&
            my < skin->es->posy + skin->es->width ) {

            child->qemu_put_mouse_event( child->qemu_put_mouse_event_opaque,
                                         y, -x, z, buttons_state);
            SDL_ShowCursor(0);
        } else {
            SDL_ShowCursor(1);
        }
    }
    
    // Check if we pressed any button
    SkinButton* button = skin->buttons;
    while (button) {
        if (skin_button_mouse_over(&button->key, mx, my)) {
            // Check if we handle start tooltip handling
            if (button->tooltip &&
                skin->tooltip.button != button) {
                skin_cleartooltip(NULL);
                skin->tooltip.button = button;
                if (!skin->tooltip.timer) {
                    skin->tooltip.timer = qemu_new_timer_ns(rt_clock,
                                                         skin_handle_tooltip_timeout,
                                                         &skin->tooltip);
                }
                // Set expiration time to be 1 second
                qemu_mod_timer(skin->tooltip.timer, qemu_get_clock_ns(rt_clock) + 1000);
            }
            int state = skin_button_handle_mouse(&button->key, buttons_state & MOUSE_EVENT_LBUTTON );
            if (skin_draw_button(skin, button, state)) {
                dpy_update(skin->ds, button->image.posx, button->image.posy,
                           button->image.width, button->image.height);
            }
        }
        else {
            if (button == skin->tooltip.button) {
                skin_cleartooltip(NULL);
            }
            int state = skin_button_handle_mouseleave(&button->key);
            if (skin_draw_button(skin, button, state)) {
                dpy_update(skin->ds, button->image.posx, button->image.posy,
                           button->image.width, button->image.height);
            }
        }
        button = button->next;
    }
    // Check if we pressed any key from the keyboard
    if (skin->keyboard.button &&
        (skin->keyboard.button->key.state == ESkinBtn_Active ||
         skin->keyboard.button->key.state == ESkinBtn_ActiveHighlighted)) {
        SkinKey* key = skin->keyboard.keys;
        while (key) {
            if (skin_button_mouse_over(key, mx, my)) {
                int state = skin_button_handle_mouse(key, buttons_state & MOUSE_EVENT_LBUTTON);
                if (skin_highlight_key(skin, key, state)) {
                    dpy_update(skin->ds, key->posx, key->posy,
                               key->width, key->height);
                }
            }
            else {
                int state = skin_button_handle_mouseleave(key);
                if (skin_highlight_key(skin, key, state)) {
                    dpy_update(skin->ds, key->posx, key->posy,
                               key->width, key->height);
                }
            }
            key = key->next;
        }
    }

    skin_handle_rotation();
    skin->mouse_event = off;
}

static void skin_position_items(int move)
{
    if (skin->keyboard.offset) {
        if (skin->background != NULL) {
            skin->background->posx += move * skin->keyboard.offset;
        }
        skin->es->posx += move * skin->keyboard.offset;
        
        SkinButton* button = skin->buttons;
        while (button) {
            button->image.posx += move * skin->keyboard.offset;
            button->key.posx += move * skin->keyboard.offset;
            button = button->next;
        }
    }
}

static void skin_animate_keyboard(int phase)
{
    //printf(">> skin_animate_keyboard, phase=%d\n", phase);
    if (skin->keyboard.button) {
        switch (skin->keyboard.button->key.state) {
            // Keyboard opening
            case ESkinBtn_Active:
            case ESkinBtn_ActiveHighlighted:
		    {
		        if (phase == ANIM_NOT_USED) {
		            // No animation, just draw the full keyboard
		            //printf("skin_animate_keyboard, no animation, draw keyboard\n");
				    SkinArea area = { skin->keyboard.image->posx,
                                      skin->keyboard.image->posy,
                                      skin->keyboard.image->width,
                                      skin->keyboard.image->height };
                    skin_draw_image(skin,
                                    skin->keyboard.image,
                                    &area);
                }
                if (phase == 0) {
                    // Resize the screen to fit keyboard
                    //printf("skin_animate_keyboard, expand screen\n");
                    keyboard_animation_phase++;
                    skin_cleartooltip(NULL);
                    skin_position_items(1);
                    original_qemu_console_resize(skin->ds, skin->keyboard.screenwidth,
                                                 skin->keyboard.screenheight);
                    skin_handle_zooming();
                    break;
                }
                    
                if (phase > 2 && phase < 7) {
                    // Draw actual animation phases
                    //printf("skin_animate_keyboard, opening, phase=%d\n", phase);
                    skin_draw_animated_keyboard(skin, skin->keyboard.image, 7-phase);
                    dpy_update(skin->ds, skin->keyboard.image->posx, 
                               skin->keyboard.image->posy,
                               skin->keyboard.image->width, 
                               skin->keyboard.image->height);
                }
                else if (phase == 7) {
                    // Draw fully open keyboard
                    //printf("skin_animate_keyboard, open\n");
                    keyboard_animation_phase = 8;
                    skin_draw_animated_keyboard(skin, skin->keyboard.image, 0);
                    dpy_update(skin->ds, skin->keyboard.image->posx, 
                               skin->keyboard.image->posy,
                               skin->keyboard.image->width, 
                               skin->keyboard.image->height);
                }
		    }
            break;
            // Keyboard closing
            case ESkinBtn_Idle:
            case ESkinBtn_Highlighted:
            {
                // Draw actual animation phases
                if (phase > 2 && phase < 7) {
                    skin_draw_animated_keyboard(skin, skin->keyboard.image, phase-2);
                    dpy_update(skin->ds, skin->keyboard.image->posx, 
                               skin->keyboard.image->posy,
                               skin->keyboard.image->width, 
                               skin->keyboard.image->height);
                }
                else if (phase == 7) {
                    // Shrink the screen, no keyboard
                    //printf("skin_animate_keyboard, shrink screen\n");
                    keyboard_animation_phase = 8;
                    skin_cleartooltip(NULL);
                    skin_position_items(-1);
                    original_qemu_console_resize(skin->ds, skin->width, skin->height);
                    skin_handle_zooming();
                }
            }
            default:         
			    // Do nothing 
                break;
        }
    }
    //printf("skin_animate_keyboard >>\n");
}

static void skin_update_keyboard(void *opaque)
{
    if (!skin->keyboard.animated || no_keyboard_anim) {
        // Animated keyboard is not used
        keyboard_animation_phase = ANIM_NOT_USED;
        no_keyboard_anim = 0;
    }
        
    //printf("skin_update_keyboard, phase=%d\n", keyboard_animation_phase);
    switch (keyboard_animation_phase) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            //printf("skin_update_keyboard, executing anim...\n");
            skin_animate_keyboard(keyboard_animation_phase);
            keyboard_animation_phase++;
            setup_keyboard_animation_timer();
            break;
        case 9:
            //printf("skin_update_keyboard, end...\n");
            keyboard_animation_phase = ANIM_NOT_USED;
            break;
        case ANIM_NOT_USED:
            skin_animate_keyboard(keyboard_animation_phase);
            break;
        default:
            // Do nothing
            break;
    }    
}

static void setup_keyboard_animation_timer(void)
{
    // Set up timer for keyboard animation 
    //printf("setup_keyboard_timer\n");
    if (keyboard_timer) qemu_del_timer(keyboard_timer);
    // Create new timer and call skin_update_keyboard when it expires
    keyboard_timer = qemu_new_timer_ns(rt_clock, skin_update_keyboard, NULL);
    // Set expiration time to be 0.1 seconds
    qemu_mod_timer(keyboard_timer, qemu_get_clock_ns(rt_clock) + 100);
        
}

static void skin_draw_skin(SkinArea* area)
{
    // Skinning draws in various layers, from bottom to top:
    // background color - background image - keyboard - buttons

    // Set the background color
    skin_fill_background(skin, area);
    // Draw the background image
    if (skin->background &&
        skin_overlaps(skin->background, area)) {
        skin_draw_image(skin, skin->background, area);
    }
    // Draw the keyboard
    if (skin->keyboard.image && skin->keyboard.keys && !first_keyboard_check) {
        skin_update_keyboard(NULL);
    }
    // Draw the buttons
    SkinButton* button = skin->buttons;
    while (button) {
        if (skin_overlaps(&button->image, area)) {
            // Check the switch state when redrawing the skin. Check the
            // keyboard switch only once, because it messes up the animated 
            // keyboard if pressed it open from computer keyboard.
            if (button == skin->keyboard.button && first_keyboard_check) {
                skin_button_checkswitch(skin, button);
                first_keyboard_check = 0;
                no_keyboard_anim = 1;
                skin_update_keyboard(NULL);
            }
            else if (button != skin->keyboard.button) {
                skin_button_checkswitch(skin, button);
            }
            skin_draw_button(skin, button, ESkinBtn_forceredraw);
        }
        button = button->next;
    }
}

#define RCT_BUFSIZE 256
static void skin_rct_serve(void *opaque)
{
    struct sockaddr_in addr;
		int i;
    socklen_t addrlen = sizeof(addr);
    int csock = qemu_accept(rct_sock, (struct sockaddr *)&addr, &addrlen);
    if (csock >= 0) {
        char req[RCT_BUFSIZE];
        char rep[RCT_BUFSIZE] = "ERROR:unidentified request";
        int rlen;

        // read and handle incoming RCT request
        if ( (rlen = recv(csock, (void *)req, RCT_BUFSIZE, 0)) > 0) {
						for(i=0;i<rlen;i++) {
							printf("%d", req[i]);
						}
						printf("\n");
            if (strncmp(req, "getzoom", 7) == 0) {
                sprintf(rep, "OK:%d", zoom_factor);
            } else if (strncmp(req, "getrotation", 11) == 0) {
                sprintf(rep, "OK:%s", skin->rotation ? "on" : "off");
            } else if (strncmp(req, "setzoom", 7) == 0) {
                char *endp;
                int arg, newzoom, ok = 0;

								printf("char 8 %c\n", req[7]);
                if (req[7] == '\n') {
                    sprintf(rep, "ERROR:missing setzoom argument");
                } else if (req[7] == '=') {
                    arg = strtol(req + 8, &endp, 10);
                    newzoom = arg;
                    ok = 1;
                } else if ((req[7] == '+' || req[7] == '-') && req[8] == '=') {
                    // set relative to old value
                    arg = strtol(req + 9, &endp, 10);
                    if (!*endp && req[7] == '-') {
                        arg = -arg;
                    }
                    newzoom = zoom_factor + arg;
                    ok = 1;
                }

                if (ok) {
                    if (*endp) {
                        sprintf(rep, "ERROR:invalid setzoom argument:%s",
                                req + ((req[7] == '=') ? 8 : 9));
                    } else if (newzoom < ZOOM_MIN_FACTOR ||
                               newzoom > ZOOM_MAX_FACTOR) {
                        sprintf(rep, "ERROR:new zoom factor out of range:%d",
                                newzoom);
                    } else {
                        zoom_factor = newzoom;
                        skin_handle_zooming();
                        sprintf(rep, "OK:%d", zoom_factor);
                    }
                } // otherwise the default error message applies
            } else if (strncmp(req, "setrotation", 11) == 0) {
                if (req[11] == '\n') {
                    // toggle current state if no argument
                    skin->rotation_req = (skin->rotation == off) ? on : off;
                    skin_handle_rotation();
                    sprintf(rep, "OK:%s",
                            (skin->rotation == on) ? "on" : "off");
                } else if (strncmp(req + 11, "=on", 3) == 0) {
                    skin->rotation_req = on;
                    skin_handle_rotation();
                    sprintf(rep, "OK:on");
                } else if (strncmp(req + 11, "=off", 4) == 0) {
                    skin->rotation_req = off;
                    skin_handle_rotation();
                    sprintf(rep, "OK:off");
                } else if (req[11] == '=') {
                    sprintf(rep, "ERROR:invalid setrotation argument\n");
                } // otherwise the default error message applies
            }

            if (send(csock, (const void *)rep, strlen(rep) + 1, 0) < 0) {
                fprintf(stderr, "%s: send(): %s\n",
                        __FUNCTION__, strerror(socket_error()));
            }
        }

        closesocket(csock);
    } else {
        fprintf(stderr, "%s: qemu_accept(): %s\n",
                __FUNCTION__, strerror(socket_error()));
    }
}

static int skin_rct_initialize(int rctport)
{
    struct sockaddr_in addr;

    if ( (rct_sock = qemu_socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "%s: qemu_socket(): %s\n",
                __FUNCTION__, strerror(socket_error()));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(rctport);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(rct_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "%s: bind(): %s\n", __FUNCTION__, strerror(socket_error()));
        return -1;
    }

    if (listen(rct_sock, 1) < 0) {
        fprintf(stderr, "%s: listen(): %s\n", __FUNCTION__, strerror(socket_error()));
        return -1;
    }

    return qemu_set_fd_handler(rct_sock, skin_rct_serve, NULL, NULL);
}

int skinning_init(char* skin_file, int portrait, int rctport)
{
    skin = skin_load_configuration(skin_file, portrait);

    if (skin) {
        if (skin->rotation == on && skin->keyboard.image &&
            skin->keyboard.keys &&
            (skin->keyboard.button->key.state == ESkinBtn_Active ||
             skin->keyboard.button->key.state == ESkinBtn_ActiveHighlighted)) {
            skin_position_items(1);
        }
        if (rctport) {
            skin_rct_initialize(rctport);
        }
    }
    return (skin == NULL);
}

static void skin_host_update(DisplayState *ds, int x, int y, int w, int h)
{
    //printf("skin_host_update()\n");
    // We probably initiated this
}

static void skin_host_setdata(DisplayState *ds)
{
    //printf("skin_host_setdata\n");
}

static void skin_host_resize(DisplayState *ds)
{
    if (!is_graphic_console()) return;
    if (startup) {
        int width = 0, height = 0;
        dpy_getresolution(ds, &width, &height);
        //printf("%s, dpy_getresolution: width=%d, height=%d, skin->width=%d, skin->height=%d\n", __FUNCTION__, width, height, skin->width, skin->height);
        if (width != 0 && height != 0) {
            startup = 0;
#if 0
            if (skin->width >= width ||
                skin->height >= height ) {
                // Determine zoom level
                while( (skin->width * zoom_factor / 100 >= width ||
                        skin->height * zoom_factor / 100 >= height) &&
                       zoom_factor > ZOOM_MIN_FACTOR)
                    zoom_factor -= ZOOM_STEP;
                skin_handle_zooming();
            }
#endif
        }
    }
    SkinArea area = { 0, 0, ds_get_width(skin->ds), ds_get_height(skin->ds) };
    skin_draw_skin(&area);
    // Likely we got a new buffer, update the emulated display buffer
    if (skin->es && skin->es->ds) {
        skin->es->ds->surface = qemu_resize_displaysurface(skin->es->ds,
                                                           skin->es->width, skin->es->height);
    }
    // Redraw the emulated screen on top
    vga_hw_invalidate();
    vga_hw_update();

    dpy_update(skin->ds, 0, 0, ds_get_width(skin->ds), ds_get_height(skin->ds));
}

static void skin_update(DisplayState *ds, int x, int y, int w, int h)
{
    // Updates done by emulated screen need to be converted to our display
    int xd = x, yd = y, wd = w, hd = h;
    if (skin->rotation == off) {
        // Check if the drawing will fit the screen
        if (ds_get_width(skin->ds) >= (w + (x + skin->es->posx)) &&
            ds_get_height(skin->ds) >= (h + (y + skin->es->posy)) ) {
            xd += skin->es->posx;
            yd += skin->es->posy;
        }
    }
    else {
        skin_rotate_buffer(skin, ds, x, y, w, h);
        xd = skin->es->posx + skin->es->height - (y + h - 1);
        yd = skin->es->posy + x;
        wd = h;
        hd = w;
    }
    // Check if we have an overlapping tooltip
    if (skin->tooltip.image) {
        struct SkinArea clip = { xd, yd, wd, hd };
        if (skin_overlaps(skin->tooltip.image, &clip)) {
            SkinArea drawclip = skin_cliparea(skin->tooltip.image, &clip);
            skin_draw_tooltip(&drawclip);
        }
    }
    // Update the correct part
    dpy_update(skin->ds, xd, yd, wd, hd);
}

static void skin_setdata(DisplayState *ds)
{
    //printf("skin_setdata()\n");
    //if (skin->es && skin->es->ds)
    //  dpy_setdata(skin->es->ds);
    if (!es_ds) es_ds = ds;
    dpy_setdata(skin->ds);
}

static void skin_resize(DisplayState *ds)
{
    // Emulated display has resized
    //printf("skin_resize()\n");
    // Resize our display also then...
    //qemu_console_resize(skin->ds, skin->width, skin->height);
    skin_setdata(ds);
}

static void skin_refresh(DisplayState *ds)
{
    //printf("skin_refresh()\n");
}

static DisplaySurface* skin_create_displaysurface(int width, int height)
{
    //printf(">> skin_create_displaysurface(%d,%d)\n", width, height);
    // We need a host surface to do this
    if (!skin) return NULL;
    DisplaySurface *surface =
        (DisplaySurface*)qemu_mallocz(sizeof(DisplaySurface));
    surface->width = width;
    surface->height = height;
    surface->linesize = ds_get_linesize(skin->ds);
    surface->pf = skin->ds->surface->pf;
    if (skin->rotation == off) {
        // No rotation, draw directly to display buffer
        surface->data = ds_get_data(skin->ds) +
            skin->es->posy * ds_get_linesize(skin->ds) +
            skin->es->posx * ds_get_bytes_per_pixel(skin->ds);
    }
    else {
        surface->linesize = ds_get_bytes_per_pixel(skin->ds) * surface->width;
        // Rotation is done in skinning source with skin_rotate_buffer
        surface->data = qemu_malloc(height * surface->linesize);
        // Mark data is allocated here
        surface->flags |= QEMU_ALLOCATED_FLAG;
    }
    return surface;
}

static void skin_free_displaysurface(DisplaySurface *surface)
{
    //printf("skin_free_displaysurface\n");
    if (surface == NULL)
        return;

    if (surface->flags & QEMU_ALLOCATED_FLAG)
        qemu_free(surface->data);
    qemu_free(surface);
}

static DisplaySurface* skin_resize_displaysurface(DisplaySurface *surface, int width, int height)
{
    //printf("skin_resize_displaysurface, width=%d, height=%d\n", width, height);
    skin_free_displaysurface(surface);
    return skin_create_displaysurface(width, height);
}

int is_skin_available(void);
int is_skin_available(void)
{
    return (skin != NULL);
}

static void skin_handle_zooming(void)
{
    //printf("skin_handle_zooming, zoom_factor=%d%%\n", zoom_factor);
    
    if (skin->keyboard.button &&
        (skin->keyboard.button->key.state == ESkinBtn_Active ||
         skin->keyboard.button->key.state == ESkinBtn_ActiveHighlighted)) {
        dpy_enablezoom(skin->ds, skin->keyboard.screenwidth * zoom_factor / 100,
                       skin->keyboard.screenheight * zoom_factor / 100);
    } 
    else {
        // No keyboard needs to be drawn
        dpy_enablezoom(skin->ds, skin->width * zoom_factor / 100, 
                       skin->height * zoom_factor / 100);            
    }
}

static void skin_show_zooming_level(int zoom_level, int posx)
{
    // Create the correct text for the zooming level
    SkinTooltip* tooltip = &skin->tooltip;
    char text[20];
    sprintf(text, "Zooming to %d%%", zoom_level);
    SkinImage *image = skin_image_createtext(skin, text);
    if (image) {
        // Everything set, store the image and have it drawn
        tooltip->image = image;
        image->posx = posx - image->width - ZOOM_INDICATOR_POS;
        image->posy = ZOOM_INDICATOR_POS;
        struct SkinArea clip = { image->posx, image->posy,
                                 image->width, image->height };
        // Make a "dummy" timer
        if (!skin->tooltip.timer) {
            skin->tooltip.timer = qemu_new_timer_ns(rt_clock,
                                                 skin_cleartooltip,
                                                 NULL);
        }
        // Prevent zoom button's own tooltip appearing. Set long expiration time.
        qemu_mod_timer(skin->tooltip.timer, qemu_get_clock_ns(rt_clock) + 600000000); // 1000 min
        // Draw the zooming level indicator
        skin_draw_tooltip(&clip);
        dpy_update(skin->ds, image->posx, image->posy,
                   image->width, image->height);
    }
}

static void skin_key_handler(void *opaque, int keycode)
{
    // Check if someone triggered one of our buttons
    int keyvalue = keycode & 0x7F;
    int released = keycode & 0x80;
    SkinButton* button = skin->buttons;
    while (button) {
        if ((button->key.keycode & 0x7F) == keyvalue) {
            int state = skin_button_handle_key(&button->key, !released);
            if (skin_draw_button(skin, button, state)) {
                dpy_update(skin->ds, button->image.posx, button->image.posy,
                           button->image.width, button->image.height);
            }
            // Check if the keyboard needs to be udpated
            if (button == skin->keyboard.button && released) {
                if (skin->keyboard.animated) {
                    keyboard_animation_phase = 0;
                    skin_update_keyboard(NULL);
                }
                else {
                    // Changing the size, automatically triggers a redraw
                    if (button->key.state == ESkinBtn_Active ||
                        button->key.state == ESkinBtn_ActiveHighlighted) {
                        original_qemu_console_resize(skin->ds, skin->keyboard.screenwidth,
                                                     skin->keyboard.screenheight);
                        SkinArea area = { skin->keyboard.image->posx,
                                          skin->keyboard.image->posy,
                                          skin->keyboard.image->width,
                                          skin->keyboard.image->height };
                        skin_handle_zooming();
                        skin_draw_image(skin,
                                        skin->keyboard.image,
                                        &area);
                    }
                    else {
                        original_qemu_console_resize(skin->ds, skin->width, skin->height);
                        skin_handle_zooming();
                    }
                }
            }
            // Check if rotation button is pressed (test-code)
            if (button->key.keycode == 67 && released) {
                if (skin->rotation_req == on) skin->rotation_req = off;
                else skin->rotation_req = on;
            }
            
            // Zoom buttons
            if (button->key.keycode == 87 && released) { // F11
                // Zooming in, zoom_factor in %
                if (zoom_factor < ZOOM_MAX_FACTOR) {
                    zoom_factor += ZOOM_STEP;
                    no_keyboard_anim = 1;
                    skin_handle_zooming();
                    skin_show_zooming_level(zoom_factor, button->image.posx);
                }
            }
            if (button->key.keycode == 88 && released) { // F12
                // Zooming out, zoom_factor in %
                if (zoom_factor > ZOOM_MIN_FACTOR) {
                    zoom_factor -= ZOOM_STEP;
                    no_keyboard_anim = 1;
                    skin_handle_zooming();
                    skin_show_zooming_level(zoom_factor, button->image.posx);
                }
            }
        }
        button = button->next;
    }
    if (skin->keyboard.button &&
        (skin->keyboard.button->key.state == ESkinBtn_Active ||
         skin->keyboard.button->key.state == ESkinBtn_ActiveHighlighted)) {
        SkinKey* key = skin->keyboard.keys;
        while (key) {
            if ((key->keycode & 0x7F) == keyvalue) {
                int state = skin_button_handle_key(key, !released);
                if (skin_highlight_key(skin, key, state)) {
                    dpy_update(skin->ds, key->posx, key->posy,
                               key->width, key->height);
                }
            }
            key = key->next;
        }
    }

    if (skin->mouse_event == off) skin_handle_rotation();
}

DisplayState *graphic_console_init(vga_hw_update_ptr update,
                                   vga_hw_invalidate_ptr invalidate,
                                   vga_hw_screen_dump_ptr screen_dump,
                                   vga_hw_text_update_ptr text_update,
                                   void *opaque)
{
    DisplayState *ds;
    //printf(">> graphic_console_init\n");
    // Host display for skin, register it to the qemu system
    ds = qemu_graphic_console_init(update, invalidate, screen_dump, text_update, opaque);
    
    // If skinning is enabled, we create a host display, otherwise return ds
    if (is_skin_available() && ds) {
        // We need to know when we are resized
        DisplayChangeListener *dcl;
        dcl = qemu_mallocz(sizeof(DisplayChangeListener));
        dcl->dpy_update = skin_host_update;
        dcl->dpy_setdata = skin_host_setdata;
        dcl->dpy_resize = skin_host_resize;
        dcl->dpy_refresh = NULL;
        register_displaychangelistener(ds, dcl);
        // Store the DisplayState to the SkinScreen information
        skin->ds = ds;
        // Resize the display to our own size
        if (skin->keyboard.button &&
            (skin->keyboard.button->key.defaultstate == on ||
             skin->keyboard.button->key.defaultstate == undefined)) {
            original_qemu_console_resize(skin->ds, skin->keyboard.screenwidth,
                                         skin->keyboard.screenheight);
        } else {
            //printf("skin->ds=%x, skin->width=%d, skin->height=%d\n", skin->ds, skin->width, skin->height);
            original_qemu_console_resize(skin->ds, skin->width, skin->height);
        }
                
        // Create a new DisplayState, this is the emulated display, we keep it to ourselves
        ds = (DisplayState *) qemu_mallocz(sizeof(DisplayState));
        // Also we want control over the emulated display ourselves
        DisplayAllocator *da = qemu_mallocz(sizeof(DisplayAllocator));
        da->create_displaysurface = skin_create_displaysurface;
        da->resize_displaysurface = skin_resize_displaysurface;
        da->free_displaysurface = skin_free_displaysurface;
        ds->allocator = da;
        //printf("%s, es->width=%d, es->height=%d\n", __FUNCTION__, skin->es->width, skin->es->height);
        ds->surface = skin_create_displaysurface(skin->es->width, skin->es->height);
        // Client DisplayState, which is the actual screen we are emulating
        skin->es->ds = ds;

        //Reserve es posx & posy for SDL blit
        es_posx = skin->es->posx;
        es_posy = skin->es->posy;        
        // We want all events on our emulated screen
        DisplayChangeListener *skindcl = qemu_mallocz(sizeof(DisplayChangeListener));
        skindcl->dpy_update = skin_update;
        skindcl->dpy_setdata = skin_setdata;
        skindcl->dpy_resize = skin_resize;
        skindcl->dpy_refresh = skin_refresh;
        register_displaychangelistener(ds, skindcl);

        // We are also interested in keyboard events to match possible switches
        qemu_add_kbd_event_handler(skin_key_handler, NULL);
        //printf("graphic_console_init >>\n");
    }
    return ds;
}

void skin_toggle_full_screen(DisplayState *ds)
{
    skin_update_keyboard(NULL);
    skin_handle_zooming();
}

void qemu_console_resize(DisplayState *ds, int width, int height)
{
    //printf(">> skinning: qemu_console_resize, width=%d, height=%d\n", width, height);
    if (is_skin_available() && skin->ds && skin->es && skin->es->ds == ds) {
        int neww = skin->width;
        int newh = skin->height;
        if (skin->keyboard.button &&
            (skin->keyboard.button->key.state == ESkinBtn_Active ||
             skin->keyboard.button->key.state == ESkinBtn_ActiveHighlighted)) {
            neww = skin->keyboard.screenwidth;
            newh = skin->keyboard.screenheight;
        }
        // Record the required width / height
        skin->es->width = width;
        skin->es->height = height;
        if (ds_get_width(skin->ds) == neww &&
            ds_get_height(skin->ds) == newh ) {
            // Just change the emulated screen size
            skin->es->ds->surface = qemu_resize_displaysurface(skin->es->ds,
                                                               skin->es->width, skin->es->height);
        }
        else {
            original_qemu_console_resize(skin->ds, neww, newh);
            skin_handle_zooming();
        }
    }
    else
        original_qemu_console_resize(ds, width, height);
    //printf("skinning: qemu_console_resize >>\n");
}


QEMUPutMouseEntry *original_qemu_add_mouse_event_handler(QEMUPutMouseEvent *func,
                                                         void *opaque, int absolute,
                                                         const char *name);

QEMUPutMouseEntry *qemu_add_mouse_event_handler(QEMUPutMouseEvent *func,
                                                void *opaque, int absolute,
                                                const char *name)
{
    if (skin) {
        // If a skin is applied, keep reference to the calling one
        // but don't register that to qemu
        QEMUPutMouseEntry *other = qemu_mallocz(sizeof(*other));
        other->qemu_put_mouse_event = func;
        other->qemu_put_mouse_event_opaque = opaque;
        other->qemu_put_mouse_event_absolute = absolute;
        other->qemu_put_mouse_event_name = qemu_strdup(name);
        original_qemu_add_mouse_event_handler(skin_mouse_event, other, 1, "Skin mouse handling");
        return other;
    }
    return original_qemu_add_mouse_event_handler(func, opaque, absolute, name);
}
