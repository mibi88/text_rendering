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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_IMPL

#include "image.h"

#define STR(s) #s,
#define STR_LAST(s) #s

char *image_get_error_str(int error) {
    char *errors[] = {
        IMAGE_ERROR_X(STR, STR_LAST)
    };

    return errors[error];
}

int image_load(struct image *img, char *file) {
    FILE *fp;
    char sig[3] = "\0\0";
    unsigned char b[4];

    size_t size;
    size_t data_offset;
    size_t header_size;

    fp = fopen(file, "rb");

    if(fp == NULL){
        return IE_OPEN;
    }

    if(fread(sig, 1, 2, fp) != 2){
        fclose(fp);

        return IE_READ;
    }

    if(strcmp(sig, "\x42\x4D")){
        fclose(fp);

        return IE_SIG;
    }

    if(fread(b, 1, 4, fp) != 4){
        fclose(fp);

        return IE_READ;
    }

    size = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);

    if(fseek(fp, 4, SEEK_CUR)){
        fclose(fp);

        return IE_SEEK;
    }

    if(fread(b, 1, 4, fp) != 4){
        fclose(fp);

        return IE_READ;
    }

    data_offset = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);

    if(fread(b, 1, 4, fp) != 4){
        fclose(fp);

        return IE_READ;
    }

    header_size = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);

    switch(header_size){
        case 40:
            /* BITMAPINFOHEADER */

            {
                unsigned long int width;
                unsigned long int height;
                unsigned short int color_planes;
                unsigned short int bpp;
                unsigned long int ppi;
                unsigned long int compression;
                size_t padding;
                size_t raw_size;
                size_t hpxpm;
                size_t vpxpm;
                size_t colors;
                size_t important;

                size_t x, y;

                /* TODO: Read the whole header at once. */

                /* 4 byte width */
                if(fread(b, 1, 4, fp) != 4){
                    fclose(fp);

                    return IE_READ;
                }

                width = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);
                if(width&(1<<31)){
                    width = ~width+1;
                    /* TODO: Flip the image */
                    fclose(fp);
                    return IE_UNSUPPORTED;
                }

                /* 4 byte height */
                if(fread(b, 1, 4, fp) != 4){
                    fclose(fp);

                    return IE_READ;
                }

                height = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);

                /* 2 byte color plane count */
                if(fread(b, 1, 2, fp) != 2){
                    fclose(fp);

                    return IE_READ;
                }

                color_planes = b[0]|(b[1]<<8);

                /* 2 byte bits per pixel */
                if(fread(b, 1, 2, fp) != 2){
                    fclose(fp);

                    return IE_READ;
                }

                bpp = b[0]|(b[1]<<8);

                /* 4 byte compression method */
                if(fread(b, 1, 4, fp) != 4){
                    fclose(fp);

                    return IE_READ;
                }

                compression = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);

                /* 4 byte size of the raw image data (can be 0 for BI_RGB) */
                if(fread(b, 1, 4, fp) != 4){
                    fclose(fp);

                    return IE_READ;
                }

                raw_size = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);

                /* 4 byte horizontal pixel per metre */
                if(fread(b, 1, 4, fp) != 4){
                    fclose(fp);

                    return IE_READ;
                }

                hpxpm = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);

                /* 4 byte vertical pixel per metre */
                if(fread(b, 1, 4, fp) != 4){
                    fclose(fp);

                    return IE_READ;
                }

                vpxpm = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);

                /* 4 byte number of colors in the palette count
                 * (0 for 2^bit_count) */
                if(fread(b, 1, 4, fp) != 4){
                    fclose(fp);

                    return IE_READ;
                }

                colors = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);

                /* 4 byte number of important colors */
                if(fread(b, 1, 4, fp) != 4){
                    fclose(fp);

                    return IE_READ;
                }

                important = b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);

                ppi = (hpxpm+vpxpm)/(39*2);

#if DEBUG_IMAGE_READ
                printf("width:          %lu\n"
                       "height:         %lu\n"
                       "color_planes:   %lu\n"
                       "bpp:            %lu\n"
                       "compression:    %lu\n"
                       "raw_size:       %lu\n"
                       "hpxpm:          %lu\n"
                       "vpxpm:          %lu\n"
                       "ppi:            %lu\n"
                       "colors:         %lu\n"
                       "important:      %lu\n",
                       width, height, color_planes, bpp, compression,
                       raw_size, hpxpm, vpxpm, ppi, colors, important);
