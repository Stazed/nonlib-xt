
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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

unsigned long
modification_time ( const char *file )
{
    struct stat st;

    if ( stat( file, &st ) )
        return 0;

    return st.st_mtime;
}

/** returns /true/ if /file1/ is newer than /file2/ (or file2 doesn't exist) */
bool
newer ( const char *file1, const char *file2 )
{
    return modification_time( file1 ) > modification_time( file2 );
}

unsigned long
size ( const char *file )
{
    struct stat st;

    if ( stat( file, &st ) )
        return 0;

    return st.st_size;
}

int
exists ( const char *name )
{
    struct stat st;

    return 0 == stat( name, &st );
}

bool
acquire_lock ( int *lockfd, const char *filename )
{
    if ( !lockfd || !filename )
        return false;
 
    struct flock fl;
    memset( &fl, 0, sizeof( fl ) );
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    *lockfd = ::open( filename, O_RDWR | O_CREAT, 0666 );
    if ( *lockfd < 0 )
        return false;

    if ( fcntl( *lockfd, F_SETLK, &fl ) != 0 )
    {
        ::close( *lockfd );
        *lockfd = 0;
        return false;
    }

    return true;
}

void
release_lock ( int *lockfd, const char *filename )
{
    if ( filename )
        unlink( filename );

    if ( lockfd && *lockfd > 0 )
    {
        ::close( *lockfd );
        *lockfd = 0;
    }
}

static void
reverse_buffer( char *s, size_t len )
{
    for ( size_t i = 0, j = len ? len - 1 : 0; i < j; ++i, --j )
    {
        const char c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

int
backwards_fgetc ( FILE *fp )
{
    int c;

    if ( fseek( fp, -1, SEEK_CUR ) != 0 )
        return -1;

    c = fgetc( fp );

    fseek( fp, -1, SEEK_CUR );

    return c;
}

char *
backwards_afgets ( FILE *fp )
{
    size_t size = 0;

    char *s = NULL;
    size_t i = 0;
    int c;
    while ( ( c = backwards_fgetc( fp ) ) >= 0 )
    {
        if ( i > 0 && '\n' == c )
            break;

        if ( i + 1 >= size )
        {
            size += 256;
            char *tmp = (char*)realloc( s, size );

            if ( tmp == NULL )
            {
                printf ("In file.C - backwards_afgets, realloc is NULL\n");
                free( s );
                return NULL;
            }

            s = tmp;
        }

        s[i++] = (char)c;

    }
    
    if ( s )
    {
        s[i] = 0;
        
        reverse_buffer( s, i );
    }

    fseek( fp, 1, SEEK_CUR );

    return s;
}


/** update the modification time of file referred to by /fd/ */
void
touch ( int fd )
{
    struct stat st;

    fstat( fd, &st );

    fchmod( fd, st.st_mode );
}

/** write a single string to a file */
void
write_line ( const char *dir, const char *name, const char *value )
{
    char path[512];

    snprintf( path, sizeof( path ), "%s/%s", dir, name );

    FILE *fp = fopen( path, "w" );

    if ( ! fp )
        return;

    fputs( value, fp );

    fclose( fp );
}

/** write a single string to a file */
char *
read_line ( const char *dir, const char *name  )
{
    char path[512];

    snprintf( path, sizeof( path ), "%s/%s", dir, name );

    FILE *fp = fopen( path, "r" );

    if ( ! fp )
        return 0;

    char *value = (char*)malloc( 512 );

    if ( value == NULL )
    {
        printf("Malloc of value is NULL: file.C read_line\n");
        fclose( fp );
        return NULL;
    }

    value[0] = 0;

    fgets( value, 512, fp );

    fclose( fp );

    return value;
}

/** return the number of blocks free on filesystem containing file named /file/ */
size_t
free_space ( const char *file )
{
    struct statvfs st;
    memset( &st, 0, sizeof( st ) );

    return statvfs( file, &st ) == 0 ? (size_t)st.f_bavail : 0;
}

/** return the total number of blocks on filesystem containing file named /file/ */
size_t
total_space ( const char *file )
{
    struct statvfs st;
    memset( &st, 0, sizeof( st ) );

    return statvfs( file, &st ) == 0 ? (size_t)st.f_blocks : 0;
}

/** return the percentage of usage on filesystem containing file named /file/ */
int
percent_used ( const char *file )
{
    const double ts = total_space( file );
    const double fs = free_space( file );

    if ( ts <= 0.0 )
        return 0;

    double percent_free = ( ( fs / ts ) * 100.0f );

    return (int) (100.0f - percent_free);
}
