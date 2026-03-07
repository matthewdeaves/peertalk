/*
 * pt_internal.h -- PeerTalk internal types and macros
 *
 * This header is private to the SDK. Never include from peertalk.h.
 */

#ifndef PT_INTERNAL_H
#define PT_INTERNAL_H

#include "peertalk.h"

#include "clog.h"

#include <string.h> /* memcpy, memset */

/* ------------------------------------------------------------------ */
/* Wire protocol constants                                             */
/* ------------------------------------------------------------------ */

#define PT_DISCOVERY_PORT   7353
#define PT_TCP_PORT         7354
#define PT_UDP_MSG_PORT     7355

#define PT_MAGIC_0  0x50  /* 'P' */
#define PT_MAGIC_1  0x54  /* 'T' */
#define PT_MAGIC_2  0x4C  /* 'L' */
#define PT_MAGIC_3  0x4B  /* 'K' */

#define PT_WIRE_VERSION     1
#define PT_MSG_TYPE_GOODBYE 255

#define PT_NAME_MAX         31
#define PT_DISCOVERY_HEADER 5   /* 4 magic + 1 version */
#define PT_DISCOVERY_MAX    37  /* header + 32 name bytes */

#define PT_TCP_HEADER_SIZE      4   /* 2 len + 1 type + 1 flags */
#define PT_TCP_CHUNK_HEADER     8   /* 2 len + 1 type + 1 flags + 2 seq + 2 total */
#define PT_UDP_HEADER_SIZE      3   /* 2 len + 1 type */

#define PT_CHUNK_FLAG           0x01

#define PT_DISCOVERY_INTERVAL   2   /* seconds between broadcasts */
#define PT_DISCOVERY_TIMEOUT    15  /* seconds before peer lost */
#define PT_TCP_TIMEOUT          60  /* seconds of TCP inactivity */
#define PT_CONNECT_TIMEOUT      15  /* seconds to establish TCP */
#define PT_REASSEMBLY_TIMEOUT   5   /* seconds for chunk reassembly */

#define PT_UDP_MTU_SAFE         1400 /* max fast message payload */

/* POSIX defaults */
#define PT_DEFAULT_MAX_PEERS    32
#define PT_DEFAULT_TCP_RECV     8192
#define PT_DEFAULT_TCP_SEND     4096
#define PT_DEFAULT_UDP_BUF      512
#define PT_DEFAULT_REASSEMBLY   65536

/* Global overhead for memory sizing */
#define PT_GLOBAL_OVERHEAD      1024
#define PT_PEER_METADATA        128

/* ------------------------------------------------------------------ */
/* Byte-order helpers                                                  */
/* ------------------------------------------------------------------ */

/* Network byte order is big-endian. 68k is natively big-endian. */

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
/* 68k/PPC: already big-endian, no conversion needed */
#define pt_htons(x) (x)
#define pt_ntohs(x) (x)
#else
/* POSIX: use system htons/ntohs */
#include <arpa/inet.h>
#define pt_htons(x) htons(x)
#define pt_ntohs(x) ntohs(x)
#endif

/* ------------------------------------------------------------------ */
/* ISR-safe memcpy (byte copy, no Toolbox)                             */
/* Only needed on Classic Mac where memcpy may call Toolbox routines.   */
/* ------------------------------------------------------------------ */

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
static void pt_memcpy_isr(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n-- > 0) {
        *d++ = *s++;
    }
}
#endif

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

struct PT_Context_Internal;
struct PT_Peer_Internal;

/* ------------------------------------------------------------------ */
/* Platform operations vtable                                          */
/* ------------------------------------------------------------------ */

typedef struct PT_PlatformOps {
    PT_Status (*init)(struct PT_Context_Internal *ctx);
    void      (*shutdown)(struct PT_Context_Internal *ctx);
    PT_Status (*udp_broadcast)(struct PT_Context_Internal *ctx,
                               unsigned short port,
                               const void *data, size_t len);
    PT_Status (*udp_send)(struct PT_Context_Internal *ctx,
                          struct PT_Peer_Internal *peer,
                          unsigned short port,
                          const void *data, size_t len);
    PT_Status (*udp_listen)(struct PT_Context_Internal *ctx,
                            unsigned short port);
    PT_Status (*tcp_listen)(struct PT_Context_Internal *ctx);
    PT_Status (*tcp_connect)(struct PT_Context_Internal *ctx,
                             struct PT_Peer_Internal *peer);
    PT_Status (*tcp_send)(struct PT_Context_Internal *ctx,
                          struct PT_Peer_Internal *peer,
                          const void *data, size_t len);
    void      (*tcp_disconnect)(struct PT_Context_Internal *ctx,
                                struct PT_Peer_Internal *peer);
    void      (*poll)(struct PT_Context_Internal *ctx);
} PT_PlatformOps;

/* ------------------------------------------------------------------ */
/* Platform peer state (union across platforms)                         */
/* ------------------------------------------------------------------ */

typedef struct PT_PlatformPeer {
#if defined(PT_PLATFORM_POSIX)
    int tcp_fd;           /* -1 if not connected */
#elif defined(PT_PLATFORM_MACTCP)
    void *tcp_stream;     /* pointer to TCPStreamSlot, NULL if not connected */
#elif defined(PT_PLATFORM_OT)
    void *endpoint;       /* EndpointRef */
    unsigned long events; /* volatile event flags */
#endif
    int dummy; /* ensure non-empty struct on all platforms */
} PT_PlatformPeer;

/* ------------------------------------------------------------------ */
/* Callbacks struct                                                     */
/* ------------------------------------------------------------------ */