#endif
                (void)color_planes;
                (void)compression;
                (void)raw_size;
                (void)colors;
                (void)important;

                img->data = malloc(width*height*sizeof(pixel_t));

                if(img->data == NULL){
                    fclose(fp);

                    return IE_MEM;
                }

                padding = width*(bpp/8)%4 ? 4-width*(bpp/8)%4 : 0;

                if(bpp >= 24){
                    if(fseek(fp, data_offset, SEEK_SET)){
                        fclose(fp);

                        return IE_SEEK;
                    }

                    b[0] = 0;
                    b[1] = 0;
                    b[2] = 0;
                    b[3] = 0xFF;
                    for(y=height;y--;){
                        for(x=0;x<width;x++){
                            if(fread(b, 1, bpp/8, fp) != bpp/8){
                                fclose(fp);

                                return IE_READ;
                            }
                            img->data[y*width+x] = b[0]|(b[1]<<8)|(b[2]<<16)|
                                                   (b[3]<<24);
                        }
                        if(fseek(fp, padding, SEEK_CUR)){
                            fclose(fp);

                            return IE_SEEK;
                        }
                    }
                }else{
                    size_t pal;

                    pal = ftell(fp);

                    /* TODO */
                    (void)pal;

                    fclose(fp);
                    free(img->data);

                    return IE_UNSUPPORTED;
                }

                img->w = width;
                img->h = height;
                img->ppi = ppi;
            }

            break;
        default:
            /* Unsupported bitmap format */
#if DEBUG_IMAGE_READ
            printf("size:           %lu\n"
                   "header size:    %lu\n"
                   "data offset:    %lu\n",
                   size, header_size, data_offset);
#endif

            fclose(fp);
            return IE_UNSUPPORTED;
    }

    (void)size;

    fclose(fp);

    return IE_NONE;
}

int image_write(struct image *img, char *file, int type, int flags) {
    FILE *fp;

    (void)flags;

    fp = fopen(file, "wb");

    if(fp == NULL){
        return IE_OPEN;
    }

    switch(type){
        case IT_BMP:
            {
                size_t x, y;

                unsigned char b[56];
                size_t size = 54+img->w*img->h*4;

                size_t hpxpm = img->ppi*39;
                size_t vpxpm = img->ppi*39;

                b[0] = 0x42; /* ASCII 'B' */
                b[1] = 0x4D; /* ASCII 'M' */

                b[2] = size;
                b[3] = size>>8;
                b[4] = size>>16;
                b[5] = size>>24;

                b[6] = 0;
                b[7] = 0;
                b[8] = 0;
                b[9] = 0;

                b[10] = 54;
                b[11] = 0;
                b[12] = 0;
                b[13] = 0;

                b[14] = 40;
                b[15] = 0;
                b[16] = 0;
                b[17] = 0;

                b[18] = img->w;
                b[19] = img->w>>8;
                b[20] = img->w>>16;
                b[21] = img->w>>24;

                b[22] = img->h;
                b[23] = img->h>>8;
                b[24] = img->h>>16;
                b[25] = img->h>>24;

                b[26] = 1;
                b[27] = 0;

                b[28] = 32;
                b[29] = 0;

                b[30] = 0;
                b[31] = 0;
                b[32] = 0;
                b[33] = 0;

                b[34] = 0;
                b[35] = 0;
                b[36] = 0;
                b[37] = 0;

                b[38] = hpxpm;
                b[39] = hpxpm>>8;
                b[40] = hpxpm>>16;
                b[41] = hpxpm>>24;

                b[42] = vpxpm;
                b[43] = vpxpm>>8;
                b[44] = vpxpm>>16;
                b[45] = vpxpm>>24;

                b[46] = 0;
                b[47] = 0;
                b[48] = 0;
                b[49] = 0;

                b[50] = 0;
                b[51] = 0;
                b[52] = 0;
                b[53] = 0;

                if(fwrite(b, 1, 54, fp) != 54){
                    fclose(fp);

                    return IE_WRITE;
                }

                /* TODO: Make things more efficient. */
                for(y=img->h;y--;){
                    for(x=0;x<img->w;x++){
                        pixel_t p = img->data[y*img->w+x];

                        b[0] = p;
                        b[1] = p>>8;
                        b[2] = p>>16;
                        b[3] = p>>24;

                        if(fwrite(b, 1, 4, fp) != 4){
                            fclose(fp);

                            return IE_WRITE;
                        }
                    }
                }
            }
            break;
    }

    fclose(fp);

    return IE_NONE;
}

int image_create(struct image *img, size_t width, size_t height, size_t ppi) {
    img->data = calloc(width*height, sizeof(pixel_t));

    if(img->data == NULL){

        return IE_MEM;
    }

    img->w = width;
    img->h = height;

    img->ppi = ppi;

    return IE_NONE;
}

void image_free(struct image *img) {
    free(img->data);
    img->data = NULL;
    img->w = 0;
    img->h = 0;
    img->ppi = 0;
}

