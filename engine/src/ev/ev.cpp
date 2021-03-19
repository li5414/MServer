#include "ev.hpp"
#include "ev_watcher.hpp"

// minimum timejump that gets detected (if monotonic clock available)
#define MIN_TIMEJUMP 1.

/*
 * the heap functions want a real array index. array index 0 is guaranteed to
 * not be in-use at any time. the first heap entry is at array [HEAP0]. DHEAP
 * gives the branching factor of the d-tree.
 */

#define HEAP0             1 // 二叉堆的位置0不用，数据从1开始存放
#define HPARENT(k)        ((k) >> 1)
#define UPHEAP_DONE(p, k) (!(p))

#if USE_EPOLL == 1
    #include "ev_epoll.inl"
#else
    #include "ev_poll.inl"
#endif

EV::EV()
{
    _fds.reserve(1024);
    _fd_changes.reserve(1024);
    _pendings.reserve(1024);
    _timers.reserve(1024);

    // 定时时使用_timercnt管理数量，因此提前分配内存以提高效率
    _timercnt = 0;
    _timers.resize(1024, nullptr);

    ev_rt_now = get_time();

    update_clock();
    now_floor = mn_now;
    rtmn_diff = ev_rt_now - mn_now;

    _busy_time = 0;

    _backend              = new EVBackend();
    _backend_time_coarse = 0;
}

EV::~EV()
{
    delete _backend;
    _backend = nullptr;
}

int32_t EV::loop()
{
    // 脚本可能加载了很久才进入loop，需要及时更新时间
    time_update();

    /*
     * 这个循环里执行的顺序有特殊要求
     * 1. 检测loop_done必须在invoke_pending之后，中间不能执行wait，不然设置loop_done
     * 为false后无法停服
     * 2. wait的前后必须执行time_update，不然计算出来的时间不准
     * 3. 计算wait的时间必须在wait之前，不能在invoke_pending的时候一般执行逻辑一边计算。
     * 因为执行逻辑可能会耗很长时间，那时候计算的时间是不准的
     */

    _done       = false;
    int64_t last_ms = ev_now_ms;
    while (EXPECT_TRUE(!_done))
    {
        // 把fd变更设置到epoll中去
        fd_reify();

        time_update();
        _busy_time = ev_now_ms - last_ms;

        EvTstamp backend_time = _backend_time_coarse - ev_now_ms;
        if (_timercnt) /* 如果有定时器，睡眠时间不超过定时器触发时间，以免sleep过头 */
        {
            EvTstamp to = 1e3 * ((_timers[HEAP0])->_at - mn_now);
            if (backend_time > to) backend_time = to;
        }
        if (EXPECT_FALSE(backend_time < BACKEND_MIN_TM))
        {
            backend_time = BACKEND_MIN_TM;
        }
        _backend->wait(this, backend_time);

        time_update();

        last_ms = ev_now_ms;

        // 不同的逻辑会预先设置主循环下次阻塞的时间。在执行其他逻辑时，可能该时间已经过去了
        // 这说明主循环比较繁忙，会被修正为BACKEND_MIN_TM，而不是精确的按预定时间执行
        _backend_time_coarse = BACKEND_MAX_TM + ev_now_ms;

        // 处理timer变更
        timers_reify();

        // 触发io和timer事件
        invoke_pending();

        running(); // 执行其他逻辑
    }

    return 0;
}

int32_t EV::quit()
{
    _done = true;

    return 0;
}

int32_t EV::io_start(EVIO *w)
{
    int32_t fd = w->_fd;

    if (EXPECT_FALSE(fd >= (int32_t)_fds.size())) _fds.resize(fd + 1, nullptr);

    _fds[fd] = w;

    fd_change(fd);

    return 1;
}

int32_t EV::io_stop(EVIO *w)
{
    clear_pending(w);

    if (EXPECT_FALSE(!w->active())) return 0;

    int32_t fd = w->_fd;
    assert(fd >= 0 && uint32_t(fd) < _fds.size());

    _fds[fd] = nullptr;
    fd_change(fd);

    return 0;
}

