#include "contiki.h"
#include "lib/random.h"
#include "net/netstack.h"
#include "net/rime/rime.h"
#include "core/net/linkaddr.h"
/*---------------------------------------------------------------------------*/
#include <stdio.h> /* For printf */
/*---------------------------------------------------------------------------*/
#include "rp.h"

#if CONTIKI_TARGET_ZOUL
#include "deployment.h"
#endif

#include "simple-energest.h"

/*---------------------------------------------------------------------------*/
#define MSG_PERIOD (30 * CLOCK_SECOND)  // send every 30 seconds
#define COLLECT_CHANNEL 0xAA
/*---------------------------------------------------------------------------*/
#if CONTIKI_TARGET_ZOUL
linkaddr_t sink = {{0xd9, 0x5f}}; /* SINK address */
#else
linkaddr_t sink = {{0x01, 0x00}}; /* SINK address */
#endif
linkaddr_t dest = {{0x00, 0x00}}; /* Destination address */
/*---------------------------------------------------------------------------*/
PROCESS(app_process, "App process");
AUTOSTART_PROCESSES(&app_process);
/*---------------------------------------------------------------------------*/
/* Application packet */
typedef struct {
  uint16_t seqn;
}
__attribute__((packed))
test_msg_t;
/*---------------------------------------------------------------------------*/
static struct rp_conn conn; /* Connection structure */
/*---------------------------------------------------------------------------*/
/* Routing recv callback declarations */
static void recv_cb(const linkaddr_t *originator, uint8_t hops);
/*---------------------------------------------------------------------------*/
struct rp_callbacks cb = {
  .recv = recv_cb
};
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(app_process, ev, data) 
{
  static struct etimer rnd;
  static struct etimer periodic;
  static test_msg_t msg = {.seqn = 0};

  PROCESS_BEGIN();

  simple_energest_start();

  /* Open routing protocol connection */
  if(linkaddr_cmp(&sink, &linkaddr_node_addr)) {
    /* Sink: open Routing Protocol connection as sink */
    printf("App: I am sink %02x:%02x\n",
      linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    rp_open(&conn, COLLECT_CHANNEL, true, &cb);
  } else {
    /* Normal node */
    printf("App: I am normal node %02x:%02x\n",
      linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    rp_open(&conn, COLLECT_CHANNEL, false, &cb);
  }

  /* Wait MSG_PERIOD before start sending messages */
  etimer_set(&periodic, MSG_PERIOD);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
    /* Fixed interval */
    etimer_reset(&periodic);
    /* Random shift within the second half of the interval */
    etimer_set(&rnd, (MSG_PERIOD / 2) + random_rand() % (MSG_PERIOD / 2));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&rnd));

#if CONTIKI_TARGET_ZOUL
	uint16_t node_id;
	static uint16_t node_ids[] = {
		//	 1,  2,  3,  4,  5,  6,  7,  8,  9,
		//10, 11, 
		        12, 13, 14, 15, 16, 17, 18, 19,
		20, 21, 22, 23, 24, 25, 26,
		//                             27, 28, 29,
		// 30, 31, 32, 33, 34, 35, 36
	};

	node_id = random_rand() % (sizeof(node_ids)/sizeof(node_ids[0]));
	node_id = node_ids[node_id];

	if (random_rand() % (sizeof(node_ids)/sizeof(node_ids[0])) > 10) 
		continue;

    /* Get random destination address */
	deployment_get_addr_by_id(node_id, &dest);
#else
    dest.u8[0] = 0xFF & ((random_rand() % 10) + 1);
#endif

    /* Send packet to destination */
    packetbuf_clear();
    memcpy(packetbuf_dataptr(), &msg, sizeof(msg));
    packetbuf_set_datalen(sizeof(msg));
    printf("App: Send seqn %d to %02x:%02x\n",
      msg.seqn, dest.u8[0], dest.u8[1]);
    rp_send(&conn, &dest);
    msg.seqn++;
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
static void
recv_cb(const linkaddr_t *originator, uint8_t hops)
{
  test_msg_t msg;
  if (packetbuf_datalen() != sizeof(msg)) {
    printf("App: wrong length: %d\n", packetbuf_datalen());
    return;
  }
  memcpy(&msg, packetbuf_dataptr(), sizeof(msg));
  printf("App: Recv from %02x:%02x seqn %d hops %d\n",
    originator->u8[0], originator->u8[1], msg.seqn, hops);
}
/*---------------------------------------------------------------------------*/
