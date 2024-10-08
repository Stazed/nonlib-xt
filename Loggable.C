
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

/* This class handles all journaling. All journaled objects must
   inherit from Loggable as well as define a few special methods (via
   macros), get and set methods, and have contructors and destructors
   that call log_create() and log_destroy() in the appropriate
   order. Any action that might affect multiple loggable objects
   *must* be braced by calls to Loggable::block_start() and
   Loggable::block_end() in order for Undo to work properly. */

#include "Loggable.H"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "file.h"

// #include "const.h"
#include "debug.h"

#include "Mutex.H"

#include <algorithm>
using std::min;
using std::max;



#ifndef NDEBUG
bool Loggable::_snapshotting = false;
int Loggable::_snapshot_count = 0;
#endif

bool _is_pasting = false;   // Extern - see note in Loggable.H

bool Loggable::_readonly = false;
FILE *Loggable::_fp;
unsigned int Loggable::_log_id = 0;
int Loggable::_level = 0;
int Loggable::_dirty = 0;
off_t Loggable::_undo_offset = 0;

std::map <unsigned int, Loggable::log_pair > Loggable::_loggables;

std::map <std::string, create_func*> Loggable::_class_map;
std::queue <char *> Loggable::_transaction;

progress_func *Loggable::_progress_callback = NULL;
void *Loggable::_progress_callback_arg = NULL;

snapshot_func *Loggable::_snapshot_callback = NULL;
void *Loggable::_snapshot_callback_arg = NULL;

dirty_func *Loggable::_dirty_callback = NULL;
void *Loggable::_dirty_callback_arg = NULL;



static Mutex _lock;

Loggable::~Loggable ( )
{
    Locker lock( _lock );;
    _loggables[ _id ].loggable = NULL;
}



void
Loggable::block_start ( void )
{
    Locker lock( _lock );;
    ++Loggable::_level;
}

void
Loggable::block_end ( void )
{
    Locker lock( _lock );;

    --Loggable::_level;

    ASSERT( Loggable::_level >= 0, "Programming error" );

    if ( Loggable::_level == 0 )
        flush();
}

Loggable *
Loggable::find ( unsigned int id )
{
    if ( _relative_id )
        id += _relative_id;

    return _loggables[ id ].loggable;
}

/** Open the journal /filename/ and replay it, bringing the end state back into RAM */
bool
Loggable::open ( const char *filename )
{
    FILE *fp;

    Loggable::_fp = NULL;

    if ( ! ( fp = fopen( filename, "a+" ) ) )
    {
        WARNING( "Could not open log file for writing!" );
        
        if ( ! ( fp = fopen( filename, "r" ) ) )
        {
            WARNING( "Could not open log file for reading!" );
            return false;
        }
        else
        {
            _readonly = true;
        }
    }
    
    _readonly = false;

    load_unjournaled_state();

    if ( newer( "snapshot", filename ) )
    {
        MESSAGE( "Loading snapshot" );

        FILE *fp = fopen( "snapshot", "r" );

        replay( fp );

        fclose( fp );
    }
    else
    {
        MESSAGE( "Replaying journal" );

        replay( fp );
    }

    fseek( fp, 0, SEEK_END );
    _undo_offset = ftell( fp );

    Loggable::_fp = fp;

    return true;
}

bool
Loggable::load_unjournaled_state ( void )
{
    FILE *fp;

    fp = fopen( "unjournaled", "r" );

    if ( ! fp )
    {
        DWARNING( "Could not open unjournaled state file for reading" );
        return false;
    }

    unsigned int id;
    char *buf;

    while ( fscanf( fp, "%X set %m[^\n]\n", &id, &buf ) == 2 )
    {
        _loggables[ id ].unjournaled_state = new Log_Entry( buf );
        free(buf);
    }

    fclose( fp );

    return true;
}

#include <sys/stat.h>
#include <unistd.h>

/** replay journal or snapshot */
bool
Loggable::replay ( const char *file, bool need_clear )
{
    if ( FILE *fp = fopen( file, "r" ) )
    {
        bool r = replay( fp, need_clear );

        fclose( fp );

        return r;
    }
    else
        return false;
}

