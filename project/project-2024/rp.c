/*---------------------------------------------------------------------------*/
#include "contiki.h"
#include "lib/random.h"
#include "net/netstack.h"
#include "net/rime/rime.h"
#include "core/net/linkaddr.h"
#include "lib/list.h"
/*---------------------------------------------------------------------------*/
#include <stdio.h> /* For printf */
// #include <stdbool.h>
#include <stdint.h> /* For numeric limits */
/*---------------------------------------------------------------------------*/
#define __DEBUG

#include "rp.h"
#include "log.h"
/*---------------------------------------------------------------------------*/
/* These look like reasonable parameters, change if needed */
#define MAX_NODES 36
#define MAX_HOPS 15
#define RSSI_THRESHOLD -95
#define BEACON_FORWARD_DELAY ((random_rand() % CLOCK_SECOND))
#define T_UPDATE_DELAY ((random_rand() % CLOCK_SECOND))
#define BEACON_INTERVAL (CLOCK_SECOND * 60) // TODO try different values
#define RECOVERY_INTERVAL (CLOCK_SECOND * 15) // TODO +?
#define EXPIRED_LIMIT (CLOCK_SECOND * 120)
/* Useful asserts */
// ! linkaddr_t has size 2 (avoids division) -- this should be true in default defines
_Static_assert(sizeof(linkaddr_t) == 2, "Change code if this fails");
/*---------------------------------------------------------------------------*/
/* What the fuck did you just bring upon this cursed land? */
#define cat(a,...) cat_impl(a, __VA_ARGS__)
#define cat_impl(a,...) a ## __VA_ARGS__

#define xCONF_nullrdc_driver 0
#define xCONF_contikimac_driver 1
#define CONF_VAL cat(xCONF_,NETSTACK_CONF_RDC)
/*---------------------------------------------------------------------------*/
/*                            FORWARD DECLARARIONS                           */
/*---------------------------------------------------------------------------*/
/* Forward declarations of callbacks */
void bc_recv(struct broadcast_conn *conn, const linkaddr_t *sender);
void uc_recv(struct unicast_conn *c, const linkaddr_t *from);
void beacon_timer_cb(void* ptr);
void unicast_timer_cb(void* ptr);
void recover_topology_cb(void* ptr);
/*---------------------------------------------------------------------------*/
/* Unicast message structures */
struct uc_header {
  uint8_t type : 1; // 0 == data, 1 == topology
  /* uint8_t pull : 1; // flag to pull data (scrapped) */
  uint8_t downwards : 1; // signals that the packet should be going down
  uint8_t hops : 6; // statically assert (max_hops <= 0x40)
} __attribute__((packed));
_Static_assert(sizeof(struct uc_header) == 1, "???");
_Static_assert(MAX_HOPS < 0x40, "MAX_HOPS too big, valid range: [0, 0x40)!!!\n");
/*---------------------------------------------------------------------------*/
/* Beacon message structure */
struct beacon_msg {
  uint16_t seqn;
  metric_t metric;
} __attribute__((packed));
/* Beacon message structure for network recovery */
struct beacon_recovery_msg {
  struct beacon_msg beacon;
  uint8_t expected_response;
} __attribute__((packed));
/*---------------------------------------------------------------------------*/
/* routing table entry */
struct nbr {
  struct nbr* next_ptr;
  linkaddr_t dest;
  linkaddr_t next;
  clock_time_t expires;
}; 
typedef struct nbr nbr;
/* routing table structure */
MEMB(nbr_mem, struct nbr, MAX_NODES);
LIST(nbr_list);
/*---------------------------------------------------------------------------*/
/* Rime Callback structures */
struct broadcast_callbacks bc_cb = {
  .recv = bc_recv,
  .sent = NULL
};
struct unicast_callbacks uc_cb = {
  .recv = uc_recv,
  .sent = NULL
};
/*---------------------------------------------------------------------------*/
/*                                   UTILS                                   */
/*---------------------------------------------------------------------------*/
/* Get an element from the nbr list, returns NULL if not found */
nbr* 
nbr_list_get(list_t l, const linkaddr_t* target) 
{
  nbr* tmp;
  for(tmp = list_head(l); tmp != NULL; tmp = list_item_next(tmp)) {
    /* Look for current nbr */
    if(linkaddr_cmp(&tmp->dest, target)) {
      break;
    }
  }
  return tmp;
}
/*---------------------------------------------------------------------------*/
/* push an element inside the list; if already present, the element gets
 * overwritten
 */
