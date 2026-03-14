
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

#pragma once

#include "Loggable.H"

#include "types.h"
#include <string>
#include <cstring>

class Log_Entry
{
    char **_sa;
    int _i;

    /* not permitted */
    Log_Entry ( const Log_Entry &rhs );
    Log_Entry & operator= ( const Log_Entry &rhs );

    static char ** parse_alist ( const char *s );
    static char * make_pair ( const char *name, const char *value );

    template <typename... Args>
    void add_formatted ( const char *name, const char *format, Args... args )
        {
            grow();
            if ( !valid() ) return;

            int needed = snprintf( NULL, 0, format, args... );
            if ( needed < 0 )
                return;

            std::string value;
            value.resize( static_cast<size_t>( needed ) );
            snprintf( &value[0], value.size() + 1, format, args... );

            _sa[ _i ] = make_pair( name, value.c_str() );
            if ( !_sa[ _i ] )
                return;

            ++_i;
        }

    static bool log_diff (  char **sa1, char **sa2 );

public:

    Log_Entry ( );
    explicit Log_Entry ( char **sa );
    explicit Log_Entry ( const char *s );
    ~Log_Entry ( );

/****************/
/* Construction */
/****************/

    void grow (  );
    bool valid ( void ) const { return _sa != NULL; }

#define ADD( type, format, exp )                                       \
    void add ( const char *name, type v )                              \
        {                                                              \
            add_formatted( name, format, (exp) );                      \
        }

    void add_raw ( const char *name, const char *v )
        {
            grow();

            if ( !valid() )
                return;

            _sa[ _i ] = make_pair( name, v ? v : "" );
            if ( !_sa[ _i ] )
                return;

            ++_i;
        }

/***************/
/* Examination */
/***************/

    static bool diff ( Log_Entry *e1, Log_Entry *e2 );

    int size ( void ) const;

    void get ( int n, const char **name, const char **value ) const;
    char **sa ( void );

    char *print ( void ) const;

    void remove ( const char *s );

    ADD( int, "%d", v );
    ADD( nframes_t, "%lu", (unsigned long)v );
    ADD( unsigned long, "%lu", v );
    ADD( const char *, "\"%s\"", v ? Loggable::escape( v ) : "" );
    ADD( Loggable * , "0x%X", v ? v->id() : 0 );
    ADD( float, "%f", v );
    ADD( double, "%f", v );

#undef ADD

};