/** replay journal or snapshot, or import strip, pasted strip */
bool
Loggable::replay ( FILE *fp, bool need_clear )
{
    /* Set the pasting flag  */
    _is_pasting = true;

    char *buf = NULL;

    struct stat st;
    fstat( fileno( fp ), &st );

    off_t total = st.st_size;
    off_t current = 0;

    if ( _progress_callback )
        _progress_callback( 0, _progress_callback_arg );

    while ( fscanf( fp, "%m[^\n]\n", &buf ) == 1 )
    {
        if ( ! ( ! strcmp( buf, "{" ) || ! strcmp( buf, "}" ) ) )
        {
            if ( *buf == '\t' )
                do_this( buf + 1, false );
            else
                do_this( buf, false );
        }

        free(buf);

        current = ftell( fp );

        if ( _progress_callback )
            _progress_callback( current * 100 / total, _progress_callback_arg );
    }

    if ( _progress_callback )
        _progress_callback( 0, _progress_callback_arg );

    /* Import strip and paste strip should not clear dirty */
    if( need_clear )
        clear_dirty();

    /* Unset the pasting flag since we are done */
    _is_pasting = false;
    
    return true;
}

/** close journal and delete all loggable objects, returing the systemt to a blank slate */
bool
Loggable::close ( void )
{
    DMESSAGE( "closing journal and destroying all journaled objects" );

    if ( _fp )
    {
        fclose( _fp );
        _fp = NULL;
    }

    std::string full_path = project_directory;
    full_path += "/snapshot";

    if ( ! snapshot( full_path.c_str() ) )
        WARNING( "Failed to create snapshot" );

    if ( ! save_unjournaled_state() )
        WARNING( "Failed to save unjournaled state" );

    for ( std::map <unsigned int, Loggable::log_pair >::iterator i = _loggables.begin();
          i != _loggables.end(); ++i )
    {
        if ( i->second.loggable )
            delete i->second.loggable;
        if ( i->second.unjournaled_state )
            delete i->second.unjournaled_state;
    }

    _loggables.clear();

    return true;
}


/** save out unjournaled state for all loggables */
bool
Loggable::save_unjournaled_state ( void )
{
    FILE *fp;

    fp = fopen( "unjournaled", "w" );

    if ( ! fp )
    {
        DWARNING( "Could not open unjournaled state file for writing!" );
        return false;
    }

    for ( std::map <unsigned int, Loggable::log_pair >::iterator i = _loggables.begin();
          i != _loggables.end(); ++i )
    {
        /* get the latest state */
        if ( i->second.loggable )
            i->second.loggable->record_unjournaled();

        if ( i->second.unjournaled_state )
        {
            char *s = i->second.unjournaled_state->print();

            fprintf( fp, "0x%X set %s\n", i->first, s );

            free( s );
        }
    }

    fclose( fp );

    return true;
}

/** must be called after construction in create() methods */
void
Loggable::update_id ( unsigned int id )
{
    /* make sure we're the last one */
    ASSERT( _id == _log_id, "%u != %u", _id, _log_id );
    assert( _loggables[ _id ].loggable == this );

    _loggables[ _id ].loggable = NULL;

    _log_id = max( _log_id, id );

    /* return this id number to the system */
//            --_log_id;

    _id = id;

    if ( _loggables[ _id ].loggable )
        FATAL( "Attempt to create object with an ID (0x%X) that already exists. The existing object is of type \"%s\", the new one is \"%s\". Corrupt journal?", _id, _loggables[ _id ].loggable->class_name(), class_name() );

    _loggables[ _id ].loggable = this;
}

/** return a pointer to a static copy of /s/ with all special characters escaped */
const char *
Loggable::escape ( const char *s )
{
    static size_t rl = 256;
    static char *r = new char[rl + 1];

    if ( strlen(s) * 2 > rl )
    {
	delete []r;
	rl = strlen(s) * 2;
	r = new char[ rl + 1 ];
    }

    size_t i = 0;
    for ( ; *s && i < rl; ++i, ++s )
    {
        if ( '\n' == *s )
        {
            r[ i++ ] = '\\';
            r[ i ] = 'n';
        }
        else if ( '"' == *s )
        {
            r[ i++ ] = '\\';
            r[ i ] = '"';
        }
        else
            r[ i ] = *s;
    }

    r[ i ] = '\0';

    return r;
}

