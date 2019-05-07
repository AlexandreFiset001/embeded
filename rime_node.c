#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "msg_info.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RETRANSMISSIONS 4
#define NUM_HISTORY_ENTRIES 4

/*---------------------------------------------------------------------------*/
PROCESS(rime_node, "rime node");
AUTOSTART_PROCESSES(&rime_node);
/*---------------------------------------------------------------------------*/
static rimeaddr_t *local_addr,*def_addr;
static char deep;
/*---------------------------------------------------------------------------*/
/* Sender history.
 * Detects duplicate callbacks at receiving nodes.
 * Duplicates appear when ack messages are lost. */
struct history_entry {
  struct history_entry *next;
  rimeaddr_t addr;
  uint8_t seq;
};
LIST(history_table);
MEMB(history_mem, struct history_entry, NUM_HISTORY_ENTRIES);
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
  printf("broadcast message received from %d.%d: '%s'\n",
         from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
static void
recv_runicast(struct runicast_conn *c, const rimeaddr_t *from, uint8_t seqno)
{
  /* OPTIONAL: Sender history */
  struct history_entry *e = NULL;
  for(e = list_head(history_table); e != NULL; e = e->next) {
    if(rimeaddr_cmp(&e->addr, from)) {
      break;
    }
  }
  if(e == NULL) {
    /* Create new history entry */
    e = memb_alloc(&history_mem);
    if(e == NULL) {
      e = list_chop(history_table); /* Remove oldest at full history */
    }
    rimeaddr_copy(&e->addr, from);
    e->seq = seqno;
    list_push(history_table, e);
  } else {
    /* Detect duplicate callback */
    if(e->seq == seqno) {
      printf("runicast message received from %d.%d, seqno %d (DUPLICATE)\n",
	     from->u8[0], from->u8[1], seqno);
      return;
    }
    /* Update existing history entry */
    e->seq = seqno;
  }
  char *msg = (char *)packetbuf_dataptr();
  printf("runicast message received from %d.%d, seqno %d, msg %s\n",
	 from->u8[0], from->u8[1], seqno, msg);
  printf("get 1 %0.1x\n",*msg);
  printf("get 2 %0.1x\n",*(msg+1));
  memcpy(local_addr->u8,msg,2);
  rimeaddr_set_node_addr(local_addr);
  printf("local addr after set %d.%d\n",local_addr->u8[0],local_addr->u8[1]);
  
}
static void
sent_runicast(struct runicast_conn *c, const rimeaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message sent to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);
}
static void
timedout_runicast(struct runicast_conn *c, const rimeaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message timed out when sending to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);
}
static const struct runicast_callbacks runicast_callbacks = {recv_runicast,
							     sent_runicast,
							     timedout_runicast};
static struct runicast_conn runicast;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rime_node, ev, data)
{

  static struct etimer et;
  def_addr->u8[0] = 0;
  def_addr->u8[1] = 0;
  local_addr->u8[0] = 0;
  local_addr->u8[1] = 0;
  PROCESS_EXITHANDLER(runicast_close(&runicast);)
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
    
  PROCESS_BEGIN();
  
  rimeaddr_set_node_addr(def_addr);
  runicast_open(&runicast, 146, &runicast_callbacks);
  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {
    
   /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    memcpy(local_addr,&rimeaddr_node_addr,sizeof(rimeaddr_t));
    printf("local addr %d.%d\n",local_addr->u8[0],local_addr->u8[1]);
    if(local_addr->u8[0] == 0 && local_addr->u8[1] == 0) {
      char *msg;
      *msg = CODE_ADDR_REQ;
      packetbuf_copyfrom(msg, 1); // TODO: need to be change in format
      broadcast_send(&broadcast);
    }
    else{
      printf("obtained addr %d.%d\n",local_addr->u8[0],local_addr->u8[1]);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
