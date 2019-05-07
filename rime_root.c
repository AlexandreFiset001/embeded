#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "msg_info.h"

#include "lib/list.h"
#include "lib/memb.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>
#include <math.h> 
#include <stdlib.h>
#include <string.h>


#define MAX_RETRANSMISSIONS 4
#define NUM_HISTORY_ENTRIES 4

/*---------------------------------------------------------------------------*/
PROCESS(rime_root, "rime root");
AUTOSTART_PROCESSES(&rime_root);
/*---------------------------------------------------------------------------*/
static rimeaddr_t *local_addr,*send_addr,*map_addr;
static char deep = 0;
static set = TRUE;
/*---------------------------------------------------------------------------*/
int set_new_addr(rimeaddr_t *parrent)
{
  int i;
  for(i = 2 ; i < pow(2,(RIMEADDR_SIZE*4)-1) ; i++)
  {
	if((map_addr+i)->u8[0] == 0 && (map_addr+i)->u8[1] == 0)
	{
		printf("map_addr i = %d and 0 = %d , 1 = %d\n",i,(map_addr+i)->u8[0],(map_addr+i)->u8[1]);
		memcpy((map_addr+i),parrent,sizeof(rimeaddr_t));
		return i;
	}
  }
  return 0;
}
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

  printf("runicast message received from %d.%d, seqno %d\n",
	 from->u8[0], from->u8[1], seqno);
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
static void
broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
  char *msg = (char *)packetbuf_dataptr();
  printf("broadcast message received from %d.%d: '%0.1x'\n",from->u8[0], from->u8[1],msg);
  //request for an address
  if(*msg == CODE_ADDR_REQ){
    printf("request addr, send addr of size %d\n",sizeof(rimeaddr_t));
    int i = set_new_addr(local_addr);
    send_addr->u8[0] = i%16;
    send_addr->u8[1] = (i>>4)%16;
    packetbuf_copyfrom(send_addr, sizeof(rimeaddr_t));
    runicast_send(&runicast, from, MAX_RETRANSMISSIONS);
  }
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rime_root, ev, data)
{

  static struct etimer et;
  int i;
  if(set){
	local_addr = malloc(sizeof(rimeaddr_t));
	local_addr->u8[0] = 1;
	local_addr->u8[1] = 0;
	map_addr = malloc(pow(2,(RIMEADDR_SIZE*4)-1)*sizeof(rimeaddr_t));
	map_addr[0].u8[0] = 0;
	map_addr[0].u8[1] = 0;
	memcpy(map_addr+1,local_addr,sizeof(rimeaddr_t));
	printf("before seting map_addr\n");
	for(i = 2 ; i < pow(2,(RIMEADDR_SIZE*4)-1) ; i++)
	{
	memcpy((map_addr+i),map_addr,sizeof(rimeaddr_t));
	}
        set = FALSE;
  }
  PROCESS_EXITHANDLER(runicast_close(&runicast);)
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
    
  PROCESS_BEGIN();
  
  rimeaddr_set_node_addr(local_addr);
  runicast_open(&runicast, 146, &runicast_callbacks);
  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {
    
   /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
