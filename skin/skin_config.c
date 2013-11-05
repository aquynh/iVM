/*
 * Skin configuration reading
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
#include "expat.h"
#include "console.h"

#include "skin_config.h"
#include "skin_image.h"

const int BUFFER_SIZE = 1024;

/* Buffer Size is Doubled from Default. Default = 512 */

typedef struct SkinParser {
    SkinScreen* skin;
    SkinLayout* layout;
    char* filepath;
} SkinParser;

static inline int value_is(const char *attr, const char *name)
{
    return (strcmp(attr, name) == 0);
}

static int skin_load_images(SkinScreen* skin, SkinLayout* layout)
{
    //printf("skin_config.c: skin_load_images, background file='%s'\n", 
    //    layout->background->file);
    if (layout->background && layout->background->file != NULL) {
        skin->background = skin_load_image((char*)layout->background->file);
        skin->background->posx = layout->background->posx;
        skin->background->posy = layout->background->posy;
    }

    if (layout->keyboard && layout->keyboard->image->file != NULL) {
        skin->keyboard.image = skin_load_image((char*)layout->keyboard->image->file);
        skin->keyboard.image->posx = layout->keyboard->image->posx;
        skin->keyboard.image->posy = layout->keyboard->image->posy;
        if (layout->keyboard->animated) {
            // If keyboard image is animated, it consists of 5 pictures
            skin->keyboard.image->height /= 5;
            skin->keyboard.animated = 1;
        }
        else skin->keyboard.animated = 0;
        
        skin->keyboard.screenwidth = layout->keyboard->screenwidth;
        skin->keyboard.screenheight = layout->keyboard->screenheight;
        skin->keyboard.highlight_red = layout->keyboard->highlight_red;
        skin->keyboard.highlight_green = layout->keyboard->highlight_green;
        skin->keyboard.highlight_blue = layout->keyboard->highlight_blue;
        skin->keyboard.offset = layout->keyboard->offset;
    }

    SkinButtonConfig *latestButton = layout->buttons;
    if(latestButton) {
        skin->buttons = (SkinButton*)qemu_mallocz(sizeof(SkinButton));
    }

    SkinButton* button = skin->buttons;
    while (latestButton) {
        if (latestButton->image.file) {
            //printf("load button: %s, posx=%d, posy=%d\n",latestButton->image.file, 
            //    latestButton->image.posx, latestButton->image.posy);
            skin_load_image_data(&button->image, latestButton->image.file);
            button->key.posx = button->image.posx = latestButton->image.posx;
            button->key.posy = button->image.posy = latestButton->image.posy;
            button->key.keycode = latestButton->keycode;
            button->key.isswitch = latestButton->isswitch;
            button->key.defaultstate = latestButton->defaultstate;
            button->image.height /= 2;
            button->tooltip = latestButton->tooltip;
            // Switches have 4 pictures
            if (button->key.isswitch)
                button->image.height /= 2;
			button->key.width = button->image.width;
			button->key.height = button->image.height;
            if (layout->keyboard && layout->keyboard->switchcode != 0 &&
                layout->keyboard->switchcode == button->key.keycode) {
                int state = button->key.state;
                if (skin->keyboard.button) state = skin->keyboard.button->key.state;
                skin->keyboard.button = button;
                skin->keyboard.button->key.state = state;
            }
        }

        latestButton = latestButton->next;
        if(latestButton) {
            button->next = (SkinButton*)qemu_mallocz(sizeof(SkinButton));
            button = button->next;
        }
    }
    return 0;
}

static void skin_free_image(SkinImage** image)
{
    if (!(*image)) return;
    if ((*image)->data) {
        qemu_free((*image)->data);
        (*image)->data = NULL;
    }
    qemu_free(*image);
    *image = NULL;
}

static void skin_free_keys(SkinKey** keys)
{
    SkinKey* key = *keys;
    while(key) {
        *keys = key->next;
        qemu_free(key);
        key = *keys;
        *keys = NULL;
    }
}

static void skin_free_buttons(SkinButton** buttons)
{
    SkinButton* button = *buttons;
    while(button) {
        *buttons = button->next;
        if (button->image.data) {
            qemu_free(button->image.data);
            button->image.data = NULL;
        }
        qemu_free(button);
        button = *buttons;
        *buttons = NULL;
    }
}

