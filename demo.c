/* Some code to render text by rendering glyphs from TTF files
 *
 * This software is licensed under the BSD-3-Clause license:
 *
 * Copyright 2026 Mibi88
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <stdio.h>
#include <wchar.h>

#include "image.h"
#include "font.h"

#define MAIN_DEBUG_IMAGE    0

#if MAIN_DEBUG_IMAGE
static int debug_image(int argc, char **argv) {
    struct image img;
    int rc;

    size_t x, y;

    (void)argc;
    (void)argv;

    if((rc = image_create(&img, 32, 32, 72))){
        puts(image_get_error_str(rc));
        return 1;
    }

    img.data[33] = IMAGE_RGBAINT(255, 255, 0, 255);

    if((rc = image_write(&img, "test.bmp", IT_BMP, 0))){
        puts(image_get_error_str(rc));

        return 1;
    }

    image_free(&img);

    if((rc = image_load(&img, "test.bmp"))){
        puts(image_get_error_str(rc));

        return 1;
    }

    for(y=0;y<img.h;y++){
        for(x=0;x<img.w;x++){
            fputc(img.data[y*img.w+x]&IMAGE_RGBAINT(255, 255, 255, 0) ?
                  '.' : '#', stdout);
            /*printf("%08x ", img.data[y*img.w+x]);*/
        }
        fputc('\n', stdout);
    }

    image_free(&img);

    return 0;
}

#else
static void font_renderer_to_image(struct font_renderer *renderer,
                                   struct image *image,
                                   font_s32_t sx, font_s32_t sy) {
    font_u32_t x, y;
    /* TODO: Clip the area that can be drawn on the image first */

    for(y=0;y<renderer->height;y++){
        for(x=0;x<renderer->width;x++){
            font_s32_t ix = (font_s32_t)x+sx;
            font_s32_t iy = (font_s32_t)y+sy;

            if(ix >= 0 && ix < (font_s32_t)image->w &&
               iy >= 0 && iy < (font_s32_t)image->h){
                pixel_t c = renderer->data[y*renderer->width+x];
                if(c){
                    c = IMAGE_RGBAINT(255-c, 255-c, 255-c, 0);

                    image->data[iy*image->w+ix] = c;
                }
            }
        }
    }
}

static int debug_font(int argc, char **argv) {
    struct font font;
    struct font_renderer renderer;
    struct image image;
    font_u32_t glyph;
    wchar_t *str;
    char *tmp;
    size_t len;
    size_t i;
    int char_len;
    int line_w;
    int w, h;
    int rc;

    int size = 150;
    int dpi = 72;

    int x, y;

    setlocale(LC_CTYPE, "");

    if(argc < 4){
        fprintf(stderr, "USAGE: %s TTF_FILE STRING OUPUT_IMAGE\n", *argv);

        return 1;
    }

    if((rc = font_load(&font, argv[1]))){
        puts(font_get_error_str(rc));

        return 1;
    }
    if((rc = font_renderer_init(&renderer, &font, dpi,
                                font_scale_size(&font, dpi, size,
                                                font.xmax-font.xmin),
                                font_scale_size(&font, dpi, size,
                                                font.ymax-font.ymin)))){
        puts(font_get_error_str(rc));

        font_free(&font);

        return 1;
    }

    len = 0;
    tmp = argv[2];
    while((char_len = mblen(tmp, strlen(tmp))) > 0){
        len++;
        tmp += char_len;
    }
    if(char_len < 0 && strlen(tmp)){
        fputs("Invalid string!\n", stderr);

        return 1;
    }

    str = malloc((len+1)*sizeof(wchar_t));
    if(str == NULL){
        fputs("Can't allocate string!\n", stderr);

        font_renderer_free(&renderer);
        font_free(&font);

        return 1;
    }

    mbstowcs(str, argv[2], len);
    str[len] = 0;

    w = 0;
    line_w = 0;
    h = renderer.height;
    for(i=0;str[i];i++){
        wchar_t c = str[i];

        if(c == '\n'){
            if(line_w > w) w = line_w;
            line_w = 0;

            h += renderer.height;

            continue;
        }

        /* TODO: Support UTF-8. */
        glyph = font_lookup_char(&font, c);

        line_w += font_scale_size(&font, dpi, size,
                                  font.glyphs[glyph].advance_width);
    }
    if(line_w > w) w = line_w;

    if((rc = image_create(&image, w, h, dpi))){
        puts(image_get_error_str(rc));

        font_renderer_free(&renderer);
        font_free(&font);

        return 1;
    }

    memset(image.data, 0xFF, image.w*image.h*sizeof(pixel_t));

    x = 0;
    y = 0;
    for(i=0;str[i];i++){
        struct font_glyph *scaled_glyph;
        wchar_t c = str[i];

        font_s32_t x_offset;
        font_s32_t y_offset;

        if(c == '\n'){
            x = 0;
            y += renderer.height;

            continue;
        }

        /* TODO: Support UTF-8. */
        glyph = font_lookup_char(&font, c);

        scaled_glyph = font_scale_glyph(&font, glyph, size, dpi);
        if(scaled_glyph == NULL){
            fprintf(stderr, "%s: Failed to scale glyph!\n", *argv);

            continue;
        }

        x_offset = font_scale_size(&font, dpi, size,
                                   -font.glyphs[glyph].xmin)+
                   scaled_glyph->left_side_bearing;
        y_offset = font_scale_size(&font, dpi, size,
                                   font.max_ascender)-scaled_glyph->ymax;

        font_renderer_glyph(&renderer, scaled_glyph);
        font_renderer_to_image(&renderer, &image,
                               x+x_offset, y_offset);

        x += scaled_glyph->advance_width;

        font_free_glyph(scaled_glyph);
    }

    if((rc = image_write(&image, argv[3], IT_BMP, 0))){
        puts(image_get_error_str(rc));

        font_renderer_free(&renderer);
        font_free(&font);
        image_free(&image);

        return 1;
    }

    free(str);
    font_renderer_free(&renderer);
    font_free(&font);
    image_free(&image);

    return 0;
}
#endif

int main(int argc, char **argv) {
#if MAIN_DEBUG_IMAGE
    return debug_image(argc, argv);
#else
    return debug_font(argc, argv);
#endif
}
