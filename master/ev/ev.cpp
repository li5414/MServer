#include <fcntl.h>

#include "ev.h"
#include "ev_watcher.h"

#define EV_CHUNK    2048
#define MIN_TIMEJUMP  1. /* minimum timejump that gets detected (if monotonic clock available) */
#define MAX_BLOCKTIME 59.743 /* never wait longer than this time (to detect time jumps) */
#define array_resize(type,base,cur,cnt,init)    \
    if ( expect_false((cnt) > (cur)) )          \
    {                                           \
        uint32 size = cur;                      \
        while ( size < cnt )                    \
        {                                       \
            size *= 2;                          \
        }                                       \
        type *tmp = (type *)new type[size];     \
        init( tmp,sizeof(type)*size );          \
        memcpy( base,tmp,sizeof(type)*cur );    \
        delete []base;                          \
        base = tmp;                             \
        cur = size;                             \
    }
    
#define EMPTY(base,size)
#define array_zero(base,size)    \
    memset ((void *)(base), 0, sizeof (*(base)) * (size))

ev_loop::ev_loop()
{
    anfds = NULL;
    anfdmax = 0;
    anfdcnt = 0;
    
    pendings = NULL;
    pendingmax = 0;
    pendingcnt = 0;
    
    fdchanges = NULL;
    fdchangemax = 0;
    fdchangecnt = 0;
}

ev_loop::~ev_loop()
{
    if ( anfds )
    {
        delete []anfds;
        anfds = NULL;
    }
    
    if ( pendings )
    {
        delete []pendings;
        pendings = NULL;
    }
    
    if ( fdchanges )
    {
        delete []fdchanges;
        fdchanges = NULL;
    }
    
    if ( backend_fd >= 0 )
    {
        ::close( backend_fd );
        backend_fd = -1;
    }
}

void ev_loop::init()
{
    assert( "loop duplicate init",!anfds && !pendings && !fdchanges );

    anfds = new ANFD[EV_CHUNK];
    anfdmax = EV_CHUNK;
    memset( anfds,0,sizeof(ANFD)*anfdmax );
    
    pendings = new ANPENDING[EV_CHUNK];
    pendingmax = EV_CHUNK;
    
    fdchanges = new ANCHANGE[EV_CHUNK];
    fdchangemax = EV_CHUNK;
    
    ev_rt_now          = get_time ();
    mn_now             = get_clock ();
    now_floor          = mn_now;
    rtmn_diff          = ev_rt_now - mn_now;
  
    backend_fd         = -1;
    
    backend_init();
}

void ev_loop::run()
{
    assert( "backend uninit",backend_fd >= 0 );

    loop_done = false;
    while ( !loop_done )
    {
        fd_reify();/* update fd-related kernel structures */
        
        /* calculate blocking time */
        {
            ev_tstamp waittime  = 0.;
            
            
            /* update time to cancel out callback processing overhead */
            time_update ();
            
            waittime = MAX_BLOCKTIME;
            
            //if (timercnt) /* 如果有定时器，睡眠时间不超过定时器触发时间，以免睡过头 */
            //{
            //    ev_tstamp to = ANHE_at (timers [HEAP0]) - mn_now;
            //    if (waittime > to) waittime = to;
            //}
    
            /* at this point, we NEED to wait, so we have to ensure */
            /* to pass a minimum nonzero value to the backend */
            if (expect_false (waittime < backend_mintime))
                waittime = backend_mintime;

            backend_poll ( waittime );

            /* update ev_rt_now, do magic */
            time_update ();
        }

        /* queue pending timers and reschedule them */
        //timers_reify (); /* relative timers called last */
  
        invoke_pending ();
    }    /* while */
}

void ev_loop::done()
{
    loop_done = true;
}

void ev_loop::io_start( ev_io *w )
{
    assert( "loop uninit",anfdmax|pendingmax|fdchangemax );

    int32 fd = w->fd;

    array_resize( ANFD,anfds,anfdmax,uint32(fd + 1),array_zero );

    ANFD *anfd = anfds + fd;

    /* 与原libev不同，现在同一个fd只能有一个watcher */
    assert( "duplicate fd",!(anfd->w) );

    anfd->w = w;
    anfd->reify = anfd->emask ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

    fd_change( fd );
}