unsigned int Loggable::_relative_id = 0;

/* calls to do_this() between invocation of this method and
 * end_relative_id_mode() will have all their IDs made relative to the
 * highest available ID at this time of this call. Non-Mixer uses
 * this to allow importing of module chains */
void
Loggable::begin_relative_id_mode ( void )
{
    _relative_id = ++_log_id;
}

void
Loggable::end_relative_id_mode ( void )
{
    _relative_id = 0;
}


/** 'do' a message like "Audio_Region 0xF1 set :r 123" */
bool
Loggable::do_this ( const char *s, bool reverse )
{
    unsigned int id = 0;

    char classname[40];
    char command[40];
    char *arguments = NULL;

    int found = sscanf( s, "%s %X %s ", classname, &id, command );

    if ( 3 != found )
        FATAL( "Invalid journal entry format \"%s\"", s );

    const char *create, *destroy;

    if ( reverse )
    {
//        sscanf( s, "%s %*X %s %*[^\n<]<< %m[^\n]", classname, command, &arguments );
        sscanf( s, "%s %*X %s%*[^\n<]<< %m[^\n]", classname, command, &arguments );
        create = "destroy";
        destroy = "create";

        DMESSAGE( "undoing \"%s\"", s );
    }
    else
    {
        sscanf( s, "%s %*X %s %m[^\n<]", classname, command, &arguments );
        create = "create";
        destroy = "destroy";
    }

    if ( ! strcmp( command, destroy ) )
    {
        Loggable *l = find( id );

        /* deleting eg. a track, which contains a list of other
           widgets, causes destroy messages to be emitted for all those
           widgets, but when replaying the journal the destroy message
           causes the children to be deleted also... This is a temporary
           hack. Would it be better to queue up objects for deletion
           (when?) */
        if ( l )
            delete l;
    }
    else if ( ! strcmp( command, "set" ) )
    {
//        printf( "got set command (%s).\n", arguments );

        Loggable *l = find( id );

        ASSERT( l, "Unable to find object 0x%X referenced by command \"%s\"", id, s );

        Log_Entry e( arguments );

        l->log_start();
        l->set( e );
        l->log_end();
    }
    else if ( ! strcmp( command, create ) )
    {
        Log_Entry e( arguments );

        ASSERT( _class_map[ std::string( classname ) ], "Journal contains an object of class \"%s\", but I don't know how to create such objects.", classname );

        {
            if ( _relative_id )
                id += _relative_id;

            /* create */
            Loggable *l = _class_map[ std::string( classname ) ]( e, id );
            l->log_create();

            /* we're now creating a loggable. Apply any unjournaled
             * state it may have had in the past under this log ID */

            Log_Entry *e = _loggables[ id ].unjournaled_state;

            if ( e )
                l->set( *e );
        }

    }

    if ( arguments )
        free( arguments );

    return true;
}

/** Reverse the last journal transaction */
void
Loggable::undo ( void )
{
    if ( ! _fp ||                                               /* journal not open */
         1 == _undo_offset )                                    /* nothing left to undo */
        return;

    char *buf;

    block_start();

    long here = ftell( _fp );

    fseek( _fp, _undo_offset, SEEK_SET );

    if ( ( buf = backwards_afgets( _fp ) ) )
    {
        if ( ! strcmp( buf, "}\n" ) )
        {
            free( buf );
                
            DMESSAGE( "undoing block" );
            for ( ;; )
            {
                if ( ( buf = backwards_afgets( _fp ) ) )
                {
                    char *s = buf;
                    if ( *s != '\t' )
                    {
                        DMESSAGE( "done with block", s );

                        break;
                    }
                    else
                        ++s;
                    
                    do_this( s, true );

                    free( buf );
                }
            }
        }
        else
        {
            do_this( buf, true );

            free( buf );
        }
    }

    off_t uo = ftell( _fp );

    ASSERT( _undo_offset <= here, "WTF?" );
    
    block_end();

    _undo_offset = uo;
    
}

