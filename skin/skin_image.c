/*
 * Skin image drawing
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
#include "qemu-common.h"
#ifndef CONFIG_COCOA
#define PNG_SKIP_SETJMP_CHECK
#include <png.h>
#endif

#include "console.h"

#include "skin_image.h"

#define PIXEL_DST 2
#include "skin_image_template.h"
#undef PIXEL_DST
#define PIXEL_DST 4
#include "skin_image_template.h"
#undef PIXEL_DST

// Font defines, ascii table font from ' ' to '~' characters supported
#define CHAR_BASE ' '   // base number (first character in table)
#define CHAR_LAST '~'   // character in the font picture

#define TOOLTIP_SPAC_W 3 // Spacing of the tooltip on each side
#define TOOLTIP_SPAC_H 1 // Spacing of the tooltip on each side

static void skin_merge_image( SkinImage *dst, SkinImage *src,
                              int xd, int yd,                // Destination
                              int xs, int ys, int w, int h)  // Source
{
    int line;
    // This will fit, now copy line by line
    uint32_t r, g, b, a, px;
    uint32_t *srcpx, *dstpx;
    for( line = 0; line < h; line++) {
        dstpx = (uint32_t*)(dst->data +         // base destination
            ((yd + line) * dst->linesize) +     // pixel row
            (xd * dst->pf.bytes_per_pixel));    // pixel column start
        srcpx = (uint32_t*)(src->data +         // base source
            src->linesize * (ys + line) +       // pixel row
            xs * dst->pf.bytes_per_pixel);      // pixel column start
        for (px = 0; px < w; px++) {
            // Extract color values and alpha channel
            r = ((srcpx[px]) & src->pf.rmask) >> (src->pf.rshift);
            g = ((srcpx[px]) & src->pf.gmask) >> (src->pf.gshift);
            b = ((srcpx[px]) & src->pf.bmask) >> (src->pf.bshift);
            a = ((srcpx[px]) & src->pf.amask) >> (src->pf.ashift);

            // Apply alpha blend factor
            r = r * a / src->pf.amax;
            g = g * a / src->pf.gmax;
            b = b * a / src->pf.bmax;
            // Convert to destination depth
            r = r >> (8 - dst->pf.rbits);
            g = g >> (8 - dst->pf.gbits);
            b = b >> (8 - dst->pf.bbits);
            // Merge the colors
            r |= ((dstpx[px] & dst->pf.rmask) >> dst->pf.rshift) *
                    (dst->pf.rmax - (a >> (8 - dst->pf.rbits))) / dst->pf.rmax;
            g |= ((dstpx[px] & dst->pf.gmask) >> dst->pf.gshift) *
                    (dst->pf.gmax - (a >> (8 - dst->pf.gbits))) / dst->pf.gmax;
            b |= ((dstpx[px] & dst->pf.bmask) >> dst->pf.bshift) *
                    (dst->pf.bmax - (a >> (8 - dst->pf.bbits))) / dst->pf.bmax;
            a = a >> (8 - dst->pf.abits);
            a = a + ((dstpx[px] & dst->pf.amask) >> dst->pf.ashift) * 
                    (dst->pf.amax - (a >> (8 - dst->pf.abits))) / dst->pf.amax;

            // Put the color as destination pixel
            dstpx[px] = ((r << dst->pf.rshift) & dst->pf.rmask) |
                        ((g << dst->pf.gshift) & dst->pf.gmask) |
                        ((b << dst->pf.bshift) & dst->pf.bmask) |
                        ((a << dst->pf.ashift) & dst->pf.amask);

        }
    }
}

SkinImage* skin_image_createtext(SkinScreen* skin, char* text)
{
    if (skin->font && skin->font->char_width != 0) {
        int cur, line;
        int len = strlen(text);
        if (len <= 0) return NULL;
        struct SkinImage* image = (SkinImage*)qemu_malloc(sizeof(SkinImage));
        if (image) {
            memcpy(image, skin->font->image, sizeof(SkinImage));
            // Create width with spacing on each side
            image->width = skin->font->char_width * len + TOOLTIP_SPAC_W * 2;
            image->height += TOOLTIP_SPAC_H * 2;
            image->linesize = image->width * 4; // bytes/pixel
            image->data = qemu_malloc(image->width * image->height * 4);
            uint32_t *cleardst;
            // Full background is always black (gives us a border)
            for (line = 0; line < image->height; line++)
                for (cur = 0; cur < image->width; cur++) {
                    cleardst = (uint32_t*)(image->data +
                                   (line * image->linesize) + (cur * 4));
                    *cleardst = 0xFF000000;	// Black
                }
            // Next fill the background color if given (defaults to transparent)
            for (line = 1; line < image->height-1; line++)
                for (cur = 1; cur < image->width-1; cur++) {
                    cleardst = (uint32_t*)(image->data +
                                    (line * image->linesize) + (cur * 4));
                    *cleardst = skin->tooltip.color;
                }
            
            // Copy the characters in place, one at a time
            for (cur = 0; cur < len; cur++) {
                skin_merge_image(image, skin->font->image,
                                 cur * skin->font->char_width + TOOLTIP_SPAC_W, TOOLTIP_SPAC_H,
                                 (text[cur]-CHAR_BASE) * skin->font->char_width, 0,
                                 skin->font->char_width, skin->font->char_height);
            }
        }
        return image;
    }
    return NULL;
}

void skin_rotate_buffer(SkinScreen* skin, DisplayState* ds, int x, int y, int w, int h)
{
    switch (ds_get_bytes_per_pixel(ds)) {
        case 2:
            skin_rotate_buffer_bytes_2(skin, ds, x, y, w, h);
            break;
        case 4:
            skin_rotate_buffer_bytes_4(skin, ds, x, y, w, h);
            break;
        default:
            printf("Wrong destination buffer\n");
            break;
    }
}

void skin_fill_color( SkinScreen* skin, SkinArea* area, int r, int g, int b)
{
    uint32_t color = 0;
    PixelFormat* pf = &skin->ds->surface->pf;
    color = (((r << (pf->rshift)) >> (8 - pf->rbits)) & pf->rmask) |
            (((g << (pf->gshift)) >> (8 - pf->gbits)) & pf->gmask) |
            (((b << (pf->bshift)) >> (8 - pf->bbits)) & pf->bmask);
    vga_fill_rect(skin->ds, area->x, area->y, area->width, area->height, color);
}

void skin_fill_background(SkinScreen* skin, SkinArea* area)
{
    int r = skin->bgcolor.red;
    int g = skin->bgcolor.green;
    int b = skin->bgcolor.blue;
    skin_fill_color(skin, area, r, g, b);
}

int skin_draw_button(SkinScreen* skin, SkinButton* button, int state)
{
    //printf("skin_draw_button( state: %d ) %d\n", state, button->key.state);
    if(state == button->key.state) return 0;
    if(state != ESkinBtn_forceredraw) button->key.state = state;
    struct SkinImage image;
    memcpy(&image, &button->image, sizeof(SkinImage));
    // Correct the data pointer
    image.data += (image.linesize * image.height * button->key.state);
    struct SkinArea clip = { image.posx, image.posy, image.width, image.height };
    skin_fill_background(skin, &clip);
    if (skin->background != NULL) {
        (void)skin_draw_image(skin, skin->background, &clip);
    }
    return skin_draw_image(skin, &image, &clip);
}

int skin_draw_animated_keyboard(SkinScreen* skin, SkinImage* image, int phase)
{
    //printf("skin_draw_animated_keyboard, phase=%d\n", phase);
    struct SkinImage keyboardpart;
    memcpy(&keyboardpart, image, sizeof(SkinImage));
    // Correct the data pointer to draw only part of the picture
    keyboardpart.data += (keyboardpart.linesize * keyboardpart.height * phase);
    struct SkinArea clip = { keyboardpart.posx, keyboardpart.posy, 
                             keyboardpart.width, keyboardpart.height };
    if (skin->background != NULL) {
        (void)skin_draw_image(skin, skin->background, &clip);
    }
    return skin_draw_image(skin, &keyboardpart, &clip);
    
}

int skin_highlight_key(SkinScreen* skin, SkinKey* key, int state)
{
    if (state == key->state) return 0;
    key->state = state;
    // Do we draw the actual key image or do we highlight that area
    if (state != ESkinBtn_Idle) {
        uint8_t *d, *d1;
        int x, y, bpp, r, g, b, a, rd, gd, bd;
        bpp = (ds_get_bits_per_pixel(skin->ds) + 7) >> 3;
        d1 = ds_get_data(skin->ds) +
            ds_get_linesize(skin->ds) * key->posy + bpp * key->posx;

        PixelFormat* dpf = &skin->ds->surface->pf;
        // Apply alpha blend factor
        a = 128;
        r = skin->keyboard.highlight_red * a / 255;
        g = skin->keyboard.highlight_green * a / 255;
        b = skin->keyboard.highlight_blue * a / 255;
        // Convert to destination depth
        r = r >> (8 - dpf->rbits);
        g = g >> (8 - dpf->gbits);
        b = b >> (8 - dpf->bbits);
        // Apply color overlay
        for (y = 0; y < key->height; y++) {
            d = d1;
            switch(bpp) {
            case 1:
                break;
            case 2:
                for (x = 0; x < key->width; x++) {
                    // Merge the colors
                    rd = r | (((*((uint16_t *)d)) & dpf->rmask) >> dpf->rshift) *
                                (dpf->rmax - (a >> (8 - dpf->rbits))) / dpf->rmax;
                    gd = g | ((*((uint16_t *)d) & dpf->gmask) >> dpf->gshift) *
                                (dpf->gmax - (a >> (8 - dpf->gbits))) / dpf->gmax;
                    bd = b | ((*((uint16_t *)d) & dpf->bmask) >> dpf->bshift) *
                                (dpf->bmax - (a >> (8 - dpf->bbits))) / dpf->bmax;
                    // Put the color as destination pixel
                    *((uint16_t *)d) = ((rd << dpf->rshift) & dpf->rmask) |
                        ((gd << dpf->gshift) & dpf->gmask) | ((bd << dpf->bshift) & dpf->bmask);
                    d += 2;
                }
                break;
            case 4:
                for (x = 0; x < key->width; x++) {
                    // Merge the colors
                    rd = r | ((*((uint32_t *)d) & dpf->rmask) >> dpf->rshift) *
                            (dpf->rmax - (a >> (8 - dpf->rbits))) / dpf->rmax;
                    gd = g | ((*((uint32_t *)d) & dpf->gmask) >> dpf->gshift) *
                            (dpf->gmax - (a >> (8 - dpf->gbits))) / dpf->gmax;
                    bd = b | ((*((uint32_t *)d) & dpf->bmask) >> dpf->bshift) *
                            (dpf->bmax - (a >> (8 - dpf->bbits))) / dpf->bmax;
                    // Put the color as destination pixel
                    *((uint32_t *)d) = ((rd << dpf->rshift) & dpf->rmask) |
                        ((gd << dpf->gshift) & dpf->gmask) | ((bd << dpf->bshift) & dpf->bmask);
                    d += 4;
                }
                break;
            }
            d1 += ds_get_linesize(skin->ds);
        }
    }
    else {
        SkinArea area = { key->posx, key->posy, key->width, key->height };
        skin_fill_background(skin, &area);
        skin_draw_image( skin, skin->keyboard.image, &area);
    }
    return 1;
}

SkinArea skin_cliparea(SkinImage* image, SkinArea* area)
{
    SkinArea clip;
    clip.x = area->x;
    clip.y = area->y;
    clip.width = area->width;
    clip.height = area->height;

    int imagex = image->posx + image->width;
    int imagey = image->posy + image->height;
    int areax = area->x + area->width;
    int areay = area->y + area->height;

    if (image->posx > area->x) {
        clip.x = image->posx;
        clip.width -= image->posx - area->x;
    }
    if (image->posy > area->y) {
        clip.y = image->posy;
        clip.height -= image->posy - area->y;
    }
    if (areax > imagex) clip.width = imagex - clip.x;
    if (areay > imagey) clip.height = imagey - clip.y;
//    printf("%d, %d, %d, %d\n", clip.x, clip.y, clip.width, clip.height);
    return clip;
}

int skin_draw_image(SkinScreen* skin, SkinImage* image, SkinArea* area)
{
    //printf("skin_draw_image(area: %d, %d, %d %d)\nimage: %d, %d, %d, %d\nimage->data: 0x%X\n",
    //        area->x, area->y, area->width, area->height,
    //        image->posx, image->posy, image->width, image->height), (unsigned int)image->data);
    // Get the clipping area we need to update
    SkinArea clip = skin_cliparea(image, area);
    // Determine the correct destination buffer
    switch (ds_get_bytes_per_pixel(skin->ds)) {
        case 2:
            return skin_draw_image_from_a24_to_2(skin, image, &clip);
        case 4:
            return skin_draw_image_from_a24_to_4(skin, image, &clip);
        default:
            printf("Wrong destination buffer\n");
            return 0;
    }
}

// Load / Read png files
#ifndef CONFIG_COCOA
void *skin_loadpng(const char *fn, unsigned *_width, unsigned *_height)
{
    FILE *fp = 0;
    unsigned char header[8];
    unsigned char *data = 0;
    unsigned char **rowptrs = 0;
    png_structp p = 0;
    png_infop pi = 0;
    
    png_uint_32 width, height;
    int bitdepth, colortype, imethod, cmethod, fmethod, i;

    p = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
    if(p == 0) {
        printf("%s: failed to allocate png read struct\n", fn);
        return 0;
    }

    pi = png_create_info_struct(p);
    if(pi == 0) {
        printf("%s: failed to allocate png info struct\n", fn);
        goto oops;
    }
        
    fp = fopen(fn, "rb");
    if(fp == 0) {
        printf("%s: failed to open file\n", fn);
        return 0;
    }

    if(fread(header, 8, 1, fp) != 1) {
        printf("%s: failed to read header\n", fn);
        goto oops;
    }
    
    if(png_sig_cmp(header, 0, 8)) {
        printf("%s: header is not a PNG header\n", fn);
        goto oops;
    }

    if(setjmp(png_jmpbuf(p))) {
        printf("%s: png library error\n", fn);
    oops:
        png_destroy_read_struct(&p, &pi, 0);
        if(fp != 0) fclose(fp);
        if(data != 0) free(data);
        if(rowptrs != 0) free(rowptrs);
        return 0;
    }

    png_init_io(p, fp);
    png_set_sig_bytes(p, 8);

    png_read_info(p, pi);

    png_get_IHDR(p, pi, &width, &height, &bitdepth, &colortype,
                 &imethod, &cmethod, &fmethod);
//    printf("PNG: %d x %d (d=%d, c=%d)\n",
//           width, height, bitdepth, colortype);

    switch(colortype){
    case PNG_COLOR_TYPE_PALETTE:
        png_set_palette_to_rgb(p);
        break;

    case PNG_COLOR_TYPE_RGB:
        if(png_get_valid(p, pi, PNG_INFO_tRNS)) {
            png_set_tRNS_to_alpha(p);
        } else {
            png_set_filler(p, 0xFF, PNG_FILLER_AFTER);
        }
        break;
        
    case PNG_COLOR_TYPE_RGB_ALPHA:
        break;

    case PNG_COLOR_TYPE_GRAY:
        printf("\n");
        if(bitdepth < 8) {
            png_set_gray_1_2_4_to_8(p);
        }
        
    default:
        printf("%d: unsupported (grayscale?) color type\n", colortype);
        goto oops;
    }

    png_set_bgr(p);

    if(bitdepth == 16) {
        png_set_strip_16(p);
    }

    data = (unsigned char*) malloc((width * 4) * height);
    rowptrs = (unsigned char **) malloc(sizeof(unsigned char*) * height);

    if((data == 0) || (rowptrs == 0)){
        printf("could not allocate data buffer\n");
        goto oops;
    }

    for(i = 0; i < height; i++) {
        rowptrs[i] = data + ((width * 4) * i);
    }
    
    png_read_image(p, rowptrs);
    
    png_destroy_read_struct(&p, &pi, 0);
    fclose(fp);
    if(rowptrs != 0) free(rowptrs);

    *_width = width;
    *_height = height;
    
    return (void*) data;   
}
#endif

static void skin_png_defaultpixelformat(SkinImage* image)
{
    image->pf.bits_per_pixel = 32;
    image->pf.bytes_per_pixel = 4;
    image->pf.depth = 32;
    image->pf.rmask = 0x00FF0000;
    image->pf.gmask = 0x0000FF00;
    image->pf.bmask = 0x000000FF;
    image->pf.amask = 0xFF000000;
    image->pf.rshift = 16;
    image->pf.gshift = 8;
    image->pf.bshift = 0;
    image->pf.ashift = 24;
    image->pf.rmax = 255;
    image->pf.gmax = 255;
    image->pf.bmax = 255;
    image->pf.amax = 255;
    image->pf.rbits = 8;
    image->pf.gbits = 8;
    image->pf.bbits = 8;
    image->pf.abits = 8;
    image->linesize = image->pf.bytes_per_pixel * image->width;
}

int skin_load_image_data(SkinImage* image, char* file)
{
    unsigned width, height;
    image->data = skin_loadpng(file, &width, &height);
    if (image->data == NULL) {
        fprintf(stderr, "failed to load image '%s'\n", file );
        return 1;
    }
    image->width = (int)width;
    image->height = (int)height;
    skin_png_defaultpixelformat(image);

//    printf("image loaded '%s', width=%d, height=%d\n", file, width, height);
    return 0;
}

SkinImage* skin_load_image(char* file)
{
    SkinImage* image = (SkinImage*)qemu_mallocz(sizeof(SkinImage));
    if(!image) return NULL;
    if(skin_load_image_data(image, file)) {
        qemu_free(image);
         return NULL;
    }
    return image;
}

