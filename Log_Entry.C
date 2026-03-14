
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

#include "Log_Entry.H"

#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "debug.h"

namespace
{

static void
unescape_in_place( std::string &s )
{
    std::string out;
    out.reserve( s.size() );

    for ( std::string::size_type i = 0; i < s.size(); ++i )
    {
        if ( s[i] == '\\' && i + 1 < s.size() )
        {
            ++i;
            switch ( s[i] )
            {
                case 'n':
                    out += '\n';
                    break;
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                default:
                    out += s[i];
                    break;
            }
        }
        else
            out += s[i];
    }

    s.swap( out );
}

static std::string
trim_trailing_spaces( const std::string &s )
{
    std::string::size_type end = s.size();

    while ( end > 0 && s[ end - 1 ] == ' ' )
        --end;

    return s.substr( 0, end );
}

static bool
parse_one_pair( const char *&p, std::string &name, std::string &value )
{
    name.clear();
    value.clear();

    while ( *p == ' ' )
        ++p;

    if ( *p == '\0' )
        return false;

    if ( *p != ':' )
        return false;

    const char *name_start = p;
    while ( *p && *p != ' ' )
        ++p;

    name.assign( name_start, p - name_start );

    while ( *p == ' ' )
        ++p;

    if ( *p == '"' )
    {
        ++p;
        std::string raw;

        while ( *p )
        {
            if ( *p == '\\' && p[1] )
            {
                raw += *p++;
                raw += *p++;
                continue;
            }

            if ( *p == '"' )
            {
                ++p;
                break;
            }

            raw += *p++;
        }

        unescape_in_place( raw );
        value = raw;
    }
    else
    {
        const char *value_start = p;
        while ( *p )
        {
            if ( *p == ' ' )
            {
                const char *q = p;
                while ( *q == ' ' )
                    ++q;

                if ( *q == ':' )
                    break;
            }

            ++p;
        }

        value.assign( value_start, p - value_start );
        value = trim_trailing_spaces( value );
        unescape_in_place( value );
    }

    return true;
}

} /* namespace */

Log_Entry::Log_Entry ( )
{
    _sa = (char**)malloc( sizeof( char * ) );
    _i = 0;

    if ( _sa == NULL )
    {
        WARNING ("Malloc of Log_Entry is NULL");
        return;
    }

    *_sa = NULL;
}

Log_Entry::Log_Entry ( char **sa )
{
    _sa = sa;
    _i = 0;

    if ( _sa )
        while ( _sa[ _i ] ) ++_i;

}

Log_Entry::Log_Entry ( const char *s )
{
    _i = 0;
    _sa = s ? parse_alist( s ) : NULL;

    if ( _sa )
        while ( _sa[ _i ] ) ++_i;
}

Log_Entry::~Log_Entry ( )
{
    if ( ! _sa )
        return;

    for ( _i = 0; _sa[ _i ]; ++_i )
    {
        free( _sa[ _i ] );
    }

    free( _sa );
}

/** return a dynamically allocated string representing this log entry */
char *
Log_Entry::print ( void ) const
{
    std::string out;

    for ( int i = 0; i < size(); ++i )
    {
        const char *s, *v;

        get( i, &s, &v );

        out += s;
        out += ' ';
        out += v;
        if ( size() != i + 1 )
            out += ' ';
    }

    char *r = (char*)malloc( out.size() + 1 );
    if ( r == NULL )
    {
        WARNING ("Malloc of print is NULL");
        return NULL;
    }

    memcpy( r, out.c_str(), out.size() + 1 );
    return r;
}

/** parse a string of ":name value :name value" pairs into an
 * array of strings, one per pair */
char **
Log_Entry::parse_alist( const char *s )
{
    if ( !s || !*s )
    {
        char **r = (char**)malloc( sizeof( char* ) );
        if ( !r )
        {
            WARNING ("Malloc of parse_alist is NULL");
            return NULL;
        }
        r[0] = NULL;
        return r;
    }

    std::vector<char*> pairs;
    const char *p = s;

    while ( *p )
    {
        std::string name;
        std::string value;

        if ( !parse_one_pair( p, name, value ) )
            break;
 
        char *pair = make_pair( name.c_str(), value.c_str() );
        if ( !pair )
        {
            WARNING ("Malloc of pair is NULL");
            for ( size_t i = 0; i < pairs.size(); ++i )
                free( pairs[i] );
            return NULL;
        }

        pairs.push_back( pair );
    }

    char **r = (char**)malloc( sizeof( char* ) * ( pairs.size() + 1 ) );
    if ( !r )
    {
        WARNING ("Malloc of parse_alist is NULL");
        for ( size_t i = 0; i < pairs.size(); ++i )
            free( pairs[i] );
        return NULL;
    }

    for ( size_t i = 0; i < pairs.size(); ++i )
        r[i] = pairs[i];

    r[ pairs.size() ] = NULL;
 
    return r;
}

/** compare elements of dumps s1 and s2, removing those elements
    of dst which are not changed from src */
bool
Log_Entry::diff ( Log_Entry *e1, Log_Entry *e2 )
{

    if ( ! e1 )
        return true;

    char **sa1 = e1->_sa;
    char **sa2 = e2->_sa;

    if ( ! sa1 )
        return true;

    int w = 0;
    for ( int i = 0; sa1[ i ]; ++i )
    {
        const char *v1 = sa1[ i ] + strlen( sa1[ i ] ) + 1;
        const char *v2 = sa2[ i ] + strlen( sa2[ i ] ) + 1;

        if ( ! strcmp( sa1[ i ], sa2[ i ] ) && ! strcmp( v1, v2 ) )
        {
            free( sa2[ i ] );
            free( sa1[ i ] );
        }
        else
        {
            sa2[ w ] = sa2[ i ];
            sa1[ w ] = sa1[ i ];

            w++;
        }
    }

    sa1[ w ] = NULL;
    sa2[ w ] = NULL;

    e1->_i = w;
    e2->_i = w;

    return w == 0 ? false : true;
}

char *
Log_Entry::make_pair( const char *name, const char *value )
{
    if ( !name )
        name = "";

    if ( !value )
        value = "";

    const size_t name_len = strlen( name );
    const size_t value_len = strlen( value );

    char *pair = (char*)malloc( name_len + 1 + value_len + 1 );
    if ( !pair )
    {
        WARNING ("Malloc of pair is NULL");
        return NULL;
    }

    memcpy( pair, name, name_len );
    pair[ name_len ] = '\0';
    memcpy( pair + name_len + 1, value, value_len + 1 );

    return pair;
}

void
Log_Entry::grow (  )
{
    char **tmp = (char**)realloc( _sa, sizeof( char * ) * (_i + 2) );

    if ( tmp == NULL )
    {
        WARNING ("Malloc of grow is NULL");
        return;
    }

    _sa = tmp;
    _sa[ _i + 1 ] = NULL;
}

int
Log_Entry::size ( void ) const
{
    return _i;
}

void
Log_Entry::get ( int n, const char **name, const char **value ) const
{
    *name = _sa[ n ];
    *value = *name + strlen( *name ) + 1;
}


void
Log_Entry::remove ( const char *name )
{
    for ( int i = 0; i < _i; i++ )
    {
        if ( _sa[i] && !strcmp( _sa[ i ], name ) )
        {
            free( _sa[i] );
            _sa[i] = NULL;
        }
    }
}

char **
Log_Entry::sa ( void )
{
    return _sa;
}
