/*
 * dbus_batt.c — 最小 D-Bus 客户端 (无 libdbus)
 * 用途: 枚举 system bus 服务 + 查电池相关接口, 定位原生 UI 电池值来源
 * 用法: ./dbus_batt [服务名过滤关键字]
 * 编译: gcc -O2 -o dbus_batt dbus_batt.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

#define DBUS_SOCK "/var/run/dbus/system_bus_socket"

static int sfd = -1;
static unsigned serial = 1;

static void xwrite(const void *b, size_t n){
    const char *p = b;
    while(n > 0){ ssize_t r = write(sfd, p, n); if(r <= 0){ perror("write"); exit(1);} p += r; n -= r; }
}
static void xread(void *b, size_t n){
    char *p = b;
    while(n > 0){ ssize_t r = read(sfd, p, n); if(r <= 0){ perror("read"); exit(1);} p += r; n -= r; }
}
static void put_u32(unsigned v){ xwrite(&v, 4); }
static void put_u16(unsigned short v){ xwrite(&v, 2); }
static void put_byte(unsigned char v){ xwrite(&v, 1); }

/* 简单 8 字节对齐的写入缓冲 */
static unsigned char msgbuf[65536];
static size_t mp;   /* 当前写位置 (对齐到 8) */
static void align8(void){ while(mp & 7) msgbuf[mp++] = 0; }

/* 构造 method call, 带 0-2 个字符串参数, 返回消息长度 */
static size_t build_call(const char *dest, const char *path,
                         const char *iface, const char *member,
                         const char *arg1, const char *arg2){
    memset(msgbuf, 0, sizeof msgbuf);
    mp = 0;
    /* 固定头 */
    msgbuf[mp++] = 'l';          /* little endian */
    msgbuf[mp++] = 1;            /* METHOD_CALL */
    msgbuf[mp++] = 0;            /* flags */
    msgbuf[mp++] = 1;            /* version */
    /* body_length: 暂填 0, 最后回填 */
    size_t body_len_off = mp;    /* 4 字节, 偏移 4 */
    put_u32(0);
    unsigned this_serial = serial++;
    put_u32(this_serial);
    /* header fields: UINT32 len + fields */
    size_t hlen_off = mp;
    put_u32(0);
    size_t hstart = mp;
    /* field: code + VARIANT */
    /* PATH (1) */
    { msgbuf[mp++] = 1; size_t sl = strlen(path); size_t vpos = mp;
      put_byte(sl); memcpy(&msgbuf[mp], path, sl); mp += sl; msgbuf[mp++] = 0;
      (void)vpos; }
    /* INTERFACE (2) */
    if(iface){ msgbuf[mp++] = 2; size_t sl = strlen(iface); put_byte(sl); memcpy(&msgbuf[mp], iface, sl); mp += sl; msgbuf[mp++] = 0; }
    /* MEMBER (3) */
    { msgbuf[mp++] = 3; size_t sl = strlen(member); put_byte(sl); memcpy(&msgbuf[mp], member, sl); mp += sl; msgbuf[mp++] = 0; }
    /* DESTINATION (6) */
    if(dest){ msgbuf[mp++] = 6; size_t sl = strlen(dest); put_byte(sl); memcpy(&msgbuf[mp], dest, sl); mp += sl; msgbuf[mp++] = 0; }
    /* SIGNATURE (9) */
    if(arg1 || arg2){ msgbuf[mp++] = 9; const char *sg = arg2 ? "ss" : "s"; size_t sl = strlen(sg); put_byte(sl); memcpy(&msgbuf[mp], sg, sl); mp += sl; msgbuf[mp++] = 0; }
    /* header array 长度 */
    size_t flen = mp - hstart;
    memcpy(&msgbuf[hlen_off], &flen, 4);
    /* 消息对齐到 8 (消息总长 = 12 固定 + 4 len + fields + padding) */
    align8();
    /* body */
    size_t body_start = mp;
    if(arg1){ put_u32(strlen(arg1)); memcpy(&msgbuf[mp], arg1, strlen(arg1)); mp += strlen(arg1); align8(); }
    if(arg2){ put_u32(strlen(arg2)); memcpy(&msgbuf[mp], arg2, strlen(arg2)); mp += strlen(arg2); align8(); }
    unsigned body_len = (unsigned)(mp - body_start);
    memcpy(&msgbuf[body_len_off], &body_len, 4);
    return mp;
}