void EV::fd_reify()
{
    for (auto fd : _fd_changes)
    {
        EVIO *io = _fds[fd];
        if (io)
        {
            int32_t events = io->_events;
            _backend->modify(fd, io->_emask, events);
            io->_emask = events;
        }
        else
        {
            _backend->modify(fd, 0, 0); // 移除该socket
        }
    }

    _fd_changes.clear();
}

EvTstamp EV::get_time()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts); // more precise then gettimeofday
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int64_t EV::get_ms_time()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

/*
 * 获取当前时钟
 * CLOCK_REALTIME:
 * 系统实时时间，从Epoch计时，可以被用户更改以及adjtime和NTP影响。
 * CLOCK_REALTIME_COARSE:
 * 系统实时时间，比起CLOCK_REALTIME有更快的获取速度，更低一些的精确度。
 * CLOCK_MONOTONIC:
 * 从系统启动这一刻开始计时，即使系统时间被用户改变，也不受影响。系统休眠时不会计时。受adjtime和NTP影响。
 * CLOCK_MONOTONIC_COARSE:
 * 如同CLOCK_MONOTONIC，但有更快的获取速度和更低一些的精确度。受NTP影响。
 * CLOCK_MONOTONIC_RAW:
 * 与CLOCK_MONOTONIC一样，系统开启时计时，但不受NTP影响，受adjtime影响。
 * CLOCK_BOOTTIME:
 * 从系统启动这一刻开始计时，包括休眠时间，受到settimeofday的影响。
 */
void EV::update_clock()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    mn_now    = ts.tv_sec + ts.tv_nsec * 1e-9;
    ev_now_ms = ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

void EV::time_update()
{
    update_clock();

    /* 直接计算出UTC时间而不通过get_time获取
     * 例如主循环为5ms时，0.5s同步一次省下了100次系统调用(get_time是一个syscall，比较慢)
     * libevent是5秒同步一次CLOCK_SYNC_INTERVAL，libev是0.5秒
     */
    if (mn_now - now_floor < MIN_TIMEJUMP * .5)
    {
        ev_rt_now = rtmn_diff + mn_now;
        return;
    }

    now_floor         = mn_now;
    ev_rt_now         = get_time();
    EvTstamp old_diff = rtmn_diff;

    /* 当两次diff相差比较大时，说明有人调了UTC时间。由于clock_gettime是一个syscall，可能
     * 出现mn_now是调整前，ev_rt_now为调整后的情况。
     * 必须循环几次保证mn_now和ev_rt_now取到的都是调整后的时间
     *
     * 参考libev
     * loop a few times, before making important decisions.
     * on the choice of "4": one iteration isn't enough,
     * in case we get preempted during the calls to
     * get_time and get_clock. a second call is almost guaranteed
     * to succeed in that case, though. and looping a few more times
     * doesn't hurt either as we only do this on time-jumps or
     * in the unlikely event of having been preempted here.
     */
    for (int32_t i = 4; --i;)
    {
        rtmn_diff = ev_rt_now - mn_now;

        EvTstamp diff = old_diff - rtmn_diff;
        if (EXPECT_TRUE((diff < 0. ? -diff : diff) < MIN_TIMEJUMP))
        {
            return;
        }

        ev_rt_now = get_time();

        update_clock();
        now_floor = mn_now;
    }
}

void EV::fd_event(int32_t fd, int32_t revents)
{
    assert(fd >= 0 && fd < (int32_t)_fds.size());

    EVIO *io = _fds[fd];

    assert(io);
    feed_event(io, revents);
}

void EV::feed_event(EVWatcher *w, int32_t revents)
{
    // 已经在待处理队列里了，则设置事件即可
    w->_revents |= revents;
    if (EXPECT_TRUE(!w->_pending))
    {
        _pendings.emplace_back(w);
        w->_pending = _pendings.size();
    }
}

