/*
 * Skin image conversion template header
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

#if PIXEL_DST == 2
    #define DST_TYPE uint16_t
#endif
#if PIXEL_DST == 4
    #define DST_TYPE uint32_t
#endif

/* Function declaration depending on the bytes per pixel
   either 2 bytes or 4 bytes types are used */
static inline void glue(skin_rotate_buffer_bytes_,PIXEL_DST)
    (SkinScreen* skin, DisplayState* ds, int x, int y, int w, int h)
{
    DST_TYPE *srcpixel, *dstpixel;
    int px, py;
    // Loop through all the pixels to rotate them 90 degr clockwise
    for (py = 0; py < h; py++) {
        for (px = 0; px < w; px++) {
            // Source pixel (landscape orientation)
            srcpixel = (DST_TYPE*)(ds_get_data(ds) +
                            ((y + py) * ds_get_linesize(ds)) +
                            ((x + px) * ds_get_bytes_per_pixel(ds)));
            // Destination pixel (portrait orientation)
            dstpixel = (DST_TYPE*)(ds_get_data(skin->ds) + 
                            (skin->es->posy + x + px) * ds_get_linesize(skin->ds) +
                            (skin->es->posx + skin->es->height - (y+py)) *
                                ds_get_bytes_per_pixel(skin->ds));
            // Copy the pixel data
            *dstpixel = *srcpixel;
        }
    }
}

static inline int glue(skin_draw_image_from_a24_to_,PIXEL_DST)
    (SkinScreen* skin, SkinImage* image, SkinArea* clip)
{
    int line;
    // Calculate source row and column start
    int colst = (clip->x - image->posx) * image->pf.bytes_per_pixel;
    int rowst = (clip->y - image->posy);
    // Determine number of bytes to copy
    if (image && skin->ds &&
        ds_get_width(skin->ds) >= clip->x + clip->width &&
        ds_get_height(skin->ds) >= clip->y + clip->height ) {
        // This will fit, now copy line by line
        uint32_t r, g, b, a, px;
        uint32_t* srcpx;
        DST_TYPE* dstpx;
        PixelFormat* dpf = &skin->ds->surface->pf;
        for( line = 0; line < clip->height; line++) {
            dstpx = (DST_TYPE*)(ds_get_data(skin->ds) +          // base destination
                ((clip->y + line) * ds_get_linesize(skin->ds)) + // pixel row
                (clip->x * ds_get_bytes_per_pixel(skin->ds)));   // pixel column start
            srcpx = (uint32_t*)(image->data +      // base source
                image->linesize * (rowst + line) + // pixel row
                colst);                            // pixel column start
            for (px = 0; px < clip->width; px++) {
                // Extra color values and alpha channel
                r = ((srcpx[px]) & image->pf.rmask) >> (image->pf.rshift);
                g = ((srcpx[px]) & image->pf.gmask) >> (image->pf.gshift);
                b = ((srcpx[px]) & image->pf.bmask) >> (image->pf.bshift);
                a = ((srcpx[px]) & image->pf.amask) >> (image->pf.ashift);
                // Apply alpha blend factor
                r = r * a / image->pf.amax;
                g = g * a / image->pf.gmax;
                b = b * a / image->pf.bmax;
                // Convert to destination depth
                r = r >> (8 - dpf->rbits);
                g = g >> (8 - dpf->gbits);
                b = b >> (8 - dpf->bbits);
                // Merge the colors             
                r |= ((dstpx[px] & dpf->rmask) >> dpf->rshift) *
                        (dpf->rmax - (a >> (8 - dpf->rbits))) / dpf->rmax;
                g |= ((dstpx[px] & dpf->gmask) >> dpf->gshift) *
                        (dpf->gmax - (a >> (8 - dpf->gbits))) / dpf->gmax;
                b |= ((dstpx[px] & dpf->bmask) >> dpf->bshift) *
                        (dpf->bmax - (a >> (8 - dpf->bbits))) / dpf->bmax;
                // Put the color as destination pixel
                dstpx[px] = ((r << dpf->rshift) & dpf->rmask) |
                            ((g << dpf->gshift) & dpf->gmask) |
                            ((b << dpf->bshift) & dpf->bmask);
            }
        }
        return 1;
    }
    else {
        printf("That wouldn't fit!\n");
    }
    return 0;
}

#undef SRC_TYPE
#undef DST_TYPE

