/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-25     heyuanjie87  the first version
 */

#include <adb_service.h>
#include <rtdevice.h>
#include <shell.h>

struct adb_shdev
{
    struct rt_device parent;
    struct rt_ringbuffer *rbuf;
    struct rt_ringbuffer *wbuf;
    struct rt_event notify;
};

#define ADEV_READ 1
#define ADEV_WRITE 2
#define ADEV_EXIT 4

struct shell_ext
{
    struct adb_packet *cur;
    adb_queue_t recv_que;
    int rque_buf[4];
    struct adb_shdev *dev;
    rt_thread_t shid;
    struct rt_event notify;
    rt_thread_t worker;
    int old_flag;
    int mode;
};

static struct adb_shdev _shdev;
static struct rt_mutex _lock;

static int _adwait(struct adb_shdev *ad, int ev, int ms)
{
    int r = 0;

    rt_event_recv(&ad->notify, ev,
                  RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                  rt_tick_from_millisecond(ms),
                  (unsigned *)&r);

    return r;
}

static rt_err_t _shell_service_device_init(struct rt_device *dev)
{
    struct adb_shdev *ad = (struct adb_shdev *)dev;

    ad->rbuf = rt_ringbuffer_create(32);
    ad->wbuf = rt_ringbuffer_create(64);
    if (!ad->rbuf || !ad->wbuf)
    {
        if (ad->rbuf)
            rt_ringbuffer_destroy(ad->rbuf);
        if (ad->wbuf)
            rt_ringbuffer_destroy(ad->wbuf);
    }

    return 0;
}

static rt_err_t _shell_service_device_open(struct rt_device *dev, rt_uint16_t oflag)
{
    struct adb_shdev *ad = (struct adb_shdev *)dev;

    dev->open_flag = oflag & 0xff;
    rt_event_init(&ad->notify, "adshdev", 0);

    return 0;
}

static rt_err_t _shell_service_device_close(struct rt_device *dev)
{
    struct adb_shdev *ad = (struct adb_shdev *)dev;

    rt_event_detach(&ad->notify);

    return 0;
}

static rt_size_t _shell_service_device_read(rt_device_t dev, rt_off_t pos,
                                            void *buffer, rt_size_t size)
{
    struct adb_shdev *ad = (struct adb_shdev *)dev;
    int len;

    if (!dev->user_data)
        return (rt_size_t)-EAGAIN;

_retry:
    rt_mutex_take(&_lock, -1);
    len = rt_ringbuffer_get(ad->rbuf, buffer, size);
    rt_mutex_release(&_lock);
    if (len == 0)
    {
        int ret = _adwait(ad, ADEV_READ, 100);
        if (ret & ADEV_READ)
            goto _retry;
    }

    if (len == 0)
        len = -EAGAIN;

    return len;
}

static rt_size_t _shell_service_device_write(rt_device_t dev, rt_off_t pos,
                                             const void *buffer, rt_size_t size)
{
    struct adb_shdev *ad = (struct adb_shdev *)dev;
    int wlen;
    char *spos = (char *)buffer;
    int cnt = 0;

    if (!dev->user_data)
        return 0;

    if (rt_interrupt_get_nest())
    {
        return rt_ringbuffer_put(ad->wbuf, (unsigned char *)spos, size);
    }

    while (size && dev->user_data)
    {
        rt_mutex_take(&_lock, -1);
        wlen = rt_ringbuffer_put(ad->wbuf, (unsigned char *)spos, size);
        rt_mutex_release(&_lock);

        rt_event_send(&ad->notify, ADEV_WRITE);

        if (wlen)
        {
            spos += wlen;
            size -= wlen;
            cnt += wlen;
        }
        else
            rt_thread_yield();
    }

    return cnt;
}

static rt_err_t _shell_service_device_ctrl(rt_device_t dev,
                                           int cmd, void *args)
{
    int ret = 0;

    return ret;
}

#ifdef RT_USING_DEVICE_OPS
const static struct rt_device_ops shell_ops =
{
    _shell_service_device_init,
    _shell_service_device_open,
    _shell_service_device_close,
    _shell_service_device_read,
    _shell_service_device_write,
    _shell_service_device_ctrl
};
#endif

static int _device_init(rt_device_t shell_device, void *usrdat)
{
    int ret;

    shell_device->type = RT_Device_Class_Char;
#ifdef RT_USING_DEVICE_OPS
    device->ops = &shell_ops;
#else
    shell_device->init = _shell_service_device_init;
    shell_device->open = _shell_service_device_open;
    shell_device->close = _shell_service_device_close;
    shell_device->read = _shell_service_device_read;
    shell_device->write = _shell_service_device_write;
    shell_device->control = _shell_service_device_ctrl;
#endif
    shell_device->user_data = usrdat;

    ret = rt_device_register(shell_device, "as-sh", RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);

    return ret;
}

static bool send_ready(struct shell_ext *ext, char *args)
{
    struct adb_packet *p;
    
    ext->mode = rt_strlen(args);
    p = adb_packet_new(ext->mode + 1);
    if (!p)
        return false;

    p->msg.data_length = ext->mode + 1;
    rt_memcpy(p->payload, args, ext->mode);
    p->payload[ext->mode] = '\n';
    if (!adb_packet_enqueue(&ext->recv_que, p, 0))
    {
        adb_packet_delete(p);
        return false;
    }

    return true;
}

