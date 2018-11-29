#include <adb_pque.h>

#if 1
#define DBG_ENABLE
#else
#undef DBG_ENABLE
#endif
#define DBG_SECTION_NAME  "ADB pque"
#define DBG_LEVEL         DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>

#ifdef DBG_ENABLE
#define LOG_CON(c, fmt, ...) \
    if (c)   \
        LOG_E(fmt, ##__VA_ARGS__)
#else
#define LOG_CON(...)
#endif

struct adb_packet* adb_packet_new(int datlen)
{
    struct adb_packet *p;

    p = rt_malloc(sizeof(struct adb_packet) + datlen);
    if (p)
    {
        p->msg.data_length = datlen;
        p->split = 0;
    }

    LOG_CON(!p, "no mem to new packet");

    return p;
}

void adb_packet_delete(struct adb_packet *p)
{
    if (!p)
        return;

    rt_free(p);
}

bool adb_packet_enqueue(adb_queue_t *q, struct adb_packet *p, int ms)
{
    int ret;
    int tick;

    tick = rt_tick_from_millisecond(ms);
    ret = rt_mb_send_wait(q, (rt_uint32_t)p, tick);
    LOG_CON(ret, "enqueue fail");

    return ret == 0;
}

bool adb_packet_dequeue(adb_queue_t *q, struct adb_packet **p, int ms)
{
    int ret;
    int tick;
    
    *p = 0;
    tick = rt_tick_from_millisecond(ms);
    ret = rt_mb_recv(q, (rt_uint32_t*)p, tick);

    return ret == 0;
}

void adb_packet_clear(adb_queue_t *q)
{
    struct adb_packet *p;

    while (rt_mb_recv(q, (rt_uint32_t*)&p, 0) == 0)
    {
        adb_packet_delete(p);
    }
}

unsigned adb_packet_checksum(struct adb_packet *p)
{
    unsigned sum = 0;
    int i;

    for (i = 0; i < p->msg.data_length; ++i) 
    {
        sum += (unsigned char)(p->payload[i]);
    }

    return sum;
}
