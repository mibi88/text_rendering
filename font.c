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

#define FILL 1
#define CURVES 1

#define STEPS 16

#define FONT_IMPL

#include "font.h"

#define STR(s) #s,
#define STR_LAST(s) #s

char *font_get_error_str(int error) {
    char *errors[] = {
        FONT_ERROR_X(STR, STR_LAST)
    };

    return errors[error];
}

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define SIGNED(a, b) ((a)&(1<<(b-1)) ? \
                      -(long int)(((a)^(((font_u32_t)1<<b)-1))+1) : (a))

static int glyph_cmp(const void *_a, const void *_b) {
    const struct font_glyph *a = *(const struct font_glyph**)_a;
    const struct font_glyph *b = *(const struct font_glyph**)_b;

    return a->code < b->code ? -1 : a->code > b->code;
}

/* XXX: Can fonts use the same glyph index for multiple character code?
 *      I don't handle that correctly currently.
 */
int font_load(struct font *font, char *file) {
    FILE *fp;

    struct font_glyph *glyphs;
    struct font_glyph **cmap;

    font_u32_t table_pos[FONT_REQUIRED_TABLES];
    font_u32_t table_count;

    font_u32_t glyph_count;

    font_s16_t xmin, ymin;
    font_s16_t xmax, ymax;

    unsigned short int style;

    unsigned short int glyph_recursion;

    unsigned short int units_per_em;

    unsigned short int points_max;
    unsigned short int contour_max;

    unsigned short int long_h_metric_count;

    unsigned char long_offsets;

    /* TODO: Verify the font's integrity */

    fp = fopen(file, "rb");
    if(fp == NULL){

        return FE_OPEN;
    }

#define ON_ERROR() \
    { \
        fclose(fp); \
    }

    {
        /* Read the table directory */

        static const unsigned char required_tables[FONT_REQUIRED_TABLES][4] = {
            {0x63, 0x6d, 0x61, 0x70}, /* cmap */
            {0x67, 0x6c, 0x79, 0x66}, /* glyf */
            {0x68, 0x65, 0x61, 0x64}, /* head */
            {0x68, 0x68, 0x65, 0x61}, /* hhea */
            {0x68, 0x6d, 0x74, 0x78}, /* hmtx */
            {0x6c, 0x6f, 0x63, 0x61}, /* loca */
            {0x6d, 0x61, 0x78, 0x70}, /* maxp */
            {0x6e, 0x61, 0x6d, 0x65}, /* name */
            {0x70, 0x6f, 0x73, 0x74}, /* post */
        };

        unsigned char b[MAX(4+2+3*2, 4*4)];

        font_u32_t i;

        unsigned short int found_tables = 0;

        if(fread(b, 1, 4+2+3*2, fp) != 4+2+3*2){
            ON_ERROR();

            return FE_READ;
        }

        table_count = (b[4]<<8)|b[5];

        for(i=0;i<table_count;i++){
            size_t n;

            if(fread(b, 1, 4*4, fp) != 4*4){
                ON_ERROR();

                return FE_READ;
            }

            for(n=0;n<FONT_REQUIRED_TABLES;n++){
                if(!memcmp(b, required_tables[n], 4)){
                    found_tables++;

                    table_pos[n] = (b[8]<<24)|(b[9]<<16)|(b[10]<<8)|b[11];

#if DEBUG_FONT
                    printf("Table %c%c%c%c has offset 0x%08x\n",
                           *b, b[1], b[2], b[3], table_pos[n]);
#endif

                    break;
                }
            }
        }

        if(found_tables != FONT_REQUIRED_TABLES){
            ON_ERROR();

            return FE_MISSING_TABLES;
        }
    }

    {
        /* Read the maxp table */

        unsigned char b[4+3*2];

        if(fseek(fp, table_pos[FONT_MAXP], SEEK_SET)){
            ON_ERROR();

            return FE_SEEK;
        }

        if(fread(b, 1, 4+3*2, fp) != 4+3*2){
            ON_ERROR();

            return FE_READ;
        }

        if(b[0] != 0 || b[1] != 1 || b[2] != 0 || b[3] != 0){
            ON_ERROR();

            return FE_CORRUPTED_MAXP;
        }

        glyph_count = (b[4]<<8)|b[5];

        points_max = (b[6]<<8)|b[7];

        contour_max = (b[8]<<8)|b[9];

        if(fseek(fp, 10*2, SEEK_CUR)){
            ON_ERROR();

            return FE_SEEK;
        }

        if(fread(b, 1, 2, fp) != 2){
            ON_ERROR();

            return FE_READ;
        }

        glyph_recursion = (b[0]<<8)|b[1];
#if DEBUG_FONT
        printf("Glyph recursion: %u\n", glyph_recursion);
#endif
    }

    /* Allocate memory to store the glyph structures */
    glyphs = malloc((glyph_count+1)*sizeof(struct font_glyph));
    cmap = malloc(glyph_count*sizeof(struct font_glyph*));
    if(glyphs == NULL || cmap == NULL){
        ON_ERROR();

        free(glyphs);
        free(cmap);

        return FE_MEM;
    }

#undef ON_ERROR
#define ON_ERROR() \
    { \
        fclose(fp); \
        free(glyphs); \
        free(cmap); \
    }

    {
        /* Read the head table */

        unsigned char b[MAX(4, MAX(10, 5*2))];

        if(fseek(fp, table_pos[FONT_HEAD], SEEK_SET)){
            ON_ERROR();

            return FE_SEEK;
        }

        if(fread(b, 1, 4, fp) != 4){
            ON_ERROR();

            return FE_READ;
        }
        if(b[0] != 0 || b[1] != 1 || b[2] != 0 || b[3] != 0){
            ON_ERROR();

            return FE_CORRUPTED_HEAD;
        }

        if(fseek(fp, 2*4, SEEK_CUR)){
            ON_ERROR();

            return FE_SEEK;
        }

        if(fread(b, 1, 8, fp) != 8){
            ON_ERROR();

            return FE_READ;
        }
        if(b[0] != 0x5F || b[1] != 0x0F || b[2] != 0x3C || b[3] != 0xF5){
            ON_ERROR();

            return FE_CORRUPTED_HEAD;
        }

        if(b[5]&1){
            font->vertical = 0;
        }else if(b[5]&(1<<6)){
            font->vertical = 1;
        }

        units_per_em = (b[6]<<8)|b[7];

        if(fseek(fp, 2*8, SEEK_CUR)){
            ON_ERROR();

            return FE_SEEK;
        }

        if(fread(b, 1, 5*2, fp) != 5*2){
            ON_ERROR();

            return FE_READ;
        }

        xmin = SIGNED((b[0]<<8)|b[1], 16);
        ymin = SIGNED((b[2]<<8)|b[3], 16);
        xmax = SIGNED((b[4]<<8)|b[5], 16);
        ymax = SIGNED((b[6]<<8)|b[7], 16);

        style = (b[8]<<8)|b[9];

        if(fseek(fp, 2*2+1, SEEK_CUR)){
            ON_ERROR();

            return FE_SEEK;
        }

        if(fread(&long_offsets, 1, 1, fp) != 1){
            ON_ERROR();

            return FE_READ;
        }
    }

    {
        /* Read the loca table */

        if(fseek(fp, table_pos[FONT_LOCA], SEEK_SET)){
            ON_ERROR();

            return FE_SEEK;
        }

        if(long_offsets){
            font_u32_t i;

            for(i=0;i<glyph_count+1;i++){
                unsigned char b[4];

#if DEBUG_FONT
                printf("cur: %08lx\n", ftell(fp));
#endif

                /* TODO: Read multiple offsets at once */
                if(fread(b, 1, 4, fp) != 4){
                    ON_ERROR();

                    return FE_READ;
                }

                glyphs[i].offset = (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
#if DEBUG_FONT
                printf("Glyph %u at 0x%08x\n", i, glyphs[i].offset+
                       table_pos[FONT_GLYF]);
#endif
            }
        }else{
            font_u32_t i;

            for(i=0;i<glyph_count+1;i++){
                unsigned char b[2];

#if DEBUG_FONT
                printf("cur: %08lx\n", ftell(fp));
#endif

                /* TODO: Read multiple offsets at once */
                if(fread(b, 1, 2, fp) != 2){
                    ON_ERROR();

                    return FE_READ;
                }

                glyphs[i].offset = ((b[0]<<8)|b[1])*2;
#if DEBUG_FONT
                printf("Glyph %u at 0x%04x\n", i, glyphs[i].offset+
                       table_pos[FONT_GLYF]);
#endif
            }
        }
    }

    {
        /* Load all the glyphs from the glyf table */

        struct {
            font_u32_t idx;
            font_u32_t offset;
        } *glyph_stack = NULL;
        unsigned char *contour_end_buffer;
        unsigned char *flags;

        font_u32_t cur = 0;

        font_u32_t i;

        contour_end_buffer = malloc(contour_max*2);
        flags = malloc(points_max);

#undef ON_ERROR
#define ON_ERROR() \
    { \
        fclose(fp); \
        free(glyphs); \
        free(cmap); \
        free(contour_end_buffer); \
        free(flags); \
    }

        if(contour_end_buffer == NULL || flags == NULL){
            ON_ERROR();

            return FE_MEM;
        }

        if(glyph_recursion){
            glyph_stack = malloc(glyph_recursion*sizeof(*glyph_stack));
            if(glyph_stack == NULL){
                ON_ERROR();

                return FE_MEM;
            }

#undef ON_ERROR
#define ON_ERROR() \
    { \
        fclose(fp); \
        free(glyphs); \
        free(cmap); \
        free(contour_end_buffer); \
        free(flags); \
        free(glyph_stack); \
    }
        }

        for(i=0;i<glyph_count;i++){
            glyphs[i].loaded = 0;
            glyphs[i].waits_loading = 0;
            glyphs[i].contour_ends = NULL;
            glyphs[i].points = NULL;
            glyphs[i].point_count = 0;
            glyphs[i].contour_count = 0;
            glyphs[i].code = 0;
            cmap[i] = glyphs+i;
        }

#undef ON_ERROR
#define ON_ERROR() \
    { \
        font_u32_t i; \
 \
        fclose(fp); \
        free(contour_end_buffer); \
        free(flags); \
 \
        for(i=0;i<glyph_count;i++){ \
            free(glyphs[i].contour_ends); \
        } \
 \
        free(glyphs); \
        free(cmap); \
        free(glyph_stack); \
    }

        i = 0;
        while(i < glyph_count || cur){
            unsigned char b[2+4*2];

            font_s16_t contour_count;

            if(i >= glyph_count){
                ON_ERROR();

                return FE_GLYPH_INDEX;
            }

            if(glyphs[i].loaded){
                goto CONTINUE;
            }

            if(glyphs[i].offset == glyphs[i+1].offset){
                glyphs[i].contour_ends = NULL;
                glyphs[i].contour_count = 0;
                glyphs[i].points = NULL;
                glyphs[i].point_count = 0;
                glyphs[i].loaded = 1;

                glyphs[i].xmin = 0;
                glyphs[i].ymin = 0;
                glyphs[i].xmax = 0;
                glyphs[i].ymax = 0;

                goto CONTINUE;
            }

            if(fseek(fp, table_pos[FONT_GLYF]+glyphs[i].offset, SEEK_SET)){
                ON_ERROR();

                return FE_SEEK;
            }

#if DEBUG_FONT
            printf("Loading glyph %u at 0x%08lx\n", i, ftell(fp));
#endif

            if(fread(b, 1, 2+4*2, fp) != 2+4*2){
                ON_ERROR();

                return FE_READ;
            }

            contour_count = SIGNED((b[0]<<8)|b[1], 16);

#if DEBUG_FONT
            printf("Contour count: %d\n", contour_count);
#endif

            glyphs[i].xmin = SIGNED((b[2]<<8)|b[3], 16);
            glyphs[i].ymin = SIGNED((b[4]<<8)|b[5], 16);
            glyphs[i].xmax = SIGNED((b[6]<<8)|b[7], 16);
            glyphs[i].ymax = SIGNED((b[8]<<8)|b[9], 16);

            if(contour_count >= 0){
                /* It is a simple glyph */

                unsigned short int n;
                unsigned short int point_count;

                unsigned short int instruction_len;

                font_s16_t x = 0;
                font_s16_t y = 0;

#if DEBUG_FONT
                printf("Load simple glyph from %lx\n", ftell(fp));
#endif

                if(contour_count > contour_max){
                    ON_ERROR();

                    return FE_CONTOUR_COUNT;
                }

                if(contour_count){
                    glyphs[i]
                        .contour_ends = malloc(contour_count*
                                               sizeof(unsigned short int));

                    if(glyphs[i].contour_ends == NULL){
                        ON_ERROR();

                        return FE_MEM;
                    }

                    if(fread(contour_end_buffer, 1, contour_count*2,
                             fp) != (unsigned short int)contour_count*2){
                        ON_ERROR();

                        return FE_READ;
                    }

                    for(n=0;n<(unsigned short int)contour_count;n++){
                        glyphs[i]
                            .contour_ends[n] = (contour_end_buffer[n*2]<<8)|
                                               contour_end_buffer[n*2+1];
                    }

                    point_count = glyphs[i].contour_ends[contour_count-1]+1;
                    if(point_count > points_max){
                        ON_ERROR();

                        return FE_TOO_MANY_POINTS;
                    }

                    if(fread(b, 1, 2, fp) != 2){
                        ON_ERROR();

                        return FE_READ;
                    }

                    instruction_len = (b[0]<<8)|b[1];

                    if(fseek(fp, instruction_len, SEEK_CUR)){
                        ON_ERROR();

                        return FE_SEEK;
                    }

                    /* Load the flags */
                    for(n=0;n<point_count;n++){
                        unsigned char flag;

                        if(fread(&flag, 1, 1, fp) != 1){
                            ON_ERROR();

                            return FE_READ;
                        }

                        flags[n] = flag;

                        if(flag&(1<<3)){
                            unsigned char count;
                            unsigned char m;

                            if(fread(&count, 1, 1, fp) != 1){
                                ON_ERROR();

                                return FE_READ;
                            }

                            for(m=0;m<count;m++){
                                if(n >= point_count-1){
                                    ON_ERROR();

                                    return FE_TOO_MANY_FLAGS;
                                }
                                flags[++n] = flag;
                            }
                        }
                    }

                    glyphs[i].points = malloc(point_count*
                                              sizeof(struct font_point));
                    if(glyphs[i].points == NULL){
                        ON_ERROR();

                        return FE_MEM;
                    }

                    glyphs[i].point_count = point_count;

#if DEBUG_FONT
                    for(n=0;n<point_count;n++){
                        printf("Flag %u: %02x\n", n, flags[n]);
                    }
#endif

                    /* Load the X coordinates */
                    for(n=0;n<point_count;n++){
                        if(flags[n]&(1<<1)){
                            /* 1 byte */
                            if(fread(b, 1, 1, fp) != 1){
                                ON_ERROR();

                                return FE_READ;
                            }

                            x += flags[n]&(1<<4) ? *b : -(font_s16_t)*b;
#if DEBUG_FONT
                            printf("One byte X coordinate. Offset: %d\n",
                                   flags[n]&(1<<4) ? *b : -(font_s16_t)*b);
#endif
                        }else if(!(flags[n]&(1<<4))){
                            /* 2 bytes */
                            if(fread(b, 1, 2, fp) != 2){
                                ON_ERROR();

                                return FE_READ;
                            }

                            x += SIGNED((b[0]<<8)|b[1], 16);
#if DEBUG_FONT
                            printf("Two byte X coordinate. Offset: %ld\n",
                                   SIGNED((b[0]<<8)|b[1], 16));
#endif
                        }
#if DEBUG_FONT
                        else{
                            puts("Repeat X");
                        }
                        printf("X: %d\n", x);
#endif

                        glyphs[i].points[n].x = x;
                        glyphs[i].points[n].on_curve = flags[n]&1;
                    }

                    /* Load the Y coordinates */
                    for(n=0;n<point_count;n++){
                        if(flags[n]&(1<<2)){
                            /* 1 byte */
                            if(fread(b, 1, 1, fp) != 1){
                                ON_ERROR();

                                return FE_READ;
                            }

                            y += flags[n]&(1<<5) ? *b : -(font_s16_t)*b;
#if DEBUG_FONT
                            printf("One byte Y coordinate. Offset: %d\n",
                                   flags[n]&(1<<5) ? *b : -(font_s16_t)*b);
#endif
                        }else if(!(flags[n]&(1<<5))){
                            /* 2 bytes */
                            if(fread(b, 1, 2, fp) != 2){
                                ON_ERROR();

                                return FE_READ;
                            }

                            y += SIGNED((b[0]<<8)|b[1], 16);
#if DEBUG_FONT
                            printf("Two byte Y coordinate. Offset: %ld\n",
                                   SIGNED((b[0]<<8)|b[1], 16));
#endif
                        }
#if DEBUG_FONT
                        else{
                            puts("Repeat Y");
                        }
                        printf("Y: %d\n", y);
#endif

                        glyphs[i].points[n].y = y;
                    }
                }

                glyphs[i].contour_count = contour_count;
                glyphs[i].loaded = 1;
            }else{
                /* It is a compound glyph */
                unsigned char comp_flags[2];
                unsigned char has_instr = 0;

#if DEBUG_FONT
                printf("Load compound glyph from %lx\n", ftell(fp));
#endif

                if(glyphs[i].waits_loading){
                    if(fseek(fp, glyph_stack[cur].offset, SEEK_SET)){
                        ON_ERROR();

                        return FE_SEEK;
                    }
                }

                do{
                    long offset;
                    unsigned short int idx;

                    font_s16_t dx, dy;

                    if((offset = ftell(fp)) < 0){
                        ON_ERROR();

                        return FE_TELL;
                    }

                    if(fread(comp_flags, 1, 2, fp) != 2){
                        ON_ERROR();

                        return FE_READ;
                    }

                    if(fread(b, 1, 2, fp) != 2){
                        ON_ERROR();

                        return FE_READ;
                    }
                    idx = (b[0]<<8)|b[1];

                    if(idx >= glyph_count){
                        ON_ERROR();

                        return FE_INVALID_COMPONENT_IDX;
                    }

                    if(!glyphs[idx].loaded){
#if DEBUG_FONT
                        printf("Glyph %u needs to be loaded!\n", idx);
#endif
                        if(cur >= glyph_recursion){
                            ON_ERROR();

                            return FE_STACK_OVERFLOW;
                        }

                        glyphs[i].waits_loading = 1;

#if DEBUG_FONT
                        printf("Pushing at %u!\n", cur);
#endif

                        glyph_stack[cur].idx = i;
                        glyph_stack[cur].offset = offset;
                        cur++;

                        i = idx;

                        goto LOAD;
                    }
#if DEBUG_FONT
                    printf("Component glyph %u already loaded!\n", idx);
#endif

                    if(comp_flags[1]&(1<<1)){
                        /* args are coordinates */

                        if(comp_flags[1]&1){
                            /* 2 bytes */

                            if(fread(b, 1, 4, fp) != 4){
                                ON_ERROR();

                                return FE_READ;
                            }

                            dx = SIGNED((b[0]<<8)|b[1], 16);
                            dy = SIGNED((b[2]<<8)|b[3], 16);
                        }else{
                            /* 1 byte */

                            if(fread(b, 1, 2, fp) != 2){
                                ON_ERROR();

                                return FE_READ;
                            }

                            dx = SIGNED(b[0], 8);
                            dy = SIGNED(b[1], 8);
                        }
                    }else{
                        /* args are point indices */
                        unsigned short int compound_idx;
                        unsigned short int component_idx;

                        if(comp_flags[1]&1){
                            /* 2 bytes */

                            if(fread(b, 1, 4, fp) != 4){
                                ON_ERROR();

                                return FE_READ;
                            }

                            compound_idx = (b[0]<<8)|b[1];
                            component_idx = (b[2]<<8)|b[3];
                        }else{
                            /* 1 byte */

                            if(fread(b, 1, 2, fp) != 2){
                                ON_ERROR();

                                return FE_READ;
                            }

                            compound_idx = b[0];
                            component_idx = b[1];
                        }

                        if(compound_idx >= glyphs[i].point_count ||
                           component_idx >= glyphs[idx].point_count){
                            ON_ERROR();

                            return FE_INVALID_POINT_IDX;
                        }

                        dx = glyphs[i].points[compound_idx].x-
                             glyphs[idx].points[compound_idx].x;
                        dy = glyphs[i].points[compound_idx].y-
                             glyphs[idx].points[compound_idx].y;
                    }

                    {
                        /* Load the contour ends and the points. */

                        font_u32_t n;
                        void *ptr;

                        ptr = realloc(glyphs[i].contour_ends,
                                      (glyphs[i].contour_count+
                                       glyphs[idx].contour_count)*
                                      sizeof(unsigned short int));
                        if(ptr == NULL){
                            ON_ERROR();

                            return FE_MEM;
                        }
                        glyphs[i].contour_ends = ptr;

                        ptr = realloc(glyphs[i].points,
                                      (glyphs[i].point_count+
                                       glyphs[idx].point_count)*
                                      sizeof(struct font_point));
                        if(ptr == NULL){
                            ON_ERROR();

                            return FE_MEM;
                        }
                        glyphs[i].points = ptr;

                        for(n=0;n<glyphs[idx].contour_count;n++){
                            register font_u32_t b = glyphs[i].contour_count;

                            glyphs[i].contour_ends[b+n] = glyphs[idx]
                                    .contour_ends[n]+glyphs[i].point_count;
                        }
                        glyphs[i].contour_count += glyphs[idx].contour_count;

                        for(n=0;n<glyphs[idx].point_count;n++){
                            struct font_point point;

                            register font_u32_t b = glyphs[i].point_count;

                            point = glyphs[idx].points[n];

                            point.x += dx;
                            point.y += dy;

                            glyphs[i].points[b+n] = point;
                        }
                        glyphs[i].point_count += glyphs[idx].point_count;
                    }

                    if(comp_flags[1]&(1<<3)){
                        /* Simple scale */

                        fputs("FIXME: Scale transformation unsupported!\n",
                              stderr);

                        if(fseek(fp, 2, SEEK_CUR)){
                            ON_ERROR();

                            return FE_SEEK;
                        }
                    }else if(comp_flags[1]&(1<<6)){
                        /* X and Y scale */

                        fputs("FIXME: X/Y scale transformation unsupported!\n",
                              stderr);

                        if(fseek(fp, 2*2, SEEK_CUR)){
                            ON_ERROR();

                            return FE_SEEK;
                        }
                    }else if(comp_flags[1]&(1<<7)){
                        /* Two by two matrix */

                        fputs("FIXME: Two-by-two matrix transformation "
                              "unsupported!\n", stderr);

                        if(fseek(fp, 4*2, SEEK_CUR)){
                            ON_ERROR();

                            return FE_SEEK;
                        }
                    }

                    has_instr |= *comp_flags&1;
                }while(comp_flags[1]&(1<<5));
                if(has_instr){
                    unsigned short int instruction_len;

                    if(fread(b, 1, 2, fp) != 2){
                        ON_ERROR();

                        return FE_READ;
                    }

                    instruction_len = (b[0]<<8)|b[1];

                    if(fseek(fp, instruction_len, SEEK_CUR)){
                        ON_ERROR();

                        return FE_SEEK;
                    }
                }

                glyphs[i].waits_loading = 0;
                glyphs[i].loaded = 1;
            }

CONTINUE:
            if(cur){
                i = glyph_stack[--cur].idx;
#if DEBUG_FONT
                printf("Returned to %u\n", cur);
#endif
            }else{
                i++;
            }
LOAD:
            (void)glyphs; /* Labels need to be followed by a statement */
        }

        free(contour_end_buffer);
        free(flags);
        free(glyph_stack);
    }

    font->glyphs = glyphs;
    font->glyph_count = glyph_count;

#undef ON_ERROR
#define ON_ERROR() \
    { \
        font_u32_t i; \
 \
        fclose(fp); \
 \
        for(i=0;i<glyph_count;i++){ \
            free(glyphs[i].contour_ends); \
        } \
 \
        free(glyphs); \
        free(cmap); \
    }

    {
        /* Read the cmap table */

        /* FIXME: I think I'm loading some cmap incorrectly. */

        unsigned short int subtables;
        unsigned short int i;

        unsigned short int best_platform_id;
        unsigned short int best_platform_specific_id;
        unsigned short int best_format;

        font_u32_t best_offset;

        unsigned char b[MAX(2*2+4, 3*4)];

        if(fseek(fp, table_pos[FONT_CMAP], SEEK_SET)){
            ON_ERROR();

            return FE_SEEK;
        }

        if(fread(b, 1, 2*2, fp) != 2*2){
            ON_ERROR();

            return FE_READ;
        }

        if(b[0] != 0 || b[1] != 0){
            ON_ERROR();

            return FE_CORRUPTED_CMAP;
        }

        subtables = (b[2]<<8)|b[3];

        for(i=0;i<subtables;i++){
            font_u32_t offset;

            unsigned short int platform_id;
            unsigned short int platform_specific_id;

            if(fread(b, 1, 2*2+4, fp) != 2*2+4){
                ON_ERROR();

                return FE_READ;
            }

            platform_id = (b[0]<<8)|b[1];
            platform_specific_id = (b[2]<<8)|b[3];

            offset = (b[4]<<24)|(b[5]<<16)|(b[6]<<8)|b[7];

            if(platform_id == 0){
                /* Unicode */

                if(platform_specific_id == 3 || platform_specific_id == 4){
                    unsigned short int format;

                    long r;

                    if((r = ftell(fp)) < 0){
                        ON_ERROR();

                        return FE_TELL;
                    }

                    if(fseek(fp, table_pos[FONT_CMAP]+offset, SEEK_SET)){
                        ON_ERROR();

                        return FE_SEEK;
                    }

                    if(fread(b, 1, 2, fp) != 2){
                        ON_ERROR();

                        return FE_READ;
                    }

                    format = (b[0]<<8)|b[1];

                    if(format == 4 || format == 12){
                        best_platform_id = platform_id;
                        best_platform_specific_id = platform_specific_id;
                        best_offset = offset+2;
                        best_format = format;

                        break;
                    }

                    if(fseek(fp, r, SEEK_SET)){
                        ON_ERROR();

                        return FE_SEEK;
                    }
                }
            }
        }

        if(i >= subtables){
            ON_ERROR();

            return FE_NO_SUPPORTED_CMAP_SUBTABLE;
        }

        /* FIXME: Seek to the best offset */
        (void)best_offset;

        if(best_platform_id == 0){
            if(best_platform_specific_id == 3 ||
               best_platform_specific_id == 4){
                if(best_format == 4){
                    unsigned short int segment_count_2;
                    unsigned short int n;

                    unsigned char *end_codes;
                    unsigned char *start_codes;
                    unsigned char *deltas;
                    unsigned char *offsets;

#if DEBUG_FONT
                    puts("Using cmap format 4!");
#endif

                    if(fseek(fp, 2*2, SEEK_CUR)){
                        ON_ERROR();

                        return FE_SEEK;
                    }

                    if(fread(b, 1, 2, fp) != 2){
                        ON_ERROR();

                        return FE_READ;
                    }

                    segment_count_2 = ((b[0]<<8)|b[1]);

                    end_codes = malloc(segment_count_2);
                    start_codes = malloc(segment_count_2);
                    deltas = malloc(segment_count_2);
                    offsets = malloc(segment_count_2);
                    if(end_codes == NULL ||
                       start_codes == NULL ||
                       deltas == NULL ||
                       offsets == NULL){
                        ON_ERROR();

                        free(end_codes);
                        free(start_codes);
                        free(deltas);
                        free(offsets);

                        return FE_MEM;
                    }
#undef ON_ERROR
#define ON_ERROR() \
    { \
        font_u32_t i; \
 \
        fclose(fp); \
 \
        free(end_codes); \
        free(start_codes); \
        free(deltas); \
        free(offsets); \
 \
        for(i=0;i<glyph_count;i++){ \
            free(glyphs[i].contour_ends); \
        } \
 \
        free(glyphs); \
        free(cmap); \
    }

                    if(fseek(fp, 3*2, SEEK_CUR)){
                        ON_ERROR();

                        return FE_SEEK;
                    }

                    if(fread(end_codes, 1, segment_count_2,
                             fp) != segment_count_2){
                        ON_ERROR();

                        return FE_READ;
                    }
                    if(fseek(fp, 2, SEEK_CUR)){
                        ON_ERROR();

                        return FE_SEEK;
                    }
                    if(fread(start_codes, 1, segment_count_2,
                             fp) != segment_count_2){
                        ON_ERROR();

                        return FE_READ;
                    }
                    if(fread(deltas, 1, segment_count_2,
                             fp) != segment_count_2){
                        ON_ERROR();

                        return FE_READ;
                    }
                    if(fread(offsets, 1, segment_count_2,
                             fp) != segment_count_2){
                        ON_ERROR();

                        return FE_READ;
                    }

                    for(n=0;n<segment_count_2;n+=2){
                        long r;

                        unsigned short int m;

                        unsigned short int start_code;
                        unsigned short int end_code;
                        unsigned short int delta;
                        unsigned short int offset;

                        start_code = (start_codes[n]<<8)|start_codes[n+1];
                        end_code = (end_codes[n]<<8)|end_codes[n+1];
                        delta = (deltas[n]<<8)|deltas[n+1];
                        offset = (offsets[n]<<8)|offsets[n+1];

                        if((r = ftell(fp)) < 0){
                            ON_ERROR();

                            return FE_TELL;
                        }

                        if(offset){
                            for(m=start_code;m<end_code;m++){
                                unsigned short int glyph_idx;

                                if(fseek(fp, offset+2*(m-start_code)-
                                             segment_count_2,
                                         SEEK_CUR)){
                                    ON_ERROR();

                                    return FE_SEEK;
                                }

                                if(fread(b, 1, 2, fp) != 2){
                                    ON_ERROR();

                                    return FE_READ;
                                }

                                glyph_idx = (b[0]<<8)|b[1];

                                if(fseek(fp, r, SEEK_SET)){
                                    ON_ERROR();

                                    return FE_SEEK;
                                }

                                if(glyph_idx >= glyph_count){
                                    ON_ERROR();

#if DEBUG_FONT
                                    printf("%08x -- %u/%u\n",
                                           m, glyph_idx, glyph_count);
#endif
                                    return FE_CMAP_INVALID_GLYPH_INDEX;
                                }

                                glyphs[glyph_idx].code = m;
                            }
                        }else{
                            for(m=start_code;m<end_code;m++){
                                unsigned short int glyph_idx;

                                glyph_idx = (delta+m)&0xFFFF;

                                if(glyph_idx >= glyph_count){
                                    ON_ERROR();

#if DEBUG_FONT
                                    printf("%08x -- %u/%u\n",
                                           m, glyph_idx, glyph_count);
#endif
                                    return FE_CMAP_INVALID_GLYPH_INDEX;
                                }

                                glyphs[glyph_idx].code = m;
                            }
                        }
                    }

                    free(end_codes);
                    free(start_codes);
                    free(deltas);
                    free(offsets);

#undef ON_ERROR
#define ON_ERROR() \
    { \
        font_u32_t i; \
 \
        fclose(fp); \
 \
        for(i=0;i<glyph_count;i++){ \
            free(glyphs[i].contour_ends); \
        } \
 \
        free(glyphs); \
        free(cmap); \
    }
                }else if(best_format == 12){
                    font_u32_t group_count;
                    font_u32_t n;

#if DEBUG_FONT
                    puts("Using cmap format 12!");
#endif

                    if(fseek(fp, 2+2*4, SEEK_CUR)){
                        ON_ERROR();

                        return FE_SEEK;
                    }

                    if(fread(b, 1, 4, fp) != 4){
                        ON_ERROR();

                        return FE_READ;
                    }

                    group_count = (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
                    for(n=0;n<group_count;n++){
                        font_u32_t m;

                        font_u32_t start_code;
                        font_u32_t end_code;
                        font_u32_t glyph_index;

                        if(fread(b, 1, 3*4, fp) != 3*4){
                            ON_ERROR();

                            return FE_READ;
                        }

                        start_code = (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
                        end_code = (b[4]<<24)|(b[5]<<16)|(b[6]<<8)|b[7];
                        glyph_index = (b[8]<<24)|(b[9]<<16)|(b[10]<<8)|b[11];

                        if(glyph_index >= glyph_count ||
                           glyph_index+(end_code-start_code) >= glyph_count){
                            ON_ERROR();

                            return FE_CMAP_INVALID_GLYPH_INDEX;
                        }

                        for(m=start_code;m<end_code;m++){
                            glyphs[m-start_code].code = m;
                        }
                    }
                }
            }
        }

        font->cmap = cmap;
    }

    /* Sort the cmap array to do interpolation search on it */
    qsort(cmap, glyph_count, sizeof(struct font_glyph*), glyph_cmp);

    {
        /* Read the hhea table */

        unsigned char b[MAX(4, 3*2)];

        if(fseek(fp, table_pos[FONT_HHEA], SEEK_SET)){
            ON_ERROR();

            return FE_SEEK;
        }

        if(fread(b, 1, 4, fp) != 4){
            ON_ERROR();

            return FE_READ;
        }
        if(b[0] != 0 || b[1] != 1 || b[2] != 0 || b[3] != 0){
            ON_ERROR();

            return FE_CORRUPTED_HHEA;
        }

        if(fread(b, 1, 3*2, fp) != 3*2){
            ON_ERROR();

            return FE_READ;
        }

        font->max_ascender = SIGNED((b[0]<<8)|b[1], 16);
        font->max_descender = SIGNED((b[2]<<8)|b[3], 16);
        font->line_gap = SIGNED((b[4]<<8)|b[5], 16);

        if(fseek(fp, 12*2, SEEK_CUR)){
            ON_ERROR();

            return FE_SEEK;
        }

        if(fread(b, 1, 2, fp) != 2){
            ON_ERROR();

            return FE_READ;
        }

        long_h_metric_count = (b[0]<<8)|b[1];
        if(long_h_metric_count < 1){
            ON_ERROR();

            return FE_TOO_FEW_LONG_H_METRICS;
        }
    }

    {
        /* Read the glyph metrics from the hmtx table */

        unsigned short int i;

        unsigned short int advance_width;

        if(fseek(fp, table_pos[FONT_HMTX], SEEK_SET)){
            ON_ERROR();

            return FE_SEEK;
        }

        for(i=0;i<long_h_metric_count;i++){
            unsigned char b[4];

            if(fread(b, 1, 2*2, fp) != 2*2){
                ON_ERROR();

                return FE_READ;
            }

            glyphs[i].advance_width = (b[0]<<8)|b[1];
            glyphs[i].left_side_bearing = SIGNED((b[2]<<8)|b[3], 16);
        }

        advance_width = glyphs[long_h_metric_count-1].advance_width;

        for(;i<glyph_count;i++){
            unsigned char b[2];

            if(fread(b, 1, 2, fp) != 2){
                ON_ERROR();

                return FE_READ;
            }

            glyphs[i].advance_width = advance_width;
            glyphs[i].left_side_bearing = SIGNED((b[0]<<8)|b[1], 16);
        }
    }

    fclose(fp);

    font->xmin = xmin;
    font->ymin = ymin;
    font->xmax = xmax;
    font->ymax = ymax;

    font->units_per_em = units_per_em;

    font->style = style;

    font->points_max = points_max;

    return FE_NONE;
}

font_s32_t font_scale_size(struct font *font, font_s32_t dpi,
                           font_s32_t points, font_s32_t size) {
    return size*points*dpi/(72*font->units_per_em);
}

font_u32_t font_lookup_char(struct font *font, font_u32_t code) {
    font_u32_t a = 0;
    font_u32_t b = font->glyph_count-1;

    while(a < b){
        font_u32_t m = (a+b)/2;

        if(font->cmap[m]->code > code){
            b = m;
        }else if(font->cmap[m]->code == code){
            return font->cmap[m]-font->glyphs;
        }else if(font->cmap[m]->code < code){
            a = m+1;
        }
    }

    return 0;
}

void font_free(struct font *font) {
    font_u32_t i;

    for(i=0;i<font->glyph_count;i++){
        free(font->glyphs[i].points);
        free(font->glyphs[i].contour_ends);
    }

    free(font->glyphs);
    font->glyphs = NULL;
    free(font->cmap);
    font->cmap = NULL;

    font->glyph_count = 0;
}

int font_renderer_init(struct font_renderer *renderer, struct font *font,
                       font_u32_t dpi, font_u32_t width, font_u32_t height) {
    renderer->data = malloc(width*height);
    renderer->x_on_line = malloc((font->points_max+1)*
                                 sizeof(struct font_renderer_hits));
    if(renderer->data == NULL){
        return FE_MEM;
    }

    renderer->width = width;
    renderer->height = height;
    renderer->dpi = dpi;

    return FE_NONE;
}

#define SIGN(x) ((x) ? (x) < 0 ? -1 : 1 : 0)

static int compare_hits(const void *a, const void *b) {
    return ((struct font_renderer_hits*)a)->x <
           ((struct font_renderer_hits*)b)->x ? -1 : 1;
}

struct font_glyph *font_scale_glyph(struct font *font, font_u32_t glyph_id,
                                    font_u32_t size, font_u32_t dpi) {
    struct font_glyph *glyph = font->glyphs+glyph_id;
    struct font_glyph *new_glyph;

    size_t i;

    new_glyph = malloc(sizeof(struct font_glyph));
    if(new_glyph == NULL) return NULL;

    *new_glyph = *glyph;

    new_glyph->points = malloc(glyph->point_count*sizeof(struct font_point));
    new_glyph->contour_ends = malloc(glyph->contour_count*
                                     sizeof(unsigned short int));

    if(new_glyph->points == NULL || new_glyph->contour_ends == NULL){
        free(new_glyph->points);
        free(new_glyph->contour_ends);

        free(new_glyph);

        return NULL;
    }

    memcpy(new_glyph->contour_ends, glyph->contour_ends,
           glyph->contour_count*sizeof(unsigned short int));

    for(i=0;i<new_glyph->point_count;i++){
        new_glyph->points[i].x = font_scale_size(font, dpi, size,
                                                 glyph->points[i].x);
        new_glyph->points[i].y = font_scale_size(font, dpi, size,
                                                 glyph->points[i].y);
        new_glyph->points[i].on_curve = glyph->points[i].on_curve;
    }

    new_glyph->advance_width = font_scale_size(font, dpi, size,
                                               glyph->advance_width);
    new_glyph->left_side_bearing = font_scale_size(font, dpi, size,
                                                   glyph->left_side_bearing);

    new_glyph->xmin = font_scale_size(font, dpi, size, glyph->xmin);
    new_glyph->ymin = font_scale_size(font, dpi, size, glyph->ymin);
    new_glyph->xmax = font_scale_size(font, dpi, size, glyph->xmax);
    new_glyph->ymax = font_scale_size(font, dpi, size, glyph->ymax);

    return new_glyph;
}

void font_free_glyph(struct font_glyph *glyph) {
    free(glyph->points);
    glyph->points = NULL;
    free(glyph->contour_ends);
    glyph->contour_ends = NULL;

    free(glyph);
}

void font_renderer_glyph(struct font_renderer *renderer,
                         struct font_glyph *glyph) {
    struct font_renderer_hits *x_on_line = renderer->x_on_line;

    font_u32_t i;

    /* TODO: Optimize this. */

    font_s32_t min_y = renderer->height-1;
    font_s32_t max_y = 0;
    font_s32_t min_x = renderer->width-1;
    font_s32_t max_x = 0;

    memset(renderer->data, 0, renderer->width*renderer->height);

    {
        font_u32_t i;

        for(i=0;i<glyph->point_count;i++){
            font_s32_t x = glyph->points[i].x;
            font_s32_t y = glyph->ymax-glyph->points[i].y;

            if(y > max_y) max_y = y;
            if(y < min_y) min_y = y;
            if(x > max_x) max_x = x;
            if(x < min_x) min_x = x;

            glyph->points[i].y = y;
        }
    }

    if(min_y < 0) min_y = 0;
    if(max_y > (font_s32_t)renderer->height) max_y = renderer->height;

    {
        font_s32_t y;
        unsigned char *l = renderer->data+min_y*renderer->width;

        for(y=min_y;y<max_y;y++){
            font_u32_t i;
            font_u32_t x_on_line_count = 0;

            font_s32_t x = min_x;
            font_s32_t next_x;
            font_s32_t fill = 0;

            for(i=0;i<glyph->contour_count;i++){
                font_u32_t start_point = i ? glyph->contour_ends[i-1]+1 : 0;
                font_u32_t end_point = glyph->contour_ends[i];

                font_u32_t i;

                for(i=start_point+1;i<=end_point;i++){
                    struct font_point lower =
                        glyph->points[i-1].y < glyph->points[i].y ?
                        glyph->points[i-1] : glyph->points[i];
                    struct font_point upper =
                        glyph->points[i-1].y >= glyph->points[i].y ?
                        glyph->points[i-1] : glyph->points[i];
                    font_s32_t direction = SIGN(glyph->points[i].y-
                                                glyph->points[i-1].y);

                    if(lower.y <= y && upper.y > y){
                        font_s32_t nx = lower.x+(upper.x-lower.x)*(y-lower.y)/
                                        (upper.y-lower.y);
                        if(nx < (font_s32_t)renderer->width){
                            x_on_line[x_on_line_count].x = nx;
                            x_on_line[x_on_line_count++].direction = direction;
                        }
                    }
                }

                {
                    struct font_point lower =
                        glyph->points[end_point].y <
                            glyph->points[start_point].y ?
                        glyph->points[end_point] : glyph->points[start_point];
                    struct font_point upper =
                        glyph->points[end_point].y >=
                            glyph->points[start_point].y ?
                        glyph->points[end_point] : glyph->points[start_point];
                    font_s32_t direction = SIGN(glyph->points[start_point].y-
                                                glyph->points[end_point].y);

                    if(lower.y <= y && upper.y > y){
                        font_s32_t nx = lower.x+(upper.x-lower.x)*(y-lower.y)/
                                        (upper.y-lower.y);
                        if(nx < (font_s32_t)renderer->width){
                            x_on_line[x_on_line_count].x = nx;
                            x_on_line[x_on_line_count++].direction = direction;
                        }
                    }
                }
            }

            qsort(x_on_line, x_on_line_count,
                  sizeof(struct font_renderer_hits), compare_hits);

            x_on_line[x_on_line_count].x = renderer->width;
            x_on_line[x_on_line_count++].direction = 0;

            next_x = x_on_line[0].x;
            for(i=1;i<x_on_line_count && next_x<0;i++){
                next_x = x_on_line[i].x;
                fill += x_on_line[i-1].direction;
            }

            x = 0;
            for(;i<x_on_line_count;i++){
                if(fill){
                    for(;x<next_x;x++,l++){
                        *l = 255;
                    }
                }else{
                    l += next_x-x;
                    x = next_x;
                }

                next_x = x_on_line[i].x;

                fill += x_on_line[i-1].direction;
            }

            next_x = renderer->width;

            if(fill){
                for(;x<next_x;x++,l++){
                    *l = 255;
                }
            }else{
                l += next_x-x;
                x = next_x;
            }
        }
    }

    {
        font_u32_t i;

        for(i=0;i<glyph->point_count;i++){
            glyph->points[i].y = glyph->ymax-glyph->points[i].y;
        }
    }
}

void font_renderer_free(struct font_renderer *renderer) {
    free(renderer->x_on_line);
    renderer->x_on_line = NULL;
    free(renderer->data);
    renderer->data = NULL;
}