typedef struct PT_Callbacks {
    PT_PeerCallback       on_peer_discovered;
    void                 *on_peer_discovered_data;
    PT_PeerCallback       on_peer_lost;
    void                 *on_peer_lost_data;
    PT_PeerCallback       on_connected;
    void                 *on_connected_data;
    PT_DisconnectCallback on_disconnected;
    void                 *on_disconnected_data;
    PT_MessageCallback    on_message[256];
    void                 *on_message_data[256];
    PT_ErrorCallback      on_error;
    void                 *on_error_data;
} PT_Callbacks;

/* ------------------------------------------------------------------ */
/* PT_Peer_Internal                                                    */
/* ------------------------------------------------------------------ */

typedef struct PT_Peer_Internal {
    /* Public-facing (cast to PT_Peer*) */
    char          name[PT_NAME_MAX + 1];
    PT_PeerState  state;
    unsigned long ip_addr;          /* network byte order */
    char          addr_str[16];     /* dotted-quad string */
    unsigned long last_seen;        /* timestamp (seconds) */
    unsigned long last_tcp_activity;/* for TCP inactivity timeout */
    unsigned long connect_start;    /* when tcp_connect was initiated */
    int           in_use;

    /* Per-peer buffers (pointers into memory_block) */
    unsigned char *tcp_recv_buf;
    size_t         tcp_recv_size;
    size_t         tcp_recv_len;    /* bytes currently buffered */

    unsigned char *tcp_send_buf;
    size_t         tcp_send_size;

    unsigned char *udp_buf;
    size_t         udp_buf_size;

    unsigned char *reassembly_buf;
    size_t         reassembly_buf_size;
    unsigned char  reassembly_type;
    unsigned short reassembly_received;
    unsigned short reassembly_total;
    unsigned long  reassembly_timer;
    size_t         reassembly_stride; /* payload size of first chunk */

    /* Platform-specific per-peer state */
    PT_PlatformPeer platform_peer;
} PT_Peer_Internal;

/* ------------------------------------------------------------------ */
/* PT_Context_Internal                                                 */
/* ------------------------------------------------------------------ */

typedef struct PT_Context_Internal {
    char          name[PT_NAME_MAX + 1];
    unsigned long local_ip;         /* network byte order */

    /* Platform abstraction */
    PT_PlatformOps *platform_ops;
    void           *platform_state;

    /* Peer management */
    PT_Peer_Internal *peers;
    int               max_peers;
    int               peer_count;

    /* Message type registry */
    PT_Transport  message_types[256];

    /* Callbacks */
    PT_Callbacks  callbacks;

    /* Discovery state */
    int           discovery_active;    /* broadcasting? */
    int           discovery_listening; /* receiving? */
    unsigned long discovery_timer;     /* next broadcast time */

    /* Memory */
    void         *memory_block;
    size_t        memory_size;

    /* Time tracking */
    unsigned long current_time;        /* updated each PT_Poll */

    /* UDP send buffer (R48: moved from PT_Send stack to avoid
       68k stack overflow on Mac SE with ~8KB stack) */
    unsigned char udp_send_buf[PT_UDP_MTU_SAFE + PT_UDP_HEADER_SIZE];
} PT_Context_Internal;

/* ------------------------------------------------------------------ */
/* Internal helpers (implemented in various .c files)                   */
/* ------------------------------------------------------------------ */

/* pt_memory.c */
size_t pt_memory_calculate_size(int max_peers, size_t tcp_recv,
                                size_t tcp_send, size_t udp_buf,
                                size_t reassembly);
int    pt_memory_allocate(PT_Context_Internal *ctx,
                          int max_peers, size_t tcp_recv,
                          size_t tcp_send, size_t udp_buf,
                          size_t reassembly);
void   pt_memory_free(PT_Context_Internal *ctx);

/* pt_discovery.c */
void      pt_discovery_broadcast(PT_Context_Internal *ctx);
void      pt_discovery_receive(PT_Context_Internal *ctx,
                               const void *data, size_t len,
                               unsigned long source_ip);
void      pt_discovery_check_timeouts(PT_Context_Internal *ctx);

/* pt_messaging.c */
void      pt_messaging_process_tcp_data(PT_Context_Internal *ctx,
                                        PT_Peer_Internal *peer);
void      pt_messaging_process_udp_data(PT_Context_Internal *ctx,
                                        const void *data, size_t len,
                                        unsigned long source_ip);
void      pt_messaging_check_reassembly_timeouts(PT_Context_Internal *ctx);

/* pt_core.c */
void      pt_format_ip(unsigned long ip, char *buf);
void      pt_fire_error(PT_Context_Internal *ctx,
                        PT_Peer_Internal *peer,
                        PT_Status err, const char *desc);
PT_Peer_Internal *pt_find_peer_by_ip(PT_Context_Internal *ctx,
                                     unsigned long ip);
PT_Peer_Internal *pt_alloc_peer(PT_Context_Internal *ctx);
void      pt_handle_incoming_connection(PT_Context_Internal *ctx,
                                        unsigned long peer_ip,
                                        PT_PlatformPeer *ppeer);
void      pt_handle_peer_disconnect(PT_Context_Internal *ctx,
                                    PT_Peer_Internal *peer,
                                    PT_DisconnectReason reason);
unsigned long pt_get_time(void);

/* Platform init functions */
#if defined(PT_PLATFORM_POSIX)
PT_PlatformOps *posix_get_ops(void);
#elif defined(PT_PLATFORM_MACTCP)
PT_PlatformOps *mactcp_get_ops(void);
#elif defined(PT_PLATFORM_OT)
PT_PlatformOps *ot_get_ops(void);
#endif

#endif /* PT_INTERNAL_H */