void ev_loop::io_stop( ev_io *w )
{
    clear_pending( w );
    
    if ( expect_false(!w->is_active()) )
        return;
        
    int32 fd = w->fd;
    assert( "illegal fd (must stay constant after start!)", fd >= 0 && uint32(fd) < anfdmax );
    
    ANFD *anfd = anfds + fd;
    anfd->w = 0;
    anfd->reify = anfd->emask ? EPOLL_CTL_DEL : 0;
    
    fd_change( fd );
}

void ev_loop::fd_change( int32 fd )
{
    ++fdchangecnt;
    array_resize( ANCHANGE,fdchanges,fdchangemax,fdchangecnt,EMPTY );
    fdchanges [fdchangecnt - 1] = fd;
}

void ev_loop::fd_reify()
{
    for ( uint32 i = 0;i < fdchangecnt;i ++ )
    {
        int32 fd     = fdchanges[i];
        ANFD *anfd   = anfds + fd;

        switch ( anfd->reify )
        {
        case 0             : /* 一个fd在fd_reify之前start,再stop会出现这种情况 */
            ERROR("please avoid this fd situation,control your watcher");
            continue;
            break;
        case EPOLL_CTL_ADD :
        case EPOLL_CTL_MOD :
        {
            int32 events = (anfd->w)->events;
            /* 允许一个fd在一次loop中不断地del，再add，或者多次mod。
               但只要event不变，则不需要更改epoll */
            if ( anfd->emask == events ) /* no reification */
                continue;
            backend_modify( fd,events,anfd );
            anfd->emask = events;
        }break;
        case EPOLL_CTL_DEL :
            /* 此时watcher可能已被delete */
            backend_modify( fd,0,anfd );
            anfd->emask = 0;
            break;
        default :
            assert( "unknow epoll modify reification",false );
            return;
        }
    }
    
    fdchangecnt = 0;
}

void ev_loop::backend_modify( int32 fd,int32 events,ANFD *anfd )
{
    struct epoll_event ev;
    
    ev.data.fd = fd;
    ev.events  = (events & EV_READ  ? EPOLLIN  : 0)
               | (events & EV_WRITE ? EPOLLOUT : 0);
    /* pre-2.6.9 kernels require a non-null pointer with EPOLL_CTL_DEL, */
    /* The default behavior for epoll is Level Triggered. */
    /* LT同时支持block和no-block，持续通知 */
    /* ET只支持no-block，一个事件只通知一次 */
    if ( expect_true (!epoll_ctl(backend_fd,anfd->reify,fd,&ev)) )
        return;

    switch ( errno )
    {
    case EBADF  :
        assert ( "ev_loop::backend_modify EBADF",false );
        break;
    case EEXIST :
        ERROR( "ev_loop::backend_modify EEXIST\n" );
        break;
    case EINVAL :
        assert ( "ev_loop::backend_modify EINVAL",false );
        break;
    case ENOENT :
        assert ( "ev_loop::backend_modify ENOENT",false );
        break;
    case ENOMEM :
        assert ( "ev_loop::backend_modify ENOMEM",false );
        break;
    case ENOSPC :
        assert ( "ev_loop::backend_modify ENOSPC",false );
        break;
    case EPERM  :
        assert ( "ev_loop::backend_modify EPERM",false );
        break;
    }
}

void ev_loop::backend_init()
{
#ifdef EPOLL_CLOEXEC
    backend_fd = epoll_create1 (EPOLL_CLOEXEC);

    if (backend_fd < 0 && (errno == EINVAL || errno == ENOSYS))
#endif
    backend_fd = epoll_create (256);

    assert( "ev_loop::backend_init failed",backend_fd > 0 );

    fcntl (backend_fd, F_SETFD, FD_CLOEXEC);

    backend_mintime = 1e-3; /* epoll does sometimes return early, this is just to avoid the worst */
}