/** write a snapshot of the current state of all loggable objects to
 * file handle /fp/ */
bool
Loggable::snapshot ( FILE *fp )
{
    FILE *ofp = _fp;

    if ( ! Loggable::_snapshot_callback )
    {
        DWARNING( "No snapshot callback defined" );
        return false;
    }

    if ( ! ( _fp = fp ) )
    {
        _fp = ofp;
        return false;
    }

#ifndef NDEBUG
    _snapshotting = true;

    _snapshot_count++;
#endif

    block_start();

    Loggable::_snapshot_callback( _snapshot_callback_arg );

    block_end();

#ifndef NDEBUG
    _snapshotting = false;
#endif

    _fp = ofp;

    clear_dirty();

    return true;
}

/** write a snapshot of the current state of all loggable objects to
 * file /name/ */
bool
Loggable::snapshot ( const char *name )
{
    FILE *fp;

    char *tmp  = NULL;

    {
        const char *filename = basename(name);
        char *dir = (char*)malloc( (strlen(name) - strlen(filename)) + 1 );

_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wstringop-overflow=\"")
        strncpy( dir, name, strlen(name) - strlen(filename) );
_Pragma("GCC diagnostic pop")

        dir[(strlen(name) - strlen(filename))] = '\0';  // make sure to null terminate
        /* Create tmp file with '#' in front of the file name -
         to be later renamed if all goes well */
        asprintf( &tmp, "%s#%s", dir, filename );
        free(dir);
    }

    if ( ! ( fp = fopen( tmp, "w" ) ))
    {
        DWARNING( "Could not open file for writing: %s", tmp );
        return false;
    }

    bool r = snapshot( fp );

    fclose( fp );

    /* Do not rename the temp file and clobber existing file if something went wrong */
    if(r)
    {
        /* Looks like all went well, so rename the temp file to the correct name */
        rename( tmp, name );
    }

    free(tmp);

    return r;
}

/** Replace the journal with a snapshot of the current state */
void
Loggable::compact ( void )
{
    fseek( _fp, 0, SEEK_SET );
    ftruncate( fileno( _fp ), 0 );

    if ( ! snapshot( _fp ) )
        FATAL( "Could not write snapshot!" );

    fseek( _fp, 0, SEEK_END );
}

#include <stdarg.h>

/** Writes (part of) a line to the journal. Each separate line will be
 * stored separately in _transaction until transaction is closed.
 */
void
Loggable::log ( const char *fmt, ... )
{
    Locker lock( _lock );

    static char * buf = NULL;
    static size_t i = 0;
    static size_t buf_size = 0;

    if ( ! _fp )
        return;

    if ( NULL == buf )
    {
        /* We limit parameters to 100 for LV2, was 1024. > 100 use custom data */
        buf_size = 4096;
        buf = (char*)malloc( buf_size );
    }

    va_list args;

    if ( fmt )
    {
        va_start( args, fmt );

        for ( ;; )
        {
            size_t l = vsnprintf( buf + i, buf_size - i, fmt, args );

            if ( l >= buf_size - i )
            {
                buf = (char*)realloc( buf, buf_size += (l + 1) + buf_size );
            }
            else
            {
                i += l;
                break;
            }
        }

        va_end( args );
    }

    if ( '\n' == buf[i-1] )
    {
       // DMESSAGE("log buf transaction push = %s", buf);
        _transaction.push( strdup( buf ) );
        i = 0;
    }
}

/** End the current transaction and commit it to the journal */
void
Loggable::flush ( void )
{
    if ( ! _fp )
    {
//        printf( "error: no log file open!\n" );

        while ( ! _transaction.empty() )
        {
            free( _transaction.front() );
            _transaction.pop();
        }

        return;
    }

    int n = _transaction.size();

    if ( n > 1 )
        fprintf( _fp, "{\n" );

    while ( ! _transaction.empty() )
    {
        char *s = _transaction.front();

      //  printf("_transaction.front s = %s\n", s);

        _transaction.pop();

        if ( n > 1 )
            fprintf( _fp, "\t" );

        fprintf( _fp, "%s", s );

        free( s );
    }

    if ( n > 1 )
        fprintf( _fp, "}\n" );

    if ( n )
        /* something done, reset undo index */
        _undo_offset = ftell( _fp );

    fflush( _fp );
}