/* 读一条消息, 返回 body 起始偏移; body_len 输出 */
static size_t read_msg(unsigned *mtype, size_t *body_len){
    unsigned char h[16];
    xread(h, 16);
    unsigned blen; memcpy(&blen, h + 4, 4);
    unsigned flen; memcpy(&flen, h + 12, 4);
    *mtype = h[1];
    /* 头字段区域: 16 字节已读 (12 固定 + 4 len) */
    unsigned char fields[4096];
    xread(fields, flen);
    /* padding to 8 */
    size_t total = 16 + flen;
    size_t pad = (8 - (total & 7)) & 7;
    unsigned char pb[8]; if(pad) xread(pb, pad);
    /* body */
    static unsigned char body[65536];
    if(blen > 0) xread(body, blen);
    *body_len = blen;
    /* 存入全局 */
    memcpy(msgbuf, body, blen < sizeof msgbuf ? blen : sizeof msgbuf);
    return 0;
}

int main(int argc, char **argv){
    const char *filter = argc > 1 ? argv[1] : "";
    struct sockaddr_un sa;
    sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sfd < 0){ perror("socket"); return 1; }
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, DBUS_SOCK);
    if(connect(sfd, (struct sockaddr*)&sa, sizeof sa) < 0){ perror("connect"); return 1; }
    /* AUTH EXTERNAL: 先试无数据 (SO_PEERCRED), 若 REJECTED 再带 uid */
    xwrite("AUTH EXTERNAL\r\n", 15);
    char line[256];
    /* 带超时读取认证响应 */
    struct timeval tv = {2, 0};
    setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    size_t got = read(sfd, line, sizeof line - 1);
    if(got == (size_t)-1){ perror("AUTH read"); return 1; }
    if(got == 0){ fprintf(stderr, "AUTH: 服务端关闭连接\n"); return 1; }
    if(got <= 0){ fprintf(stderr, "AUTH 无响应 (errno=%d), 服务端可能拒绝\n", got < 0 ? 0 : (int)got); return 1; }
    line[got] = 0;
    fprintf(stderr, "AUTH 响应 (%zd bytes): ", got);
    for(size_t i = 0; i < got; i++) fprintf(stderr, "%02x ", (unsigned char)line[i]);
    fprintf(stderr, " | '%s'\n", line);
    if(strncmp(line, "OK", 2)){
        fprintf(stderr, "认证失败, 重试 REJECTED...\n");
        return 1;
    }
    xwrite("BEGIN\r\n", 7);

    /* Hello */
    size_t n = build_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                          "org.freedesktop.DBus", "Hello", NULL, NULL);
    xwrite(msgbuf, n);
    unsigned mt; size_t bl;
    read_msg(&mt, &bl);
    /* unique name 在 body, 忽略 */

    /* ListNames */
    n = build_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                   "org.freedesktop.DBus", "ListNames", NULL, NULL);
    xwrite(msgbuf, n);
    read_msg(&mt, &bl);
    /* body: 'as' -> UINT32 n + strings */
    if(mt == 2){
        unsigned cnt; memcpy(&cnt, msgbuf, 4);
        size_t o = 4;
        printf("=== system bus 服务 (%u 个, 含关键字 '%s') ===\n", cnt, filter);
        for(unsigned i = 0; i < cnt; i++){
            unsigned sl; memcpy(&sl, msgbuf + o, 4); o += 4;
            char s[512]; if(sl >= sizeof s) sl = sizeof s - 1;
            memcpy(s, msgbuf + o, sl); s[sl] = 0; o += sl;
            if(o & 3) o += 4 - (o & 3);   /* 字符串 4 对齐 */
            if(!filter[0] || strstr(s, filter)) printf("  %s\n", s);
        }
    } else {
        printf("ListNames 失败 (type=%u)\n", mt);
    }
    close(sfd);
    return 0;
}
