
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cctype>

static bool
is_hex_digit( char c )
{
    return std::isxdigit( static_cast<unsigned char>( c ) ) != 0;
}

void unescape_url ( char *url )
{
    if ( !url )
        return;

    char *r, *w;

    r = w = url;

    for ( ; *r; r++, w++ )
    {
        if ( *r == '%' &&
             r[1] != '\0' &&
             r[2] != '\0' &&
             is_hex_digit( r[1] ) &&
             is_hex_digit( r[2] ) )
        {
            char data[3] = { *(r + 1), *(r + 2), 0 };

            unsigned int c;

            if ( sscanf( data, "%2X", &c ) == 1 )
            {
                *w = static_cast<char>( c );
                r += 2;
            }
            else
                *w = *r;
        }
        else
            *w = *r;
    }

    *w = 0;
}

char *escape_url ( const char *url )
{
    if ( !url )
        return strdup( "" );

    std::string out;

    for ( const unsigned char *s = reinterpret_cast<const unsigned char *>( url ); *s; ++s )
    {
        switch ( *s )
        {
            case '<':
            case '>':
            case '%':
            /* liblo doesn't like these in method names */
            case '[':
            case ']':
            case '{':
            case '}':
            case '?':
            case ',':
            case '#':
            case '*':
            case ' ':
            {
                char encoded[4];
                snprintf( encoded, sizeof( encoded ), "%%%02X", *s );
                out += encoded;
                break;
            }
            default:
                out += static_cast<char>( *s );
                break;
        }
    }

    char *r = (char*)malloc( out.size() + 1 );
    if ( !r )
        return NULL;

    memcpy( r, out.c_str(), out.size() + 1 );
    return r;
}
