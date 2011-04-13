/*
 * Qemu skinning header
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

#ifndef SKIN_H__
#define SKIN_H__

typedef enum SwitchDefaultState {
    undefined = -1,
    off = 0,
    on
} SwitchDefaultState;

typedef struct SkinImageConfig {
    char* file;
    int posx;
    int posy;
    struct SkinImageConfig* next;
} SkinImageConfig;

typedef struct SkinBackgroundColor {
    int red;
    int green;
    int blue;
} SkinBackgroundColor;

typedef struct SkinButtonConfig {
    SkinImageConfig image;
    unsigned int keycode;
    unsigned int isswitch;
    SwitchDefaultState defaultstate;
    char* tooltip;
    struct SkinButtonConfig* next;
} SkinButtonConfig;

typedef struct SkinKeyConfig {
    int posx;
    int posy;
    int width;
    int height;
    unsigned int keycode;
    struct SkinKeyConfig* next;
} SkinKeyConfig;

typedef struct SkinKeyboardConfig {
	SkinImageConfig* image;
	int animated;
	SkinKeyConfig* keys;
	int switchcode;
    int screenwidth;
    int screenheight;
    int highlight_red;
    int highlight_green;
    int highlight_blue;
    int offset;             // Offset to move the skin
} SkinKeyboardConfig;

typedef struct SkinLayout {
    int width;
    int height;
    int emuscreen_posx;
    int emuscreen_posy;
    int emuscreen_width;
    int emuscreen_height;
    struct SkinBackgroundColor bgcolor;
    struct SkinImageConfig* background;
    struct SkinKeyboardConfig* keyboard;
    struct SkinButtonConfig* buttons;
} SkinLayout;

typedef struct SkinConfig {
    struct SkinLayout* landscape;
    struct SkinLayout* portrait;
} SkinConfig;

typedef struct SkinImage {
    uint16_t posx;              // X Position on the skin display
    uint16_t posy;              // Y Position on the skin display
    uint16_t width;             // Width of the image
    uint16_t height;            // Height of the image
    void* data;                 // Image data, encoded in PixelFormat
    int linesize;               // Bytes per line
    struct PixelFormat pf;      // Color/Bit information
} SkinImage;

typedef enum SkinButtonState {
    ESkinBtn_Idle = 0,
    ESkinBtn_Highlighted,
    ESkinBtn_Active,
    ESkinBtn_ActiveHighlighted,
    // Button state count
    ESkinBtn_statecount,
    ESkinBtn_forceredraw
} SkinButtonState;

typedef struct SkinKeyEvent {
    uint8_t mouseover : 1;          // Mouse is hovered over the button
    uint8_t mousedown : 1;          // Mouse LSK is clicked on the button currently
    uint8_t keydown   : 1;          // The related key is pressed currently
} SkinKeyEvent;

typedef struct SkinKey {        // Has same structure as SkinButton with
    uint8_t keycode;                // posx - height from SkinImage
    SkinKeyEvent event;
    uint8_t isswitch : 1;
    int8_t defaultstate : 3;
    uint8_t state : 4;
    uint16_t posx;
    uint16_t posy;
    uint16_t width;
    uint16_t height;
    struct SkinKey* next;
} SkinKey;

typedef struct SkinButton {     // SkinButton and SkinKey share information
    struct SkinImage image;
	struct SkinKey key;
    char* tooltip;
	struct SkinButton* next;
} SkinButton;

typedef struct EmulatedScreen {
    struct DisplayState* ds;    // DisplayState used by client display drivers
    int posx;                   // X Position on the skin display
    int posy;                   // Y Position on the skin display
    int width;                  // Required width of this display
    int height;                 // Required height of this display
} EmulatedScreen;

typedef struct SkinKeyboard {
    SkinImage* image;
    int animated;
    SkinKey* keys;
    SkinButton* button;
    int screenwidth;
    int screenheight;
    int highlight_red;
    int highlight_green;
    int highlight_blue;
    int offset;
} SkinKeyboard;

typedef struct SkinFont {
    struct SkinImage* image;    // Font image with characters
    uint8_t char_width;         // Character Width in px
    uint8_t char_height;        // Character Height in px
} SkinFont;

typedef struct SkinTooltip {
    uint32_t color;     // color for the tooltip background
    QEMUTimer *timer;   // Timeout before the tooltip is shown
    SkinButton *button; // Button that activated the tooltip
    SkinImage *image;   // Cached image for redrawing
} SkinTooltip;

typedef struct SkinScreen {
    int width;                  // Total width of the display
    int height;                 // Total height of the display
    struct SkinBackgroundColor bgcolor;
    struct DisplayState* ds;    // DisplayState towards qemu
    struct EmulatedScreen* es;  // Emulated display information
    struct SkinImage* background;
    struct SkinKeyboard keyboard;
    struct SkinButton* buttons;
    struct SkinFont* font;
    struct SkinTooltip tooltip;
    int rotation;
    int rotation_req;
    int mouse_event;
    struct SkinConfig* config;  // Possible skin configurations to use
} SkinScreen;

typedef struct SkinArea {
    int x;
    int y;
    int width;
    int height;
} SkinArea;

#endif /* SKIN_H__ */

