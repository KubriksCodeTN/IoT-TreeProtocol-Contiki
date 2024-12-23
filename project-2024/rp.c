/*
 *
 * Student Name: Patrick Pastorelli
 *
 */
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
/* These look like reasonable parameters, change if needed */
#define MAX_NODES 36
#define MAX_HOPS 10
#define RSSI_THRESHOLD -95
#define BEACON_FORWARD_DELAY ((random_rand() % CLOCK_SECOND))
#define T_UPDATE_DELAY ((random_rand() % CLOCK_SECOND))
#define BEACON_INTERVAL (CLOCK_SECOND * 60) // TODO try different values
/* Useful asserts */
_Static_assert(MAX_HOPS < 0x80, "MAX_HOPS too big, valid range: [0, 0x80)!!!\n");
/*---------------------------------------------------------------------------*/
/* What the fuck did you just bring upon this cursed land? */
#define cat(a,...) cat_impl(a, __VA_ARGS__)
#define cat_impl(a,...) a ## __VA_ARGS__

#define xCONF_nullrdc_driver 0
#define xCONF_contikimac_driver 1
#define CONF_VAL cat(xCONF_,NETSTACK_CONF_RDC)
/*---------------------------------------------------------------------------*/
#include "rp.h"
#include "log.h"
/*---------------------------------------------------------------------------*/
/* Forward declarations of callbacks */
void bc_recv(struct broadcast_conn *conn, const linkaddr_t *sender);
void uc_recv(struct unicast_conn *c, const linkaddr_t *from);
void beacon_timer_cb(void* ptr);
void unicast_timer_cb(void* ptr);
/*---------------------------------------------------------------------------*/
/* Forward declarations of strcts */
// TODO move
struct uc_header {
  uint8_t type : 1; // 0 == data, 1 == topology
  uint8_t hops : 7; // statically assert (max_hops <= 0x80)
} __attribute__((packed));
/*---------------------------------------------------------------------------*/
/* I have trouble finding the hbr-table doc so we try a Contiki list */
struct nbr {
  struct nbr* next_ptr;
  linkaddr_t dest;
  linkaddr_t next;
  clock_time_t /* uint16_t */ expries;
}; 
typedef struct nbr nbr;