ev_tstamp ev_loop::get_time()
{
    struct timespec ts;
    clock_gettime (CLOCK_REALTIME, &ts);//more precise then gettimeofday
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

ev_tstamp ev_loop::get_clock()
{
    struct timespec ts;
    //linux kernel >= 2.6.39,otherwise CLOCK_REALTIME instead
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

ev_tstamp ev_loop::ev_now()
{
    return ev_rt_now;
}

void ev_loop::time_update()
{
    mn_now = get_clock ();
    
    /* only fetch the realtime clock every 0.5*MIN_TIMEJUMP seconds */
    /* interpolate in the meantime */
    if ( mn_now - now_floor < MIN_TIMEJUMP * .5 )
    {
        ev_rt_now = rtmn_diff + mn_now;
        return;
    }

    now_floor = mn_now;
    ev_rt_now = get_time ();

    /* loop a few times, before making important decisions.
     * on the choice of "4": one iteration isn't enough,
     * in case we get preempted during the calls to
     * get_time and get_clock. a second call is almost guaranteed
     * to succeed in that case, though. and looping a few more times
     * doesn't hurt either as we only do this on time-jumps or
     * in the unlikely event of having been preempted here.
     */
    for ( int32 i = 4; --i; )
    {
        rtmn_diff = ev_rt_now - mn_now;

        if ( expect_true (mn_now - now_floor < MIN_TIMEJUMP * .5) )
            return; /* all is well */
  
        ev_rt_now = get_time ();
        mn_now    = get_clock ();
        now_floor = mn_now;
    }

    /* no timer adjustment, as the monotonic clock doesn't jump */
    /* timers_reschedule (loop, rtmn_diff - odiff) */
}

void ev_loop::backend_poll( ev_tstamp timeout )
{
    /* epoll wait times cannot be larger than (LONG_MAX - 999UL) / HZ msecs, which is below */
    /* the default libev max wait time, however. */
    int32 eventcnt = epoll_wait (backend_fd, epoll_events, EPOLL_MAXEV, timeout * 1e3);
    if (expect_false (eventcnt < 0))
    {
        if ( errno != EINTR )
            FATAL( "ev_loop::backend_poll epoll wait errno(%d)",errno );

        return;
    }

    for ( int32 i = 0; i < eventcnt; ++i)
    {
        struct epoll_event *ev = epoll_events + i;

        int fd = ev->data.fd;
        int got  = (ev->events & (EPOLLOUT | EPOLLERR | EPOLLHUP) ? EV_WRITE : 0)
                 | (ev->events & (EPOLLIN  | EPOLLERR | EPOLLHUP) ? EV_READ  : 0);

        assert( "catch not interested event",got & anfds[fd].emask );

        fd_event ( fd, got );
    }
}

void ev_loop::fd_event( int32 fd,int32 revents )
{
    ANFD *anfd = anfds + fd;
    assert( "fd event no watcher",anfd->w );
    feed_event( anfd->w,revents );
}

void ev_loop::feed_event( ev_watcher *w,int32 revents )
{
    if ( expect_false(w->pending) )
        pendings[w->pending - 1].events |= revents;
    else
    {
        w->pending = ++pendingcnt;
        array_resize( ANPENDING,pendings,pendingmax,pendingcnt,EMPTY );
        pendings[w->pending - 1].w      = w;
        pendings[w->pending - 1].events = revents;
    }
}

void ev_loop::invoke_pending()
{
    while (pendingcnt)
    {
        ANPENDING *p = pendings + --pendingcnt;
  
        ev_watcher *w = p->w;
        if ( expect_true(w) ) /* 调用了clear_pending */
        {
            w->pending = 0;
            w->cb( w,p->events );
        }
    }
}

void ev_loop::clear_pending( ev_watcher *w )
{
    if (w->pending)
    {
        pendings [w->pending - 1].w = 0;
        w->pending = 0;
    }
}
