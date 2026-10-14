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
#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <limits.h>

#if UINT_MAX >= 4294967295
typedef unsigned int pixel_t;
#else
typedef unsigned long int pixel_t;
#endif

struct image {
    size_t w, h;
    unsigned long int ppi;
    pixel_t *data;
};

#define IMAGE_ERROR_X(x, l) \
    x(IE_NONE) \
    x(IE_OPEN) \
    x(IE_READ) \
    x(IE_SEEK) \
    x(IE_WRITE) \
    x(IE_SIG) \
    x(IE_MEM) \
    x(IE_UNSUPPORTED) \
    l(IE_UNKNOWN)

#define IMAGE_X_LIST(e) e,
#define IMAGE_X_LIST_END(e) e

enum {
    IMAGE_ERROR_X(IMAGE_X_LIST, IMAGE_X_LIST_END)
};

#ifndef IMAGE_IMPL
#undef IMAGE_ERROR_X
#undef IMAGE_X_LIST
#undef IMAGE_X_LIST_END
#endif


enum {
    IT_BMP
};

#define IMAGE_RGBAINT(r, g, b, a) ((pixel_t)(b)| \
                                  ((pixel_t)(g)<<8)| \
                                  ((pixel_t)(r)<<16)| \
                                  ((pixel_t)(a)<<24))

char *image_get_error_str(int error);

int image_load(struct image *img, char *file);

int image_write(struct image *img, char *file, int type, int flags);

int image_create(struct image *img, size_t width, size_t height, size_t ppi);

void image_free(struct image *img);

#endif
