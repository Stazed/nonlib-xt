
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

#include <pthread.h>

class Mutex {
public:
    Mutex()
        {
            pthread_mutexattr_t attr;
            pthread_mutexattr_init( &attr );
            pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
            pthread_mutex_init( &m_mutex, &attr );
            pthread_mutexattr_destroy( &attr );
        }

    virtual ~Mutex()
        {
            pthread_mutex_destroy( &m_mutex );
        }

    void
    lock ( void )
        {
            pthread_mutex_lock( &m_mutex );
        }

    void
    unlock ( void )
        {
            pthread_mutex_unlock( &m_mutex );
        }

    bool
    trylock ( void )
        {
            return pthread_mutex_trylock( &m_mutex ) == 0;
        }

private:
    pthread_mutex_t m_mutex;
};


class Locker
{

    Mutex &_lock;

public:

    explicit Locker ( Mutex & lock ) : _lock( lock )
        {
            _lock.lock();
        }

    ~Locker ( )
        {
            _lock.unlock();
        }
};