void EV::invoke_pending()
{
    for (auto w : _pendings)
    {
        // 可能其他事件调用了clear_pending导致当前watcher无效了
        if (EXPECT_TRUE(w->_pending))
        {
            w->_pending = 0;
            w->_cb(w->_revents);
            w->_revents = 0;
        }
    }
    _pendings.clear();
}

void EV::clear_pending(EVWatcher *w)
{
    // 如果这个watcher在pending队列中，不需要删除，只需要设置标识即可
    if (w->_pending)
    {
        w->_revents = 0;
        w->_pending = 0;
    }
}

void EV::timers_reify()
{
    while (_timercnt && (_timers[HEAP0])->_at < mn_now)
    {
        EVTimer *w = _timers[HEAP0];

        assert(w->active());

        if (w->_repeat)
        {
            w->_at += w->_repeat;

            // 如果时间出现偏差，重新调整定时器
            if (EXPECT_FALSE(w->_at < mn_now)) w->reschedule(mn_now);

            assert(w->_repeat > 0.);

            down_heap(_timers.data(), _timercnt, HEAP0);
        }
        else
        {
            w->stop();
        }

        feed_event(w, EV_TIMER);
    }
}

int32_t EV::timer_start(EVTimer *w)
{
    w->_at += mn_now;

    assert(w->_repeat >= 0.);

    ++_timercnt;
    int32_t active = _timercnt + HEAP0 - 1;
    if (_timers.size() < (size_t)active + 1)
    {
        _timers.resize(active + 1024, nullptr);
    }

    _timers[active] = w;
    up_heap(_timers.data(), active);

    assert(active >= 1 && _timers[w->_active] == w);

    return active;
}

// 暂停定时器
int32_t EV::timer_stop(EVTimer *w)
{
    clear_pending(w);
    if (EXPECT_FALSE(!w->active())) return 0;

    {
        int32_t active = w->_active;

        assert(_timers[active] == w);

        --_timercnt;

        // 如果这个定时器刚好在最后，就不用调整二叉堆
        if (EXPECT_TRUE(active < _timercnt + HEAP0))
        {
            // 把当前最后一个timer(_timercnt + HEAP0)覆盖当前timer的位置，再重新调整
            _timers[active] = _timers[_timercnt + HEAP0];
            adjust_heap(_timers.data(), _timercnt, active);
        }
    }

    w->_at -= mn_now;
    w->_active = 0;

    return 0;
}

void EV::down_heap(HeapNode *heap, int32_t N, int32_t k)
{
    HeapNode he = heap[k];

    for (;;)
    {
        // 二叉堆的规则：N*2、N*2+1则为child的下标
        int c = k << 1;

        if (c >= N + HEAP0) break;

        // 取左节点(N*2)还是取右节点(N*2+1)
        c += c + 1 < N + HEAP0 && (heap[c])->_at > (heap[c + 1])->_at ? 1 : 0;

        if (he->_at <= (heap[c])->_at) break;

        heap[k]            = heap[c];
        (heap[k])->_active = k;
        k                  = c;
    }

    heap[k]     = he;
    he->_active = k;
}

void EV::up_heap(HeapNode *heap, int32_t k)
{
    HeapNode he = heap[k];

    for (;;)
    {
        int p = HPARENT(k);

        if (UPHEAP_DONE(p, k) || (heap[p])->_at <= he->_at) break;

        heap[k]            = heap[p];
        (heap[k])->_active = k;
        k                  = p;
    }

    heap[k]     = he;
    he->_active = k;
}

void EV::adjust_heap(HeapNode *heap, int32_t N, int32_t k)
{
    if (k > HEAP0 && (heap[k])->_at <= (heap[HPARENT(k)])->_at)
    {
        up_heap(heap, k);
    }
    else
    {
        down_heap(heap, N, k);
    }
}

void EV::reheap(HeapNode *heap, int32_t N)
{
    /* we don't use floyds algorithm, upheap is simpler and is more
     * cache-efficient also, this is easy to implement and correct
     * for both 2-heaps and 4-heaps
     */
    for (int32_t i = 0; i < N; ++i)
    {
        up_heap(heap, i + HEAP0);
    }
}