/** Print bidirectional journal entry */
void
Loggable::log_print( const Log_Entry *o, const Log_Entry *n ) const
{
    if ( ! _fp )
        return;

    if ( n )
        for ( int i = 0; i < n->size(); ++i )
        {
            const char *s, *v;

            n->get( i, &s, &v );

            log( "%s %s%s", s, v, n->size() == i + 1 ? "" : " " );
        }

    if ( o && o->size() )
    {
        if ( n ) log( " << " );

        for ( int i = 0; i < o->size(); ++i )
        {
            const char *s, *v;

            o->get( i, &s, &v );

            log( "%s %s%s", s, v, o->size() == i + 1 ? "" : " " );
        }
    }

    log( "\n" );
}

/** Remember current object state for later comparison. *Must* be
 * called before any user action that might change one of the object's
 * journaled properties.  */
void
Loggable::log_start ( void )
{
    Locker lock( _lock );;

    if ( ! _old_state )
    {
        _old_state = new Log_Entry;

        get( *_old_state );
    }
    ++_nest;
}

/** Log any change to the object's state since log_start(). */
void
Loggable::log_end ( void )
{
    Locker lock( _lock );

    ASSERT( _old_state, "Programming error: log_end() called before log_start()" );

    if ( --_nest > 0 )
        return;

    Log_Entry *new_state;

    new_state = new Log_Entry;

    get( *new_state );

    if ( Log_Entry::diff( _old_state, new_state ) )
    {
        log( "%s 0x%X set ", class_name(), _id );

        log_print( _old_state, new_state );

        set_dirty();
    }

    delete new_state;
    delete _old_state;

    _old_state = NULL;

    if ( Loggable::_level == 0 )
        Loggable::flush();
}

/** Log object creation. *Must* be called at the end of all public
 * constructors for leaf classes  */

void
Loggable::log_create ( void ) const
{
    Locker lock( _lock );;

    set_dirty();

    if ( ! _fp )
        /* replaying, don't bother */
        return;

#ifndef NDEBUG
    if ( _snapshotting && _snapshot_count != _num_snapshot )
    {
        _num_snapshot_creates = 1;
        _num_snapshot = _snapshot_count;
    }
    else if ( _snapshotting && _snapshot_count == _num_snapshot )
    {
        _num_snapshot_creates++;

        ASSERT( _num_snapshot_creates < 2, "Attempt to log creation of same object twice in one snapshot! %s", class_name() );
    }
    else
    {
        _num_log_creates++;
        ASSERT( _num_log_creates < 2, "Attempt to log creation of same object twice in the journal! %s", class_name() );
    }
#endif

    log( "%s 0x%X create ", class_name(), _id );

    Log_Entry e;

    get( e );

    if ( e.size() )
        log_print( NULL, &e );
    else
        log( "\n" );

    if ( Loggable::_level == 0 )
        Loggable::flush();
}

/** record this loggable's unjournaled state in memory */
void
Loggable::record_unjournaled ( void ) const
{
    Log_Entry *e = new Log_Entry();

    get_unjournaled( *e );

    Log_Entry **le = &_loggables[ _id ].unjournaled_state;

    if ( *le )
    {
        delete *le;
        *le = NULL;
    }

    if ( e->size() )
        *le = e;
    else
        delete e;
}

/** Log object destruction. *Must* be called at the beginning of the
 * destructors of leaf classes */
void
Loggable::log_destroy ( void ) const
{
    Locker lock( _lock );;

    set_dirty();

    if ( ! _fp )
        /* tearing down... don't bother */
        return;

    /* the unjournaled state may have changed: make a note of it. */
    record_unjournaled();

    log( "%s 0x%X destroy << ", class_name(), _id );

    Log_Entry e;

    get( e );

    log_print( NULL, &e );

    if ( Loggable::_level == 0 )
        Loggable::flush();
}