int skin_activate_layout(SkinScreen* skin, int rotation)
{
    int result = -1;
    if (!skin->config->landscape && !skin->config->portrait) {
        //printf("skin_config.c: skin_activate_layout, no landscape or portrait\n");
        return result;
    }
    //printf("skin_config.c: skin_activate_layout, rotation=%d\n", rotation);
    SkinLayout* layout = skin->config->landscape;
    if (!skin->config->landscape) {
        layout = skin->config->portrait;
        // Only portrait possible
        skin->rotation_req = skin->rotation = on;
    }
    if (rotation == on) {
        if (skin->config->portrait) {
					 	layout = skin->config->portrait;
						skin->rotation_req = skin->rotation = off;
				}
        // Only landscape possible
        else skin->rotation_req = skin->rotation = on;
    } else {
					skin->rotation_req = skin->rotation = on;
		}
    if (layout->width == 0 || layout->height == 0) {
        // No value given in XML, use some default
        //printf("Default layout values\n");
        layout->width = 320;
        layout->height = 480;
    }
    if (layout->emuscreen_width == 0 || layout->emuscreen_height == 0) {
        // No default value given, make it 800x480 then
        layout->emuscreen_width = 320;
        layout->emuscreen_height = 480;
    }
    skin->width = layout->width;
    skin->height = layout->height;
    memcpy(&skin->bgcolor, &layout->bgcolor, sizeof(SkinBackgroundColor));
    if (layout->keyboard)
        skin->keyboard.animated = layout->keyboard->animated;
    
    skin->es->posx = layout->emuscreen_posx;
    skin->es->posy = layout->emuscreen_posy;
    skin->es->width = layout->emuscreen_width;
    skin->es->height = layout->emuscreen_width;
    
    // Store pointers to obsolete data    
    SkinImage*  curr_background = skin->background;
    SkinButton* curr_buttons    = skin->buttons;
    SkinImage*  curr_keyboard   = skin->keyboard.image;
    SkinKey*    curr_keys       = skin->keyboard.keys;

    // Copy all the keys
    if (layout->keyboard) {
        if (layout->keyboard->keys)
            skin->keyboard.keys = (SkinKey*)qemu_mallocz(sizeof(SkinKey));
        SkinKey *key = skin->keyboard.keys;
        SkinKeyConfig* keyc = layout->keyboard->keys;
        while(keyc) {
            key->posx = keyc->posx;
            key->posy = keyc->posy;
            key->width = keyc->width;
            key->height = keyc->height;
            key->keycode = keyc->keycode;
            keyc = keyc->next;
            if (keyc) {
                key->next = (SkinKey*)qemu_mallocz(sizeof(SkinKey));
                key = key->next;
            }
        }
    }
    result = skin_load_images(skin, layout);

    // Free obsolete data
    skin_free_image(&curr_background);
    skin_free_buttons(&curr_buttons);
    skin_free_image(&curr_keyboard);
    skin_free_keys(&curr_keys);

    return result;
}

