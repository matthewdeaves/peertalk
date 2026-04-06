/*
 * peertalk.h -- PeerTalk LAN peer-to-peer SDK
 *
 * C89-compatible public header. No stdint.h, no bool.
 * Include this single header to use the SDK.
 */

#ifndef PEERTALK_H
#define PEERTALK_H

#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Opaque types                                                        */
/* ------------------------------------------------------------------ */

typedef struct PT_Context PT_Context;
typedef struct PT_Peer    PT_Peer;

/* ------------------------------------------------------------------ */
/* Enums                                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    PT_OK = 0,
    PT_ERR_INIT,
    PT_ERR_NOT_CONNECTED,
    PT_ERR_SEND_FAILED,
    PT_ERR_INVALID_ARG,
    PT_ERR_NO_ROOM
} PT_Status;

typedef enum {
    PT_PEER_DISCOVERED,
    PT_PEER_CONNECTED,
    PT_PEER_DISCONNECTED
} PT_PeerState;

typedef enum {
    PT_FAST,
    PT_RELIABLE
} PT_Transport;

typedef enum {
    PT_QUIT,
    PT_TIMEOUT,
    PT_DISCONNECT_ERROR
} PT_DisconnectReason;

/* ------------------------------------------------------------------ */
/* Callback typedefs                                                   */
/* ------------------------------------------------------------------ */

typedef void (*PT_PeerCallback)(
    PT_Peer *peer,
    void *user_data
);

typedef void (*PT_DisconnectCallback)(
    PT_Peer *peer,
    PT_DisconnectReason reason,
    void *user_data
);

typedef void (*PT_MessageCallback)(
    PT_Peer *peer,
    const void *data,
    size_t len,
    void *user_data
);

typedef void (*PT_ErrorCallback)(
    PT_Peer *peer,
    PT_Status error,
    const char *description,
    void *user_data
);

/* ------------------------------------------------------------------ */
/* Lifecycle (2)                                                       */
/* ------------------------------------------------------------------ */

PT_Status PT_Init(PT_Context **ctx, const char *name);
void      PT_Shutdown(PT_Context *ctx);

/* ------------------------------------------------------------------ */
/* Discovery (2)                                                       */
/* ------------------------------------------------------------------ */

PT_Status PT_StartDiscovery(PT_Context *ctx);
void      PT_StopDiscovery(PT_Context *ctx);

/* ------------------------------------------------------------------ */
/* Connections (2)                                                     */
/* ------------------------------------------------------------------ */

PT_Status PT_Connect(PT_Context *ctx, PT_Peer *peer);
void      PT_Disconnect(PT_Context *ctx, PT_Peer *peer);

/* ------------------------------------------------------------------ */
/* Messaging (3)                                                       */
/* ------------------------------------------------------------------ */

void      PT_RegisterMessage(PT_Context *ctx, unsigned char type,
                             PT_Transport transport);

PT_Status PT_Send(PT_Context *ctx, PT_Peer *peer,
                  unsigned char type, const void *data, size_t len);

PT_Status PT_Broadcast(PT_Context *ctx, unsigned char type,
                       const void *data, size_t len);

/* ------------------------------------------------------------------ */
/* Event loop (1)                                                      */
/* ------------------------------------------------------------------ */

void PT_Poll(PT_Context *ctx);

/* ------------------------------------------------------------------ */
/* Callback registration (6)                                           */
/* ------------------------------------------------------------------ */

void PT_OnPeerDiscovered(PT_Context *ctx, PT_PeerCallback cb,
                         void *user_data);

void PT_OnPeerLost(PT_Context *ctx, PT_PeerCallback cb,
                   void *user_data);

void PT_OnConnected(PT_Context *ctx, PT_PeerCallback cb,
                    void *user_data);

void PT_OnDisconnected(PT_Context *ctx, PT_DisconnectCallback cb,
                       void *user_data);

void PT_OnMessage(PT_Context *ctx, unsigned char type,
                  PT_MessageCallback cb, void *user_data);

void PT_OnError(PT_Context *ctx, PT_ErrorCallback cb,
                void *user_data);

/* ------------------------------------------------------------------ */
/* Configuration (1)                                                   */
/* ------------------------------------------------------------------ */

PT_Status PT_SetName(PT_Context *ctx, const char *name);

/* ------------------------------------------------------------------ */
/* Peer info (5)                                                       */
/* ------------------------------------------------------------------ */

int              PT_GetPeerCount(const PT_Context *ctx);
PT_Peer         *PT_GetPeer(PT_Context *ctx, int index);
const char      *PT_PeerName(const PT_Peer *peer);
const char      *PT_PeerAddress(const PT_Peer *peer);   /* static buf, valid until next call */
const char      *PT_LocalAddress(const PT_Context *ctx); /* static buf, valid until next call */
PT_Status        PT_SendUDPBroadcast(PT_Context *ctx, unsigned short port,
                                     const void *data, size_t len);
PT_PeerState     PT_GetPeerState(const PT_Peer *peer);

#ifdef __cplusplus
}
#endif

#endif /* PEERTALK_H */
