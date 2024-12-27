#ifndef __NBR_TABLE_H__
#define __NBR_TABLE_H__
/*---------------------------------------------------------------------------*/
/* Default static size of neighbour list is 36, can be overridden with 
 * a new define at compile time
 */
#ifndef OVERRIDE_MAX_NODES
#define MAX_NODES 36
#else
#define MAX_NODES OVERRIDE_MAX_NODES
#endif
/*---------------------------------------------------------------------------*/
/* I have trouble finding the hbr-table doc so we try a struct of 
 * arrays (at least is cache friendly and it shouldn't be too big?).
 * This approach is also very similar to what Contiki's MEMB does.
 */
struct nbr_table {
  linkaddr_t dest[MAX_NODES];
  linkaddr_t next[MAX_NODES];
  clock_time_t expires[MAX_NODES];
  uint8_t sz;
};
typedef struct nbr_table nbr_table;

#endif