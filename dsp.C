
/*******************************************************************************/
/* Copyright (C) 2008-2021 Jonathan Moore Liles                                */
/* Copyright (C) 2021- Stazed                                                  */
/*                                                                             */
/*                                                                             */
/* This program is free software; you can redistribute it and/or modify it     */
/* under the terms of the GNU General Public License as published by the       */
/* Free Software Foundation; either version 2 of the License, or (at your      */
/* option) any later version.                                                  */
/*                                                                             */
/* This program is distributed in the hope that it will be useful, but WITHOUT */
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       */
/* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for   */
/* more details.                                                               */
/*                                                                             */
/* You should have received a copy of the GNU General Public License along     */
/* with This program; see the file COPYING.  If not,write to the Free Software */
/* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */
/*******************************************************************************/

/* General DSP related functions. */

#include "dsp.h"
#include "string.h" // for memset.
#include <stdlib.h> 

static const int ALIGNMENT = 16;

#ifdef HAS_BUILTIN_ASSUME_ALIGNED
#define assume_aligned(x) __builtin_assume_aligned(x,ALIGNMENT)
#else
#define assume_aligned(x) (x)
#endif

sample_t *
buffer_alloc ( nframes_t size )
{
    void *p = NULL;
    
    posix_memalign( &p, ALIGNMENT, size * sizeof( sample_t ) );

    return (sample_t*)p;
}

void
buffer_apply_gain ( sample_t * __restrict__ buf, nframes_t nframes, float g )
{
    sample_t * buf_ = (sample_t*) assume_aligned(buf);
	
    if ( g == 1.0f )
        return;

    while ( nframes-- )
	*buf_++ *= g;
}

void
buffer_apply_gain_unaligned ( sample_t * __restrict__ buf, nframes_t nframes, float g )
{
    if ( g == 1.0f )
        return;

    while ( nframes-- )
	*buf++ *= g;
}

void
buffer_apply_gain_buffer ( sample_t * __restrict__ buf, const sample_t * __restrict__ gainbuf, nframes_t nframes )
{
    sample_t * buf_ = (sample_t*) assume_aligned(buf);
    const sample_t * gainbuf_ = (const sample_t*) assume_aligned(gainbuf);

    while ( nframes-- )
	*buf_++ *= *gainbuf_++;
}

void
buffer_copy_and_apply_gain_buffer ( sample_t * __restrict__ dst, const sample_t * __restrict__ src, const sample_t * __restrict__ gainbuf, nframes_t nframes )
{
    sample_t * dst_ = (sample_t*) assume_aligned(dst);
    const sample_t * src_ = (const sample_t*) assume_aligned(src);
    const sample_t * gainbuf_ = (const sample_t*) assume_aligned(gainbuf);

    while ( nframes-- )
	*dst_++ = *src_++ * *gainbuf_++;
}

void
buffer_mix ( sample_t * __restrict__ dst, const sample_t * __restrict__ src, nframes_t nframes )
{
    sample_t * dst_ = (sample_t*) assume_aligned(dst);
    const sample_t * src_ = (const sample_t*) assume_aligned(src);

    while ( nframes-- )
	*dst_++ += *src_++;
}

void
buffer_mix_with_gain ( sample_t * __restrict__ dst, const sample_t * __restrict__ src, nframes_t nframes, float g )
{
    sample_t * dst_ = (sample_t*) assume_aligned(dst);
    const sample_t * src_ = (const sample_t*) assume_aligned(src);

    while ( nframes-- )
	*dst_++ = *src_++ * g;
}

void
buffer_interleave_one_channel ( sample_t * __restrict__ dst, const sample_t * __restrict__ src, int channel, int channels, nframes_t nframes )
{
    dst += channel;

    while ( nframes-- )
    {
        *dst = *(src++);
        dst += channels;
    }
}

