/*
 * GAMS - General Algebraic Modeling System GDX API
 *
 * Copyright (c) 2025 GAMS Software GmbH <support@gams.com>
 * Copyright (c) 2025 GAMS Development Corp. <support@gams.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback suite describing a random-access data source consumable by the GDX loader.
 */
typedef struct gdx_random_access {
    void *user_data; /**< Caller-supplied context pointer passed to every callback. */
    int (*read_at)(void *user_data,
                   uint64_t offset,
                   void *dst,
                   size_t requested,
                   size_t *out_read); /**< Blocking read starting at @p offset. Returns non-zero on success. */
    int (*get_size)(void *user_data,
                    uint64_t *out_size); /**< Query total logical size in bytes. Returns non-zero on success. */
    void (*close)(void *user_data); /**< Optional cleanup hook; may be NULL if the caller keeps ownership. */
} gdx_random_access;

#ifdef __cplusplus
} // extern "C"
#endif