static int _shell_open(struct adb_service *ser, char *args)
{
    int ret = -1;
    struct shell_ext *ext;

    ext = (struct shell_ext *)ser->extptr;
    ext->shid = rt_thread_find(FINSH_THREAD_NAME);
    if (!ext->shid)
    {
        return -1;
    }

    ret = _device_init(&_shdev.parent, ser);
    if (ret == 0)
    {
        int pr = 23;

        rt_mb_init(&ext->recv_que, "as-sh", ext->rque_buf,
                   sizeof(ext->rque_buf) / sizeof(ext->rque_buf[0]), 0);
        rt_event_init(&ext->notify, "as-sh", 0);

        ext->dev = &_shdev;

        ext->old_flag = ioctl(libc_stdio_get_console(), F_GETFL, 0);
        ioctl(libc_stdio_get_console(), F_SETFL, (void *)(ext->old_flag | O_NONBLOCK));

        rt_thread_control(ext->shid, RT_THREAD_CTRL_CHANGE_PRIORITY,
                          (void *)&pr);
        libc_stdio_set_console("as-sh", O_RDWR);
        rt_console_set_device("as-sh");
        rt_thread_resume(ext->shid);
        rt_thread_mdelay(50);

        ret = rt_thread_startup(ext->worker);
        if (ret == 0)
        {
            if (!send_ready(ext, args))
            {
                //todo
                ret = -1;
            }
        }
    }

    return ret;
}

static int _shell_close(struct adb_service *ser)
{
    int ret = 0;
    struct shell_ext *ext;

    ext = (struct shell_ext *)ser->extptr;
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
    libc_stdio_set_console(RT_CONSOLE_DEVICE_NAME, O_RDWR);
    ser->online = 0;

    rt_thread_resume(ext->shid);
    rt_thread_mdelay(50);
    rt_mutex_take(&_lock, -1);

    rt_mutex_release(&_lock);

    rt_event_send(&ext->notify, 2);
    rt_mb_detach(&ext->recv_que);
    rt_event_detach(&ext->notify);

    rt_device_unregister(&ext->dev->parent);

    return ret;
}

static bool _shell_enqueue(struct adb_service *ser, struct adb_packet *p, int ms)
{
    struct shell_ext *ext;
    bool ret;

    if (!ser->online)
        return false;

    ext = (struct shell_ext *)ser->extptr;
    ret = adb_packet_enqueue(&ext->recv_que, p, ms);

    return ret;
}

static const struct adb_service_ops _ops =
{
    _shell_open,
    _shell_close,
    _shell_enqueue
};

static void do_readdev(struct adb_service *ser, struct shell_ext *ext)
{
    int size;
    struct adb_packet *p;

    size = rt_ringbuffer_data_len(ext->dev->wbuf);
    p = adb_packet_new(size);
    if (!p)
        return;

    rt_ringbuffer_get(ext->dev->wbuf, (unsigned char *)p->payload, size);
    if (!adb_service_sendpacket(ser, p, 60))
    {
        adb_packet_delete(p);
    }
}

static void do_writedev(struct adb_service *ser, struct shell_ext *ext)
{
    struct adb_packet *p;
    char *pos;
    int len;

    if (!ext->cur)
    {
        if (!adb_packet_dequeue(&ext->recv_que, &ext->cur, 20))
            return;
        ext->cur->split = 0;
    }
    p = ext->cur;

    pos = p->payload + p->split;
    len = rt_ringbuffer_put(ext->dev->rbuf, (const unsigned char *)pos,
                            p->msg.data_length);
    p->split += len;
    p->msg.data_length -= len;
    if (p->msg.data_length == 0)
    {
        ext->cur = 0;
        adb_packet_delete(p);
    }

    rt_event_send(&ext->dev->notify, ADEV_READ);
}

static void service_thread(void *arg)
{
    struct adb_service *ser;
    struct shell_ext *ext;
    unsigned revt;
    int exit = 50;

    ser = arg;
    ext = ser->extptr;
    ser->online = 1;

    while (ser->online)
    {
        do_writedev(ser, ext);

        revt = _adwait(ext->dev, ADEV_WRITE, 20);
        if (revt & ADEV_WRITE)
        {
            exit = 20;
            do_readdev(ser, ext);
        }
        else if (ext->mode != 0)
        {
            if (--exit == 0)
                break;
        }
    }
    ser->online = 0;
    ext->dev->parent.user_data = 0;
    adb_packet_clear(&ext->recv_que);
    adb_send_close(ser->d, ser->localid, ser->remoteid);
}

static struct adb_service *_shell_create(struct adb_service_handler *h)
{
    struct adb_service *ser;
    struct shell_ext *ext;

    if (_shdev.parent.ref_count)
        return RT_NULL;
    if (rt_thread_find("as-sh"))
        return RT_NULL;

    ser = adb_service_alloc(&_ops, sizeof(struct shell_ext));
    if (ser)
    {
        ext = (struct shell_ext *)ser->extptr;
        ext->dev = &_shdev;
        ext->worker = rt_thread_create("as-sh",
                                       service_thread,
                                       ser,
                                       2048,
                                       22,
                                       20);
    }

    return ser;
}

static void _shell_destroy(struct adb_service_handler *h, struct adb_service *s)
{
    rt_free(s);
}

int adb_shell_init(void)
{
    static struct adb_service_handler _h;

    _h.name = "shell:";
    _h.create = _shell_create;
    _h.destroy = _shell_destroy;

    rt_mutex_init(&_lock, "as-sh", 0);

    return adb_service_handler_register(&_h);
}
INIT_APP_EXPORT(adb_shell_init);

static void exitas(int argc, char **argv)
{
    rt_thread_t tid;

    if (_shdev.parent.ref_count == 0)
    {
        rt_kprintf("adb shell service not run");
        return;
    }

    rt_mutex_take(&_lock, -1);
    tid = rt_thread_find("as-sh");
    if (tid)
    {
        struct adb_service *ser;

        ser = tid->parameter;
        ser->online = 0;
    }
    rt_mutex_release(&_lock);
}
MSH_CMD_EXPORT(exitas, exit adb shell service);