MEMB(nbr_mem, struct nbr, MAX_NODES);
LIST(nbr_list);
/*---------------------------------------------------------------------------*/
/* Rime Callback structures */
// TODO
struct broadcast_callbacks bc_cb = {
  .recv = bc_recv,
  .sent = NULL
};
struct unicast_callbacks uc_cb = {
  .recv = uc_recv,
  .sent = NULL
};
/*---------------------------------------------------------------------------*/
// TODO
inline void 
get_metric() 
{

}
/*---------------------------------------------------------------------------*/
/*                                   UTILS                                   */
/*---------------------------------------------------------------------------*/
nbr* 
list_get(list_t l, const linkaddr_t* target) 
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
/*                                    API                                    */
/*---------------------------------------------------------------------------*/
void
rp_open(struct rp_conn* conn, uint16_t channels, bool is_sink,
  const struct rp_callbacks *callbacks)
{
  /* default settings */
  linkaddr_copy(&conn->parent, &linkaddr_null);
  conn->metric = UINT16_MAX;
  conn->beacon_seqn = 0;
  conn->is_sink = is_sink;
  conn->callbacks = callbacks;
  conn->notified = false;

  /* Open the underlying Rime primitives */
  broadcast_open(&conn->bc, channels, &bc_cb);
  unicast_open(&conn->uc, channels + 1, &uc_cb);

  if (!conn->is_sink) 
    return;

  /* sink metric is always 0 */
  conn->metric = 0; 
  /* Schedule the first beacon message flood */
  ctimer_set(&conn->beacon_timer, CLOCK_SECOND, beacon_timer_cb, conn);
}
/*---------------------------------------------------------------------------*/
int
rp_send(struct rp_conn *conn, const linkaddr_t *dest)
{
  // for some reason nodes can send a msg to themselves
  if (linkaddr_cmp(dest, &linkaddr_node_addr)) {
    conn->callbacks->recv(&linkaddr_node_addr, 0);
    return 0;
  }

  const int expected_sz = sizeof(struct uc_header) + (sizeof(linkaddr_t) << 1);

  // packetbuf_clear(); // ! DO NOT DO THIS AS APP ALLOCS DATA
  if (packetbuf_hdralloc(expected_sz)) { 
    struct uc_header hdr = { .type = 0, .hops = 0 }; // Prepare the collection header
    memcpy(packetbuf_hdrptr(), &hdr, sizeof(struct uc_header)); 
    memcpy(packetbuf_hdrptr() + sizeof(struct uc_header), &linkaddr_node_addr, sizeof(linkaddr_t));
    memcpy(packetbuf_hdrptr() + sizeof(struct uc_header) + sizeof(linkaddr_t), dest, sizeof(linkaddr_t));
    nbr* tmp = list_get(nbr_list, dest);
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
/* Beacon message structure */
struct beacon_msg {
  uint16_t seqn;
  uint16_t metric;
} __attribute__((packed));
/*---------------------------------------------------------------------------*/
/* Send beacon using the current seqn and metric */
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
  LOG("INFO: sending beacon: seqn %d metric %d\n",
    conn->beacon_seqn, conn->metric);
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

  /* Check if the received broadcast packet looks legitimate */
  if (packetbuf_datalen() != sizeof(struct beacon_msg)) {
    LOG("ERROR: broadcast of wrong size\n");
    return;
  }
  memcpy(&beacon, packetbuf_dataptr(), sizeof(struct beacon_msg));

  rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
  /*
  {
    radio_value_t v;
    NETSTACK_RADIO.get_value(RADIO_PARAM_LAST_RSSI, &v);
    rssi = v;
  }
  */

  LOG("INFO: recv beacon from %02x:%02x seqn %u metric %u rssi %d\n", 
      sender->u8[0], sender->u8[1], 
      beacon.seqn, beacon.metric, rssi);

  // TODO Attempt to handle seqn overflow
  /*
  bool check = ((conn->beacon_seqn - beacon.seqn >= UINT16_MAX - 500) || (beacon.seqn > conn->beacon_seqn));
	if (rssi < RSSI_THRESHOLD || !check) // Either too weak or too old
		return;
  */
  if (rssi < RSSI_THRESHOLD || beacon.seqn < conn->beacon_seqn)
    return;

  nbr* tmp = list_get(nbr_list, sender);

  if (tmp == NULL) {
    tmp = memb_alloc(&nbr_mem);
    if (tmp == NULL) {
      LOG("ERROR: MEMB insufficent memory, this should NOT be happening\n");
      return; // it's broken anyway :(
    }
    linkaddr_copy(&tmp->dest, sender);
    // TODO expires
    list_push(nbr_list, tmp);
    linkaddr_copy(&tmp->next, sender); // TODO HERE? 
  }

  /* Sink doesn't need parents */
  /*
  if (conn->is_sink)
    return;
   */
  
  if (beacon.seqn == conn->beacon_seqn && beacon.metric + 1 >= conn->metric)
    return; // worse or equal than what we have, ignore it

/*
  since entries expire this is bad
  if (linkaddr_cmp(&conn->parent, sender))
    return; // ignore reassignment of same parent 
  */

  /* Otherwise, memorize the new parent, the metric, and the seqn */
  linkaddr_copy(&conn->parent, sender);
  conn->metric = beacon.metric + 1;
  conn->beacon_seqn = beacon.seqn;
  LOG("INFO: new parent %02x:%02x, my metric %d, my seqn %d\n", 
      sender->u8[0], sender->u8[1], conn->metric, conn->beacon_seqn);
  conn->notified = false; // exploit piggybacking when possible

  /* Schedule beacon propagation */
  uint16_t delay = BEACON_FORWARD_DELAY;
  ctimer_set(&conn->beacon_timer, delay, beacon_timer_cb, conn);
  // TODO try to delay lower rank nodes to exploit piggybacking
  ctimer_set(&conn->unicast_timer, CLOCK_SECOND + T_UPDATE_DELAY, unicast_timer_cb, conn);
}
/*---------------------------------------------------------------------------*/
/*                             Unicast Handling                              */
/*---------------------------------------------------------------------------*/
/* Unicast message structures */
/*
struct uc_header {
  uint8_t type : 1; // 0 == data, 1 == topology
  uint8_t hops : 7; // statically assert (max_hops <= 0x80)
} __attribute__((packed));
*/
// _Static_assert(sizeof(struct uc_header) == 1, "???");
/*---------------------------------------------------------------------------*/
void
send_topolgy_update(struct rp_conn* conn)
{
  if (linkaddr_cmp(&conn->parent, &linkaddr_null)) { // The node is still disconnected 
    LOG("ERROR: trying to send a topology update without having a parent!\n");
    return; // Should not be happening unless entry expires
  }

  packetbuf_clear(); // TODO check if races are possible
  if (packetbuf_copyfrom(&linkaddr_node_addr, sizeof(linkaddr_t)) != sizeof(linkaddr_t)) {
    LOG("ERROR: topology update too big to fit in packet!\n"); // should not be happening since we have max 36 nodes
    return;
  }

  if (packetbuf_hdralloc(sizeof(struct uc_header))) { 
    struct uc_header hdr = { .type = 1, .hops = 0 }; // Prepare the collection header
    memcpy(packetbuf_hdrptr(), &hdr, sizeof(hdr)); 
    unicast_send(&conn->uc, &conn->parent);
  }
  return; // Inform the app that an issue with packetbuf_hdralloc() has occurred
}
/*---------------------------------------------------------------------------*/
void 
unicast_timer_cb(void* ptr) 
{
  LOG("INFO: ctimer unicast expired\n");

  /* --- Common nodes and sink logic --- */
  struct rp_conn* conn = (struct rp_conn*)ptr;

  if (conn->notified) {
    return;
  }

  if (conn->is_sink) {
    LOG("ERROR: sink trying to send a topology update!?\n");
    return;
  }

  send_topolgy_update(conn);
}
/*---------------------------------------------------------------------------*/
void
rp_msg_handle(struct rp_conn* conn, const linkaddr_t* from, const struct uc_header* hdr)
{
  const int expected_sz = sizeof(struct uc_header) + 
    (sizeof(linkaddr_t) << 1);

  // check if packet looks consistent
  if (packetbuf_datalen() < expected_sz) {
    LOG("ERROR: too short header unicast for data packet %d\n", packetbuf_datalen());
    return;
  }

  // TODO adjust later
  linkaddr_t dest;
  memcpy(&dest, packetbuf_dataptr() + sizeof(struct uc_header) + sizeof(linkaddr_t), sizeof(linkaddr_t));
  if (linkaddr_cmp(&dest, &linkaddr_node_addr)) {
    linkaddr_t src;
    memcpy(&src, packetbuf_dataptr() + sizeof(struct uc_header), sizeof(linkaddr_t));
    packetbuf_hdrreduce(expected_sz);
    conn->callbacks->recv(&src, hdr->hops);
    return;
  }

  memcpy(packetbuf_dataptr(), hdr, sizeof(struct uc_header));

  nbr* tmp = list_get(nbr_list, &dest);
  if (tmp != NULL) {
    unicast_send(&conn->uc, &tmp->next);
    return;
  }

  if (conn->is_sink) {
    LOG("ERROR: sink couldn't find path!\n");
    return;
  }

  if (linkaddr_cmp(&conn->parent, &linkaddr_null)) {
    LOG("WARN: dropping packet, parent is NULL");
    return;
  }

  unicast_send(&conn->uc, &conn->parent);
}
/*---------------------------------------------------------------------------*/
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

/*
  for (i = 0; i < len; ++i) {
    LOG("CHAIN: %02x:%02x\n", chain[i].u8[0], chain[i].u8[1]);
  }
 */

  for (i = 0; i < len; ++i) {
    tmp = list_get(nbr_list, &chain[i]);
    if (tmp == NULL) {
      tmp = memb_alloc(&nbr_mem);
      if (tmp == NULL) {
        LOG("ERROR: MEMB insufficent memory, this should NOT be happening\n");
        return; // it's broken anyway :(
      }
      linkaddr_copy(&tmp->dest, &chain[i]);
      // TODO expires
      list_push(nbr_list, tmp);
      linkaddr_copy(&tmp->next, from);
    } 
    // TODO always update? Or go for min route? Prob depends on metric
    else if (!linkaddr_cmp(&tmp->next, &linkaddr_node_addr)) {
      linkaddr_copy(&tmp->next, from);
    }
  }

  // sink has no need to forward packet
  if (conn->is_sink) {
    /*
    LOG("TABLE: START\n");
    for(tmp = list_head(nbr_list); tmp != NULL; tmp = list_item_next(tmp)) {
      LOG("TABLE: %02x:%02x - %02x:%02x\n", tmp->dest.u8[0], tmp->dest.u8[1],
        tmp->next.u8[0], tmp->next.u8[1]);
    }
    LOG("TABLE: END\n");
     */
    return;
  }

  // TODO remove piggybacking if packet too big
  if (!conn->notified) {
    LOG("INFO: exploting piggybacking!");
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

/*
  linkaddr_t chain1[len + 1];
  memcpy(chain1, packetbuf_dataptr() + sizeof(struct uc_header), len << 1);

  LOG("CHAIN START\n");
  for (i = 0; i < len + 1; ++i) {
    LOG("CHAIN: %02x:%02x\n", chain1[i].u8[0], chain1[i].u8[1]);
  }
  LOG("CHAIN END\n");
 */

  LOG("INFO: forwarding t update started from %02x:%02x\n",
    chain[0].u8[0], chain[0].u8[1]);
  unicast_send(&conn->uc, &conn->parent);
  
/*
  LOG("TABLE: START\n");
  for(tmp = list_head(nbr_list); tmp != NULL; tmp = list_item_next(tmp)) {
    LOG("TABLE: %02x:%02x - %02x:%02x\n", tmp->dest.u8[0], tmp->dest.u8[1],
      tmp->next.u8[0], tmp->next.u8[1]);
  }
  LOG("TABLE: END\n");
 */
}
/*---------------------------------------------------------------------------*/
void 
uc_recv(struct unicast_conn *c, const linkaddr_t *from) 
{
  /* Get the pointer to the overall structure my_collect_conn from its field uc */
  struct rp_conn* conn = (struct rp_conn*)(((uint8_t*)c) - 
    offsetof(struct rp_conn, uc));

  struct uc_header hdr;
  /* Check if the received unicast message looks legitimate */
  if (packetbuf_datalen() < sizeof(struct uc_header)) {
    printf("ERROR: too short unicast packet header %d\n", packetbuf_datalen());
    return;
  }

  memcpy(&hdr, packetbuf_dataptr(), sizeof(hdr));
  if (hdr.hops == MAX_HOPS) {
    LOG("WARN: loop detected, dropping packet\n");
    return;
  }
  ++hdr.hops; // to avoid unsigned overflow on a ++;

  if (!hdr.type) {
    rp_msg_handle(conn, from, &hdr);
  } else {
    t_update_handle(conn, from, &hdr);
  }
}
/*---------------------------------------------------------------------------*/
