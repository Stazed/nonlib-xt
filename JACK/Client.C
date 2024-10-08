
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

#include "Client.H"
#include "Port.H"

#include <algorithm>



#include "../nonlib/debug.h"

#ifdef __SSE2_MATH__
#include <xmmintrin.h>
#endif

bool stop_process = false;  // extern

namespace JACK
{

//    nframes_t Client::_sample_rate = 0;

    Client::Client ( )
    {
        _active = false;
        _freewheeling = false;
        _zombified = false;
        _client = NULL;
        _xruns = 0;
    }

    Client::~Client ( )
    {
        close();
    }

    /** Tell JACK to stop calling process callback. This MUST be called in
     * an inheriting class' destructor */
    void
    Client::deactivate ( )
    {
        if ( _active )
            jack_deactivate( _client );

        _active = false;
    }
    

/*******************/
/* Static Wrappers */
/*******************/

    int
    Client::process ( nframes_t nframes, void *arg )
    {
        if ( stop_process )
            return 0;

        Client *c = (Client*)arg;
     
        if ( ! c->_frozen.trylock() )
            return 0;
        
        int r = c->process(nframes);

        c->_frozen.unlock();

        return r;
    }

    int
    Client::sync ( jack_transport_state_t state, jack_position_t *pos, void *arg )
    {
        return ((Client*)arg)->sync( state, pos );
    }

    int
    Client::xrun ( void *arg )
    {
        ++((Client*)arg)->_xruns;
        return ((Client*)arg)->xrun();
    }

    void
    Client::timebase ( jack_transport_state_t state, jack_nframes_t nframes, jack_position_t *pos, int new_pos, void *arg )
    {
        ((Client*)arg)->timebase( state, nframes, pos, new_pos );
    }

    void
    Client::freewheel ( int starting, void *arg )
    {
        ((Client*)arg)->_freewheeling = starting;
        ((Client*)arg)->freewheel( starting );
    }

    int
    Client::buffer_size ( nframes_t nframes, void *arg )
    {
        return ((Client*)arg)->buffer_size( nframes );
    }

    void
    Client::thread_init ( void *arg )
    {
#ifdef __SSE2_MATH__
    /* set FTZ and DAZ flags */
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif

        ((Client*)arg)->thread_init();
    }

    void
    Client::latency ( jack_latency_callback_mode_t mode, void *arg )
    {
        if ( stop_process )
            return;

        ((Client*)arg)->latency( mode );
    }

    void
    Client::shutdown ( void *arg )
    {
        ((Client*)arg)->_zombified = true;
        ((Client*)arg)->shutdown();
    }

    void
    Client::port_connect ( jack_port_id_t a, jack_port_id_t b, int connect, void *arg )
    {
        Client *c = (Client*)arg;
        if  (! c->_frozen.trylock() )
            return;

        ((Client*)arg)->port_connect( a, b, connect );
        
        c->_frozen.unlock();
    }

    int
    Client::sample_rate_changed ( nframes_t srate, void *arg )
    {
//        ((Client*)arg)->_sample_rate = srate;
        return ((Client*)arg)->sample_rate_changed( srate );
    }

    
    void
    Client::activate ( void )
    {
        jack_activate( _client );
        _active = true;
    }

/** Connect to JACK using client name /client_name/. Return a static
 * pointer to actual name as reported by JACK */
    const char *
    Client::init ( const char *client_name, unsigned int opts )
    {
        if (( _client = jack_client_open ( client_name, (jack_options_t)0, NULL )) == 0 )
            return NULL;

#define set_callback( name ) jack_set_ ## name ## _callback( _client, &Client:: name , this )

        set_callback( thread_init );
        set_callback( process );
        set_callback( xrun );
        set_callback( freewheel );
        set_callback( buffer_size );
        set_callback( port_connect );

        jack_set_sample_rate_callback( _client, &Client::sample_rate_changed, this );
  
#ifdef HAVE_JACK_PORT_GET_LATENCY_RANGE
        set_callback( latency );
#endif

        /* FIXME: should we wait to register this until after the project
           has been loaded (and we have disk threads running)? */
        if ( opts & SLOW_SYNC )
            set_callback( sync );

        if ( opts & TIMEBASE_MASTER )
            jack_set_timebase_callback( _client, 0, &Client::timebase, this );

        jack_on_shutdown( _client, &Client::shutdown, this );

        activate();

//        _sample_rate = frame_rate();

        return jack_get_client_name( _client );
    }

/* THREAD: RT */
/** enter or leave freehweeling mode */
    void
    Client::freewheeling ( bool yes )
    {
        if ( jack_set_freewheel( _client, yes ) )
        {
            ;
//            WARNING( "Unkown error while setting freewheeling mode" );
        }
    }
    
    const char *
    Client::jack_name ( void ) const
    {
        return jack_get_client_name( _client );
    }

    void
    Client::port_added ( Port *p )
    {
        std::list < JACK::Port * >::iterator i = std::find( _active_ports.begin(), _active_ports.end(), p );

        if ( i != _active_ports.end() )
            return;

        _active_ports.push_back( p );
    }

    void
    Client::port_removed ( Port *p )
    {
        _active_ports.remove( p );
    }


    void
    Client::freeze_ports ( void )
    {
        for ( std::list < JACK::Port * >::iterator i = _active_ports.begin();
              i != _active_ports.end();
              ++i )
        {
            (*i)->freeze();
        }
    }

    void
    Client::thaw_ports ( void )
    {
        /* Sort ports for the sake of clients (e.g. patchage), for
         * whom the order of creation may matter (for display) */

        _active_ports.sort();

        for ( std::list < JACK::Port * >::iterator i = _active_ports.begin();
              i != _active_ports.end();
              ++i )
        {
            (*i)->thaw();
        }
    }

    void
    Client::close ( void )
    {
        deactivate();
        
        if ( _client )
        {
            DMESSAGE( "Closing JACK client" );
            jack_client_close( _client );
        }

        _client = NULL;
    }

    const char *
    Client::name ( const char *s )
    {
        /* Because the JACK API does not provide a mechanism for renaming
         * clients, we have to save connections, destroy our client,
         * create a client with the new name, and restore our
         * connections. Lovely. */


        freeze_ports();

        jack_deactivate( _client );
        
        jack_client_close( _client );

        _client = NULL;

        _frozen.lock();

        s = init( s );

        _frozen.unlock();

        thaw_ports();

        return s;
    }

    void
    Client::recompute_latencies ( void )
    {
        jack_recompute_total_latencies( _client );
    }

    void
    Client::transport_stop ( )
    {
        jack_transport_stop( _client );
    }

    void
    Client::transport_start ( )
    {
        jack_transport_start( _client );
    }

    void
    Client::transport_locate ( nframes_t frame )
    {
        jack_transport_locate( _client, frame );
    }

    jack_transport_state_t
    Client::transport_query ( jack_position_t *pos )
    {
        return jack_transport_query( _client, pos );
    }
}