static void parser_start_hndl(void *data, const char *element, const char **attr)
{
    //printf("parser_start_hndl, element=%s\n", element);
    SkinParser *parserconfig = (SkinParser*)data;
    SkinLayout *layout = NULL;
    SkinButtonConfig *latestButton = NULL;
    SkinKeyConfig *latestKey = NULL;
    int i = 0;
    unsigned int hex;
    // Print XML file content for testing
    /*for (i = 0; attr[i]; i += 2) {
        printf("attr %s='%s'\n", attr[i], attr[i + 1]);
    }*/    

    if (value_is(element, "root")) {
        // Initialize the pointers
        layout = NULL;
        latestButton = NULL;
        latestKey = NULL;
        return;
    }

    if (value_is(element, "landscape")) {
        if(parserconfig->layout != NULL) {
            fprintf(stderr, "XML Parsing failure unexpected nesting of %s\n", element);
            exit(1);
        }
        parserconfig->skin->config->landscape =
            (SkinLayout*) qemu_mallocz(sizeof(SkinLayout));
        parserconfig->layout = parserconfig->skin->config->landscape;
    }

    if (value_is(element, "portrait")) {
        if(parserconfig->layout != NULL) {
            fprintf(stderr, "XML Parsing failure unexpected nesting of %s\n", element);
            exit(1);
        }
        parserconfig->skin->config->portrait =
            (SkinLayout*) qemu_mallocz(sizeof(SkinLayout));
        parserconfig->layout = parserconfig->skin->config->portrait;
    }
    
    if (parserconfig) layout = parserconfig->layout;
    
    if (value_is(element, "tooltip")) {
        parserconfig->skin->tooltip.color |= 0xFF000000;    // Full alpha channel
        for (i = 0; attr[i]; i += 2) {
            if (value_is(attr[i], "red")) {
                parserconfig->skin->tooltip.color |= atoi(attr[i + 1]) << 16;
            }
            if (value_is(attr[i], "green")) {
                parserconfig->skin->tooltip.color |= atoi(attr[i + 1]) << 8;
            }
            if (value_is(attr[i], "blue")) {
                parserconfig->skin->tooltip.color |= atoi(attr[i + 1]);
            }
        }
    }

    if (value_is(element, "font")) {
        // We have a font, allocate it
        SkinFont* font = parserconfig->skin->font = (SkinFont*)qemu_mallocz(sizeof(SkinFont));
        for (i = 0; attr[i]; i += 2) {
            if (value_is(attr[i], "width")) {
                font->char_width = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "height")) {
                font->char_height = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "image")) {
                // We load the font image directly, first construct the path
                char* filepath = (char*)qemu_malloc(
                                        strlen(parserconfig->filepath) +
                                        strlen(attr[i + 1]) + 1);
                strcpy(filepath, parserconfig->filepath);
                strcat(filepath, attr[i + 1]);
                font->image = skin_load_image(filepath);
                qemu_free(filepath);
            }
        }
        // If we don't have an image, then no font
        if (!font->image || font->char_width == 0 || font->char_height == 0) {
            parserconfig->skin->font = NULL;
            qemu_free(font);
        }
    }

    if (value_is(element, "skin")) {
        for (i = 0; attr[i]; i += 2) {
            if (value_is(attr[i], "width")) {
                layout->width = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "height")) {
                layout->height = atoi(attr[i + 1]);
            }
        }
    }

    if (value_is(element, "bgcolor")) {
        for (i = 0; attr[i]; i += 2) {
            if (value_is(attr[i], "red")) {
                layout->bgcolor.red = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "green")) {
                layout->bgcolor.green = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "blue")) {
                layout->bgcolor.blue = atoi(attr[i + 1]);
            }
        }
    }
    if (value_is(element, "background")) {
        if (!layout->background) {
            layout->background =
                (SkinImageConfig*) qemu_mallocz(sizeof(SkinImageConfig));
            layout->background->file = NULL;
        }
        for (i = 0; attr[i]; i += 2) {
            if (value_is(attr[i], "image")) {
                layout->background->file = qemu_mallocz(strlen(parserconfig->filepath) +
                                                        strlen(attr[i + 1]) + 1);
                strcpy(layout->background->file, parserconfig->filepath);
                strcat(layout->background->file, attr[i + 1]);
                //printf("layout->background->file='%s'\n",layout->background->file);
            }
            if (value_is(attr[i], "posx")) {
                layout->background->posx = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "posy")) {
                layout->background->posy = atoi(attr[i + 1]);
            }
        }
    }
    if (value_is(element, "screen")) {
        for (i = 0; attr[i]; i += 2) {
            if (value_is(attr[i], "posx")) {
                layout->emuscreen_posx = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "posy")) {
                layout->emuscreen_posy = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "width")) {
                layout->emuscreen_width = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "height")) {
                layout->emuscreen_height = atoi(attr[i + 1]);
            }
        }
        //layout->emuscreen_posx, layout->emuscreen_posy,layout->emuscreen_width, layout->emuscreen_height);
    }
    
    if (value_is(element, "keyboard")) {
        if (!layout->keyboard) {
            layout->keyboard =
                (SkinKeyboardConfig*)qemu_mallocz(sizeof(SkinKeyboardConfig));
            layout->keyboard->image =
                (SkinImageConfig*) qemu_mallocz(sizeof(SkinImageConfig));
            layout->keyboard->image->file = NULL;
        }
        for (i = 0; attr[i]; i += 2) {
            if (value_is(attr[i], "image")) {
                layout->keyboard->image->file = qemu_mallocz(strlen(parserconfig->filepath) +
                                                             strlen(attr[i + 1]) + 1);
                strcpy(layout->keyboard->image->file, parserconfig->filepath);
                strcat(layout->keyboard->image->file, attr[i + 1]);
            }
            if (value_is(attr[i], "animated")) {
                 if (value_is(attr[i + 1], "yes")) 
                    layout->keyboard->animated = 1;
            }
            if (value_is(attr[i], "posx")) {
                layout->keyboard->image->posx = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "posy")) {
                layout->keyboard->image->posy = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "switchkey")) {
                layout->keyboard->switchcode = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "screenwidth")) {
                layout->keyboard->screenwidth = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "screenheight")) {
                layout->keyboard->screenheight = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "red")) {
                layout->keyboard->highlight_red = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "green")) {
                layout->keyboard->highlight_green = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "blue")) {
                layout->keyboard->highlight_blue = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "offset")) {
                layout->keyboard->offset = atoi(attr[i + 1]);
            }
        }
    }
    if (value_is(element, "key")) {
        if (!layout->keyboard->keys) {
            //printf("first key\n");
            layout->keyboard->keys = (SkinKeyConfig*) qemu_mallocz(sizeof(SkinKeyConfig));
            latestKey = layout->keyboard->keys;
        }
        else {
            //printf("not the first key\n");
            latestKey = layout->keyboard->keys;
            while (latestKey->next) latestKey = latestKey->next;
            latestKey->next = (SkinKeyConfig*) qemu_mallocz(sizeof(SkinKeyConfig));
            latestKey = latestKey->next;
        }
        for (i = 0; attr[i]; i += 2) {
            if (value_is(attr[i], "posx")) {
                latestKey->posx = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "posy")) {
                latestKey->posy = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "width")) {
                latestKey->width = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "height")) {
                latestKey->height = atoi(attr[i + 1]);
            }
            if (value_is(attr[i], "keycode")) {
                sscanf(attr[i + 1], "%X", &hex);
                latestKey->keycode = hex;
            }
        }
    }     
    if (value_is(element, "button")) {
        if (!layout->buttons) {
            //printf("first button\n");
            layout->buttons = (SkinButtonConfig*)qemu_mallocz(sizeof(SkinButtonConfig));
            latestButton = layout->buttons;
        }
        else {
            //printf("not the first button\n");
            latestButton = layout->buttons;
            while( latestButton->next ) latestButton = latestButton->next;
            latestButton->next = (SkinButtonConfig*)qemu_mallocz(sizeof(SkinButtonConfig));
            latestButton = latestButton->next;
        }       
        //printf("not the first button 2\n");
        for (i = 0; attr[i]; i += 2) {
            if (value_is(attr[i], "image")) {
                latestButton->image.file = (char*)qemu_malloc(strlen(attr[i + 1]) 
                    + strlen(parserconfig->filepath) + 1);
                strcpy(latestButton->image.file, parserconfig->filepath);
                strcat(latestButton->image.file, attr[i + 1]);
                //printf("button file='%s'\n", latestButton->image.file);
            }
            if (value_is(attr[i], "posx")) {
                latestButton->image.posx = atoi(attr[i + 1]);
                //printf("posx=%d\n", latestButton->image.posx);
            }
            if (value_is(attr[i], "posy")) {
                latestButton->image.posy = atoi(attr[i + 1]);
                //printf("posy=%d\n", latestButton->image.posy);
            }
            if (value_is(attr[i], "keycode")) {
                latestButton->keycode = atoi(attr[i + 1]);
                //printf("keycode=0x%x\n",latestButton->keycode);
            }
            if (value_is(attr[i], "switch")) {
                if (value_is(attr[i + 1], "yes")) {
                    latestButton->isswitch = 1;
                    latestButton->defaultstate = undefined;
                }
                //printf("switch=%s\n", latestButton->isswitch ? "yes":"no");
            }
            if (value_is(attr[i], "defaultstate")) {
                if (value_is(attr[i + 1], "on")) latestButton->defaultstate = on;
                if (value_is(attr[i + 1], "off")) latestButton->defaultstate = off;
                //printf("defaultstate=%d\n", latestButton->defaultstate);
            }
            if (value_is(attr[i], "action")) {
                latestButton->tooltip = (char*)qemu_malloc(strlen(attr[i + 1]) + 1);
                strcpy(latestButton->tooltip, attr[i + 1]);
            }
        }
    }
}


