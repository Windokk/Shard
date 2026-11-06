/* Interface "shard.net" 1.0 : UDP and TCP sockets, for the networking a game builds itself.
 *
 * The engine does not impose a protocol, a topology or a replication scheme : a plugin brings the ones its game wants (client/server, lockstep,
 * rollback, a lobby) and this gives it the transport. The sockets are non blocking and driven by the host : it reads and writes them on the
 * frame, and tells the plugin what happened through ONE callback per socket, on the main thread, so a plugin's networking code runs in the same
 * thread as the simulation it feeds and needs no locks. The host polls at the start of every frame ; poll() delivers the events that arrived
 * since, for the plugin that wants to read just before a fixed step.
 *
 * Reliability, ordering, fragmentation, encryption and NAT traversal are the plugin's: UDP gives datagrams (up to 65507 bytes, no promise), TCP
 * a stream (events carry whatever chunk arrived : frame your messages). Addresses are IPv4. Name resolution happens on a worker, never in the
 * frame.
 *
 * A plugin that uses it declares the permission "net" in its manifest (a native plugin is trusted whatever it declares, but the user is told ;
 * a sandboxed plugin is not offered this interface). The host caps a plugin at 64 sockets, 1 MiB queued per TCP socket and 256 events a socket
 * per poll (the rest comes with the next poll) ; sockets are closed with the plugin (disable or unload).
 *
 * To keep two peers' simulations the same, number your messages with the tick of shard.time (`fixedStep`) and compare shard.time's state_hash.
 *
 * thread : main, unless a function says otherwise. A sandboxed plugin (tier 2) is not offered this interface. */
#ifndef SHARD_NET_H
#define SHARD_NET_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_NET "shard.net"

typedef uint64_t ShardSocket;       /* 0 is never valid */

typedef struct ShardNetAddress {
    uint32_t structSize;
    uint32_t port;                  /* 0 to bind : any free port (local_port tells which) */
    ShardStr host;                  /* a dotted address, a name ("example.com"), or empty : any local address (to bind) */
} ShardNetAddress;

#define SHARD_NET_EVENT_CONNECTED   1u  /* tcp_connect: the connection is established */
#define SHARD_NET_EVENT_ACCEPTED    2u  /* a listener got a client : `socket` is the new connection (it uses the listener's callback), `peer` its address */
#define SHARD_NET_EVENT_DATA        3u  /* data arrived : a chunk of a TCP stream, or one UDP datagram (`peer` is its sender) */
#define SHARD_NET_EVENT_CLOSED      4u  /* the peer closed the connection (TCP) ; the socket is closed afterwards */
#define SHARD_NET_EVENT_ERROR       5u  /* the connect failed, or the socket broke : `error` is the system's code. The socket is closed afterwards. */

typedef struct ShardNetEvent {
    uint32_t structSize;
    uint32_t kind;                  /* SHARD_NET_EVENT_* */
    ShardSocket socket;
    ShardSocket listener;           /* ACCEPTED : the listening socket */
    ShardStr peer;                  /* "192.0.2.7:4000" ; valid for the call */
    const void* data;               /* DATA : valid for the call */
    uint64_t size;
    int32_t  error;
    int32_t  reserved;
} ShardNetEvent;

/* thread : main. The socket may be closed from inside. */
typedef void (SHARD_CALL *ShardNetFn)(void* user, ShardPluginCtx ctx, const ShardNetEvent* event);

#define SHARD_NET_OPT_NODELAY       1u  /* TCP : send small writes at once (no Nagle). Default on. */
#define SHARD_NET_OPT_BROADCAST     2u  /* UDP : may send to a broadcast address */
#define SHARD_NET_OPT_REUSE_ADDR    3u
#define SHARD_NET_OPT_RECV_BUFFER   4u  /* bytes the system buffers for reading */
#define SHARD_NET_OPT_SEND_BUFFER   5u

typedef struct ShardNetApi {
    uint32_t structSize;

    /* Opens a UDP socket bound to `bind` (NULL : any local address and port). SHARD_E_ALREADY_EXISTS : the address is in use. */
    ShardResult (SHARD_CALL *udp_open)(ShardPluginCtx ctx, const ShardNetAddress* bind, ShardNetFn fn, void* user, ShardSocket* out);
    /* Listens for TCP connections on `bind`. Each client is announced with SHARD_NET_EVENT_ACCEPTED. */
    ShardResult (SHARD_CALL *tcp_listen)(ShardPluginCtx ctx, const ShardNetAddress* bind, int32_t backlog, ShardNetFn fn, void* user, ShardSocket* out);
    /* Starts connecting to `remote` (resolving the name first) : the result is an event, CONNECTED or ERROR. The handle is usable for send at
     * once : what is sent before the connection is established is queued, and dropped if it fails. */
    ShardResult (SHARD_CALL *tcp_connect)(ShardPluginCtx ctx, const ShardNetAddress* remote, ShardNetFn fn, void* user, ShardSocket* out);

    /* TCP: queues the bytes (copied). SHARD_E_OUT_OF_MEMORY : the socket's queue would pass its limit -- the bytes are NOT queued, send fewer or
     * later (queued_bytes tells). */
    ShardResult (SHARD_CALL *send)(ShardPluginCtx ctx, ShardSocket socket, const void* data, uint64_t size);
    /* UDP: sends one datagram. SHARD_E_INVALID_ARG : more than 65507 bytes. A datagram the system cannot take right now is dropped, as UDP allows. */
    ShardResult (SHARD_CALL *send_to)(ShardPluginCtx ctx, ShardSocket socket, const ShardNetAddress* remote, const void* data, uint64_t size);
    /* Closes it ; no event follows for a socket the plugin closed itself. SHARD_E_STALE_HANDLE : already closed. */
    ShardResult (SHARD_CALL *close)(ShardPluginCtx ctx, ShardSocket socket);

    /* The port a bound socket has (after binding port 0, the one the system chose). */
    ShardResult (SHARD_CALL *local_port)(ShardPluginCtx ctx, ShardSocket socket, uint32_t* port);
    /* Bytes still waiting in the TCP send queue. */
    ShardResult (SHARD_CALL *queued_bytes)(ShardPluginCtx ctx, ShardSocket socket, uint64_t* bytes);
    ShardResult (SHARD_CALL *set_option)(ShardPluginCtx ctx, ShardSocket socket, uint32_t option, int32_t value);

    /* Reads every socket of this plugin now and delivers the events that arrived. Also flushes the send queues. */
    ShardResult (SHARD_CALL *poll)(ShardPluginCtx ctx);
} ShardNetApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_NET_H */