nbr* 
nbr_list_push(list_t l, const linkaddr_t* new, struct memb* m) {
  LOG("INFO: pushing %02x:%02x\n", new->u8[0], new->u8[1]);
  nbr* tmp = memb_alloc(&nbr_mem);
  if (tmp == NULL) {
    LOG("ERROR: MEMB insufficent memory, this should NOT be happening\n");
    return NULL; // it's broken anyway :(
  }
  linkaddr_copy(&tmp->dest, new);
  list_push(nbr_list, tmp);
  return tmp;
}
/*---------------------------------------------------------------------------*/
/*                                    API                                    */
/*---------------------------------------------------------------------------*/
/* initialise the rp_conn structure by opening the necessary connections 
 * and setting the default values. Also starts sink's beacon timer
 */
void
rp_open(struct rp_conn* conn, uint16_t channels, bool is_sink,
  const struct rp_callbacks *callbacks)
{
  /* default settings */
  linkaddr_copy(&conn->parent, &linkaddr_null);
  conn->metric = DEFAULT_METRIC_VALUE;
  conn->beacon_seqn = 0;
  conn->is_sink = is_sink;

  conn->callbacks = callbacks;
  conn->notified = false;

  /* Open the underlying Rime primitives */
  broadcast_open(&conn->bc, channels, &bc_cb);
  unicast_open(&conn->uc, channels + 1, &uc_cb);

  /* recovery timer if disconnected from network */
  if (!conn->is_sink) {
    ctimer_set(&conn->unicast_timer, RECOVERY_INTERVAL + CLOCK_SECOND, recover_topology_cb, conn);
    return;
  }

  /* sink metric is always 0 */
  conn->metric = 0; 
  /* Schedule the first beacon message flood */
  ctimer_set(&conn->beacon_timer, CLOCK_SECOND, beacon_timer_cb, conn);
}
/*---------------------------------------------------------------------------*/
/* application API to send messages */
int
rp_send(struct rp_conn *conn, const linkaddr_t *dest)
{
  // for some reason nodes can send a msg to themselves
  if (linkaddr_cmp(dest, &linkaddr_node_addr)) {
    conn->callbacks->recv(&linkaddr_node_addr, 0);
    return 0;
  }

  // assumes default size of addresses (2)
  const int expected_sz = sizeof(struct uc_header) + (sizeof(linkaddr_t) << 1);

  // packetbuf_clear(); // ! DO NOT DO THIS AS APP ALLOCS DATA
  if (packetbuf_hdralloc(expected_sz)) { 
    struct uc_header hdr = { .type = 0, .downwards = 0, .hops = 0 }; // Prepare the collection header
    memcpy(packetbuf_hdrptr(), &hdr, sizeof(struct uc_header)); 
    memcpy(packetbuf_hdrptr() + sizeof(struct uc_header), &linkaddr_node_addr, sizeof(linkaddr_t));
    memcpy(packetbuf_hdrptr() + sizeof(struct uc_header) + sizeof(linkaddr_t), dest, sizeof(linkaddr_t));
    nbr* tmp = nbr_list_get(nbr_list, dest);
    if (tmp != NULL) {
      return unicast_send(&conn->uc, &tmp->next);  
    }
    if (linkaddr_cmp(&conn->parent, &linkaddr_null)) {
      return -3;
    }
    return unicast_send(&conn->uc, &conn->parent);
  }
  return -1; // Inform the app that an issue with packetbuf_hdralloc() has occurred
}
/*---------------------------------------------------------------------------*/
/*                              Beacon Handling                              */
/*---------------------------------------------------------------------------*/
/* Send beacon with the current seqn and metric */
void
send_beacon(struct rp_conn* conn)
{
  /* Prepare the beacon message */
  struct beacon_msg beacon = {
    .seqn = conn->beacon_seqn, .metric = conn->metric
  };

  /* Send the beacon message in broadcast */
  packetbuf_clear();
  packetbuf_copyfrom(&beacon, sizeof(beacon));
  LOG("INFO: sending beacon: seqn %d\n",
    conn->beacon_seqn);
  broadcast_send(&conn->bc);
}
/*---------------------------------------------------------------------------*/
/* scan nbrs using a recovery beacon */
void
scan_nbrs(struct rp_conn* conn, uint8_t expected_response)
{
  struct beacon_recovery_msg beacon = {
    .beacon = {
      .seqn = conn->beacon_seqn, .metric = conn->metric
    },
    .expected_response = expected_response,
  };

  packetbuf_clear();
  packetbuf_copyfrom(&beacon, sizeof(beacon));
  LOG("INFO: sending recovery beacon: seqn %d\n",
    conn->beacon_seqn);
  broadcast_send(&conn->bc);
}
/*---------------------------------------------------------------------------*/
/* Beacon timer callback */
void 
beacon_timer_cb(void* ptr) 
{
  LOG("INFO: ctimer broadcast expired\n");

  /* --- Common nodes and sink logic --- */
  struct rp_conn* conn = (struct rp_conn*)ptr;
  send_beacon(conn);

  if (!conn->is_sink) 
    return;

  /* --- Sink-only logic: Rebuild the tree from scratch after the beacon interval --- */
  ++conn->beacon_seqn; /* Before beginning a new beacon flood, increase seqn */

  /* Schedule the next beacon message flood */
  /* The simulation shows that contikiMAC often loses the first table updates
   * leading to the first app packets being dropped at the sink, by flooding
   * multiple times during tree creation we mitigate this problem
   */
#if CONF_VAL == xCONF_contikimac_driver
  if (conn->beacon_seqn == 1) {
    ctimer_set(&conn->beacon_timer, 4 * CLOCK_SECOND, beacon_timer_cb, conn);
    return;
  }
#endif

  ctimer_set(&conn->beacon_timer, BEACON_INTERVAL, beacon_timer_cb, conn);
}
/*---------------------------------------------------------------------------*/
void 
bc_recv(struct broadcast_conn *bc_conn, const linkaddr_t *sender) 
{
  struct beacon_msg beacon;
  int16_t rssi;

  /* Get the pointer to the overall structure my_collect_conn from its field bc */
  struct rp_conn* conn = (struct rp_conn*)(((uint8_t*)bc_conn) - 
    offsetof(struct rp_conn, bc));

  if (packetbuf_datalen() != sizeof(struct beacon_msg) && 
    packetbuf_datalen() != sizeof(struct beacon_recovery_msg)) {
    LOG("ERROR: broadcast of wrong size\n");
    return;
  }
  memcpy(&beacon, packetbuf_dataptr(), sizeof(struct beacon_msg));

  rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);

  if (rssi < RSSI_THRESHOLD) 
    return;

  LOG("INFO: recv beacon from %02x:%02x seqn %u rssi %d\n", 
      sender->u8[0], sender->u8[1], 
      beacon.seqn, rssi);

  /* If it's a recovery request answer regardless of the seqn */
  if (packetbuf_datalen() == sizeof(struct beacon_recovery_msg)) {
    if (((uint8_t*)packetbuf_dataptr())[packetbuf_datalen() - 1]) {
      ctimer_set(&conn->beacon_timer, BEACON_FORWARD_DELAY, beacon_timer_cb, conn);
    }
  }

  /* update last seen of node */
  nbr* tmp = nbr_list_get(nbr_list, sender);
  if (tmp == NULL) {
    LOG("INFO: pushing from bc_recv\n");
    tmp = nbr_list_push(nbr_list, sender, &nbr_mem);
    linkaddr_copy(&tmp->next, sender); 
  }
  tmp->expires = clock_time();

  metric_t hop_cost = get_hop_cost();

  if ((beacon.seqn < conn->beacon_seqn && conn->beacon_seqn - beacon.seqn < UINT16_MAX - 10) || 
    (beacon.seqn == conn->beacon_seqn && beacon.metric + hop_cost >= conn->metric)){
    return; // worse or equal than what we have, ignore it
  }

  /* Otherwise, memorize the new parent, the metric, and the seqn */
  linkaddr_copy(&conn->parent, sender);
  conn->metric = beacon.metric + 1;
  conn->beacon_seqn = beacon.seqn;
  LOG("INFO: new parent %02x:%02x, my seqn %d\n", 
      sender->u8[0], sender->u8[1], conn->beacon_seqn);
  conn->notified = false; // exploit piggybacking when possible

  /* Schedule beacon propagation */
  ctimer_set(&conn->beacon_timer, BEACON_FORWARD_DELAY, beacon_timer_cb, conn);
  /* Periodic topology update is after beacon recv */
  ctimer_set(&conn->unicast_timer, CLOCK_SECOND + T_UPDATE_DELAY, unicast_timer_cb, conn);
}
/*---------------------------------------------------------------------------*/
/*                             Unicast Handling                              */
/*---------------------------------------------------------------------------*/
/* recovery timer callback */
void
recover_topology_cb(void* ptr)
{
  LOG("WARN: attempt to recover topology\n");

  struct rp_conn* conn = (struct rp_conn*)ptr;
  /* If no beacon has been received in the last interval request info from nbrs */
  scan_nbrs(conn, 1);
  // reset timer for another recovery attempt if previous one fails
  ctimer_set(&conn->unicast_timer, RECOVERY_INTERVAL, recover_topology_cb, conn);
}
/*---------------------------------------------------------------------------*/
/* send a topology report */
void
send_topolgy_update(struct rp_conn* conn)
{
  if (linkaddr_cmp(&conn->parent, &linkaddr_null)) { // The node is still disconnected 
    LOG("ERROR: trying to send a topology update without having a parent!\n");
    return; // Should not be happening unless entry expires
  }

  packetbuf_clear();
  if (packetbuf_copyfrom(&linkaddr_node_addr, sizeof(linkaddr_t)) != sizeof(linkaddr_t)) {
    LOG("ERROR: topology update too big to fit in packet!\n"); // should not be happening since we have max 36 nodes
    return;
  }

  if (packetbuf_hdralloc(sizeof(struct uc_header))) { 
    struct uc_header hdr = { .type = 1, .downwards = 0, .hops = 0 }; // Prepare the collection header
    memcpy(packetbuf_hdrptr(), &hdr, sizeof(hdr)); 
    unicast_send(&conn->uc, &conn->parent);
  }
  /* Reset the timer to fire if no beacon is heard in the next interval */
  ctimer_set(&conn->unicast_timer, BEACON_INTERVAL + RECOVERY_INTERVAL, recover_topology_cb, conn);
  return; 
}
/*---------------------------------------------------------------------------*/
/* topology report callback */
void 
unicast_timer_cb(void* ptr) 
{
  LOG("INFO: ctimer unicast expired\n");

  /* --- Common nodes and sink logic --- */
  struct rp_conn* conn = (struct rp_conn*)ptr;

  if (conn->notified) { // info was piggybacked
    return;
  }

  if (conn->is_sink) {
    LOG("ERROR: sink trying to send a topology update!?\n");
    return;
  }

  send_topolgy_update(conn);
}
/*---------------------------------------------------------------------------*/
/* handles an application message */
void
rp_msg_handle(struct rp_conn* conn, const linkaddr_t* from, struct uc_header* hdr)
{
  const int expected_sz = sizeof(struct uc_header) + 
    (sizeof(linkaddr_t) << 1);

  // check if packet looks consistent
  if (packetbuf_datalen() < expected_sz) {
    LOG("ERROR: too short header unicast for data packet %d\n", packetbuf_datalen());
    return;
  }

  linkaddr_t src;
  memcpy(&src, packetbuf_dataptr() + sizeof(struct uc_header), sizeof(linkaddr_t));
  
  /* Exploit data packets to update routing info if they are going upwards */
  if (!hdr->downwards) {
    nbr* tmp = nbr_list_get(nbr_list, &src);
    if (tmp == NULL) {
      LOG("INFO: pushing from rp_handle\n");
      tmp = nbr_list_push(nbr_list, &src, &nbr_mem);
      linkaddr_copy(&tmp->next, from);
    }
    tmp->expires = clock_time();
  }
  /* I'm the destination */
  linkaddr_t dest;
  memcpy(&dest, packetbuf_dataptr() + sizeof(struct uc_header) + sizeof(linkaddr_t), sizeof(linkaddr_t));
  if (linkaddr_cmp(&dest, &linkaddr_node_addr)) {
    packetbuf_hdrreduce(expected_sz);
    conn->callbacks->recv(&src, hdr->hops);
    return;
  }

  /* Forward up (or down) the message */
  nbr* tmp = nbr_list_get(nbr_list, &dest);
  if (tmp != NULL) {
    hdr->downwards = 1; // OLD WAS DOWN THERE
    if (clock_time() - tmp->expires > EXPIRED_LIMIT) {
      // scapped
      /* Sink can try to request for information update if actual info is stale */
      /*
      // LOG("INFO: requesting info pull");
      // hdr->pull = 1;
      */
      if (conn->is_sink) {
        // detected inconsistency, trigger routing update
        LOG("WARN: reset beacon interval\n");
        ctimer_set(&conn->beacon_timer, 10 * CLOCK_SECOND, beacon_timer_cb, conn);
      } else {
        LOG("WARN: stale info, removing entry...\n");
        list_remove(nbr_list, tmp);
        memb_free(&nbr_mem, tmp);
      }
      // still try to send
      memcpy(packetbuf_dataptr(), hdr, sizeof(struct uc_header));
      unicast_send(&conn->uc, &tmp->next);
      return;
    } else {
      memcpy(packetbuf_dataptr(), hdr, sizeof(struct uc_header));
      unicast_send(&conn->uc, &tmp->next);
      return;
    }
  }

  if (conn->is_sink) {
    LOG("ERROR: sink couldn't find path, attempt to fix topology!\n");
    ctimer_set(&conn->beacon_timer, 10 * CLOCK_SECOND, beacon_timer_cb, conn);
    return;
  }

  if (linkaddr_cmp(&conn->parent, &linkaddr_null)) {
    // node is still disconnected
    LOG("WARN: dropping packet, parent is NULL\n");
    return;
  }

  /* If the packet started going down and now is going up we have a loop, try to fix locally */
  if (hdr->downwards) {
    LOG("ERROR: loop detected, dropping packet\n");
    scan_nbrs(conn, 1); // try to fix locally
    return;
  }

  memcpy(packetbuf_dataptr(), hdr, sizeof(struct uc_header));
  unicast_send(&conn->uc, &conn->parent);
}
/*---------------------------------------------------------------------------*/
/* handles a topology update */
void
t_update_handle(struct rp_conn* conn, const linkaddr_t* from, const struct uc_header* hdr)
{
  LOG("INFO: recv t update from %02x:%02x\n", from->u8[0], from->u8[1]);

  // check if packet looks consistent
  if (packetbuf_datalen() < sizeof(struct uc_header) + sizeof(linkaddr_t)) {
    printf("ERROR: too short unicast t update packet %i\n", packetbuf_datalen());
    return;
  }

  // ! linkaddr_t has size 2 (avoids division) -- this should be true in default defines
  uint8_t len = (packetbuf_datalen() - sizeof(struct uc_header)) >> 1;
  // assert(len % sizeof(linkaddr_t) == 0);
  linkaddr_t chain[len + 1]; // scary alloca() :(
  linkaddr_copy(&chain[len], &linkaddr_node_addr);

  nbr* tmp;
  uint8_t i;

  memcpy(chain, packetbuf_dataptr() + sizeof(struct uc_header), len << 1);
  for (i = 0; i < len; ++i) {
    tmp = nbr_list_get(nbr_list, &chain[i]);
    if (tmp == NULL) {
      LOG("INFO: pushing from t_handle\n");
      tmp = nbr_list_push(nbr_list, &chain[i], &nbr_mem);
    } 
    // update every entry
    linkaddr_copy(&tmp->next, from);
    tmp->expires = clock_time();
  }

  // sink has no need to forward packet
  if (conn->is_sink) {
    return;
  }

  // mere debug print
  if (!conn->notified) {
    LOG("INFO: exploting piggybacking!\n");
  }

  len += !conn->notified;
  conn->notified = true;
  // piggyback info if parent is still in need of notify 
  // for some reason setdatalen doesn't seem to work so we do this instead :(
  if (packetbuf_copyfrom(chain, len << 1) != len << 1) {
    LOG("ERROR: topology update too big to send!\n");
    return;
  }
  if (!packetbuf_hdralloc(sizeof(struct uc_header))) {
    LOG("ERROR: topology update too big to send! (header)\n");
    return;
  }
  memcpy(packetbuf_hdrptr(), hdr, sizeof(struct uc_header));

  LOG("INFO: forwarding t update started from %02x:%02x\n",
    chain[0].u8[0], chain[0].u8[1]);
  unicast_send(&conn->uc, &conn->parent);
}
/*---------------------------------------------------------------------------*/
/* unicast recv callback */
void 
uc_recv(struct unicast_conn *c, const linkaddr_t *from) 
{
  /* Get the pointer to the overall structure my_collect_conn from its field uc */
  struct rp_conn* conn = (struct rp_conn*)(((uint8_t*)c) - 
    offsetof(struct rp_conn, uc));
  // bool flag_pull;
  struct uc_header hdr;

  /* Check if the received unicast message looks legitimate */
  if (packetbuf_datalen() < sizeof(struct uc_header)) {
    printf("ERROR: too short unicast packet header %d\n", packetbuf_datalen());
    return;
  }

  memcpy(&hdr, packetbuf_dataptr(), sizeof(hdr));
  if (hdr.hops == MAX_HOPS) {
    LOG("WARN: loop detected, dropping packet\n");
    scan_nbrs(conn, 1); // try to fix locally
    return;
  }
  ++hdr.hops; // to avoid unsigned overflow on a ++;
  // flag_pull = hdr.pull;

  if (!hdr.type) {
    // linkaddr_t src = *(linkaddr_t*)(packetbuf_dataptr() + sizeof(hdr) + sizeof(linkaddr_t));
    rp_msg_handle(conn, from, &hdr);
  } else {
    t_update_handle(conn, from, &hdr);
  }
}
/*---------------------------------------------------------------------------*/