static void parser_end_hndl(void *data, const char *element)
{
    //printf("parser_end_hndl, element=%s\n", element);
    SkinParser *parserconfig = (SkinParser*)data;
    if (value_is(element, "landscape")) {
        if(parserconfig->layout != parserconfig->skin->config->landscape) {
            fprintf(stderr, "Unexpected ending of %s in XML file\n", element);
            exit(1);
        }
        parserconfig->layout = NULL;
    }

    if (value_is(element, "portrait")) {
        if(parserconfig->layout != parserconfig->skin->config->portrait) {
            fprintf(stderr, "Unexpected ending of %s in XML file\n", element);
            exit(1);
        }
        parserconfig->layout = NULL;
    }
}


static int skin_load_file(SkinScreen* skin, char *file)
{
    FILE* skin_xml;
    char buffer[BUFFER_SIZE];
    int done;
    XML_Parser parser = XML_ParserCreate(NULL);
    if (parser) {
        SkinParser parserconfig;
        parserconfig.skin = skin;
        parserconfig.layout = NULL;

        XML_SetUserData( parser, (void*)&parserconfig );
        XML_SetElementHandler(parser, parser_start_hndl, parser_end_hndl);
    
        skin_xml = fopen(file, "r");
        //printf("skin_config_c: skin_load_file = '%s'\n", file);
        if (!skin_xml) {
            fprintf(stderr, "Error opening skin file '%s'\n", file);
            XML_ParserFree(parser);
            return -1;
        }
        // Extract the path of the given XML file to be used as a default path
        char *p = strrchr(file, '/');
        if (p) {
            int index = p - file + 1;
            p = qemu_mallocz(index + 1);
            strncpy(p, file, index);
            // last character already zeroed at malloc
        } else {
            p = qemu_mallocz(3);
            p[0] = '.';
            p[1] = '/';
            // last character already zeroed at malloc
        }
        
        parserconfig.filepath = p;
        XML_SetUserData(parser, (void*)&parserconfig);
        XML_SetElementHandler(parser, parser_start_hndl, parser_end_hndl);
        do {
            if (fgets(buffer, sizeof(buffer), skin_xml) == NULL)
                done = 1;
            else {
                done = feof(skin_xml);
                if (!XML_Parse(parser, buffer, strlen(buffer), done)) {
                    fprintf(stderr, "Parse error at line %d: %s\n",
                        (int)XML_GetCurrentLineNumber(parser),
                        XML_ErrorString(XML_GetErrorCode(parser)));
                    // Continue anyway
                }
            }
        } while (!done);

        if (done && !feof(skin_xml)) {
            fprintf(stderr, "Parse error, unexpected EOF\n");
            // Continue anyway
        }

        XML_ParserFree(parser);
        qemu_free(parserconfig.filepath);

        // Create a buffer to store the information
        fclose(skin_xml);
        return skin_activate_layout(skin, skin->rotation);
    }
    return -1;
}

SkinScreen* skin_load_configuration(char* file, int portrait)
{
    //printf("skin_config.c: >> skin_load_configuration\n");
    SkinScreen *skin = (SkinScreen*) qemu_mallocz(sizeof(SkinScreen));
    if(!skin) return NULL;
    skin->es = (EmulatedScreen*) qemu_mallocz(sizeof(EmulatedScreen));
    //printf("skin_load_configuration 1\n");
    if(!skin->es) {
        qemu_free(skin);
        return NULL;
    }
    skin->config = (SkinConfig*) qemu_mallocz(sizeof(SkinConfig));
    //printf("skin_load_configuration 2\n");
    if (!skin->config) {
        qemu_free(skin->es);
        qemu_free(skin);
        return NULL;
    }
    if (portrait) {
			skin->rotation_req = skin->rotation = on;
    }
    if(skin_load_file(skin, file)) {
    //printf("skin_config.c: skin_load_configuration, skin loaded '%s'\n", file);
        qemu_free(skin->config);
        qemu_free(skin->es);
        qemu_free(skin);
        skin = NULL;
    }
    if (skin && skin->keyboard.button)
        skin->keyboard.button->key.state = ESkinBtn_Active;

    return skin;
}
