#ifndef __RP_H__
#define __RP_H__
/*---------------------------------------------------------------------------*/
#include <stdbool.h>
#include "contiki.h"
#include "net/rime/rime.h"
#include "net/netstack.h"
#include "core/net/linkaddr.h"
/*---------------------------------------------------------------------------*/
/* Callback structure */
struct rp_callbacks {
  /* The recv function should be called when a node receives an any-to-any
   * packet (i.e., a packet sent by the application using rp_send function).
   *
   * Arguments: 
   * const linkaddr_t *src: source of any-to-any message
   * uint8_t hops: number of hops from source to final destination
   */
  void (* recv)(const linkaddr_t *src, uint8_t hops);
};
/*---------------------------------------------------------------------------*/
/* Connection data structure for the routing protocol */
struct rp_conn {
  struct broadcast_conn bc;
  struct unicast_conn uc;
  const struct rp_callbacks* callbacks;
  linkaddr_t parent;
  struct ctimer beacon_timer;
  uint16_t metric;
  uint16_t beacon_seqn;
  bool is_sink;
  /* Add below whatever other connection parameters you may need to implement
   * your any-to-any routing protocol
   */
  struct ctimer unicast_timer;
  // bool notified;
};
/*---------------------------------------------------------------------------*/
/* Initialize a routing protocol connection 
 *  - conn -- a pointer to a connection object 
 *  - channels -- starting channel C
 *  - is_sink -- initialize in either sink or router mode
 *  - callbacks -- a pointer to the callback structure
 */
void rp_open(
  struct rp_conn* conn, 
  uint16_t channels, 
  bool is_sink,
  const struct rp_callbacks *callbacks);
/*---------------------------------------------------------------------------*/
/* Send packet to a given destination dest 
 * Arguments: 
 * struct rp_conn *c: a pointer to a connection object 
 * const linkaddr_t *dest: the final link layer destination address to send the
 *                         the message to.
 * Return value:
 * Non-zero if the packet could be sent, zero otherwise
 */
int rp_send(struct rp_conn *c, const linkaddr_t *dest);
/*---------------------------------------------------------------------------*/
#endif /* __RP_H__ */