void
buffer_interleave_one_channel_and_mix ( sample_t *__restrict__ dst, const sample_t * __restrict__ src, int channel, int channels, nframes_t nframes )
{
    dst += channel;

    while ( nframes-- )
    {
        *dst += *(src++);
        dst += channels;
    }
}

void
buffer_deinterleave_one_channel ( sample_t * __restrict__ dst, const sample_t * __restrict__ src, int channel, int channels, nframes_t nframes )
{
    src += channel;

    while ( nframes-- )
    {
        *(dst++) = *src;
        src += channels;
    }
}

void
buffer_interleaved_mix ( sample_t *__restrict__ dst, const sample_t * __restrict__ src, int dst_channel, int src_channel, int dst_channels, int src_channels, nframes_t nframes )
{
    sample_t * dst_ = (sample_t*) assume_aligned(dst);
    const sample_t * src_ = (const sample_t*) assume_aligned(src);

    dst_ += dst_channel;
    src_ += src_channel;

    while ( nframes-- )
    {
        *dst_ += *src_;
        dst_ += dst_channels;
        src_ += src_channels;
    }
}

void
buffer_interleaved_copy ( sample_t *__restrict__ dst, const sample_t * __restrict__ src, int dst_channel, int src_channel, int dst_channels, int src_channels, nframes_t nframes )
{
    sample_t * dst_ = (sample_t*) assume_aligned(dst);
    const sample_t * src_ = (const sample_t*) assume_aligned(src);

    dst_ += dst_channel;
    src_ += src_channel;

    while ( nframes-- )
    {
        *dst_ = *src_;
        dst_ += dst_channels;
        src_ += src_channels;
    }
}

void
buffer_fill_with_silence ( sample_t *buf, nframes_t nframes )
{
    memset( buf, 0, nframes * sizeof( sample_t ) );
}

bool
buffer_is_digital_black ( const sample_t *buf, nframes_t nframes )
{
    while ( nframes-- )
    {
        if (! *(buf++) )
            continue;

        return false;
    }

    return true;
}

float
buffer_get_peak ( const sample_t * __restrict__ buf, nframes_t nframes )
{
    const sample_t * buf_ = (const sample_t*) assume_aligned(buf);

    float p = 0.0f;
    
    while (nframes--)
    {
	const float v = fabsf( *buf_++ );

	if ( v > p )
	    p = v;
    }

    return p;
}

void
buffer_copy ( sample_t * __restrict__ dst, const sample_t * __restrict__ src, nframes_t nframes )
{
    if ( dst != NULL && src != NULL )
        memcpy( dst, src, nframes * sizeof( sample_t ) );
}

void
buffer_copy_and_apply_gain ( sample_t * __restrict__ dst, const sample_t * __restrict__ src, nframes_t nframes, float gain )
{
    memcpy( dst, src, nframes * sizeof( sample_t ) );
    buffer_apply_gain( dst, nframes, gain );
}


void
Value_Smoothing_Filter::sample_rate ( nframes_t n )
{
    const float FS = n;
    const float T = 0.05f;
   
    w = _cutoff / (FS * T);
}

/* FIXME: need a method that just returns a single value, skipping the within-buffer interpolation */
bool
Value_Smoothing_Filter::apply( sample_t * __restrict__ dst, nframes_t nframes, float gt )
{
    if ( target_reached(gt) )
        return false;

    sample_t * dst_ = (sample_t*) assume_aligned(dst);
    
    const float a = 0.07f;
    const float b = 1 + a;
    
    const float gm = b * gt;

    float g1 = this->g1;
    float g2 = this->g2;

    for (nframes_t i = 0; i < nframes; i++)
    {
        g1 += w * (gm - g1 - a * g2);
        g2 += w * (g1 - g2);
        dst_[i] = g2;
    }

    g2 += 1e-10f;		/* denormal protection */

    if ( fabsf( gt - g2 ) < 0.0001f )
        g2 = gt;

    this->g1 = g1;
    this->g2 = g2;

    return true;
}
