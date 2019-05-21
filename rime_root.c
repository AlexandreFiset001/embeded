#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "msg_info.h"

#include "lib/list.h"
#include "lib/memb.h"

#include <stdio.h>


#define MAX_RETRANSMISSIONS 4
#define NUM_HISTORY_ENTRIES 4

/*---------------------------------------------------------------------------*/
PROCESS(rime_root, "rime root");
AUTOSTART_PROCESSES(&rime_root);
/*---------------------------------------------------------------------------*/
static rimeaddr_t *local_addr,*send_addr,*map_addr,*def_addr,*req_addr,*child_addr;
static char deep = 0;
static set = TRUE;
void send_info(char *msg,rimeaddr_t *req_addr);
/*---------------------------------------------------------------------------*/
int set_new_addr(rimeaddr_t *parrent)
{
  int i;
  for(i = 2 ; i < pow(2,(RIMEADDR_SIZE*4)-1) ; i++)
  {
	if((map_addr+i)->u8[0] == 0 && (map_addr+i)->u8[1] == 0)
	{
		if(rimeaddr_cmp(parrent,def_addr)){
			parrent = local_addr;
		}
		memcpy((map_addr+i),parrent,sizeof(rimeaddr_t));
		printf("map_addr i = %d and 0 = %d , 1 = %d\n",i,(map_addr+i)->u8[0],(map_addr+i)->u8[1]);
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
  char *msg = (char *)packetbuf_dataptr();

  printf("runicast message received from %d.%d, seqno %d, msg %0.1x.%0.1x\n",
	 from->u8[0], from->u8[1], seqno,msg[1],msg[2]);

  if(*msg == CODE_ADDR_REQ && (!(msg[1] == 1 && msg[2] ==0) || rimeaddr_cmp(from,def_addr))){
    printf("request addr, send addr of size %d\n",sizeof(rimeaddr_t));
    memcpy(req_addr,from,sizeof(rimeaddr_t));
    int i = set_new_addr(from);
    char *send;
    send = malloc(3);
    send[0] = CODE_ADDR_INFO;
    send[1] = i%16;
    send[2] = (i>>4)%16;
    printf("request addr, send addr %d.%d\n",send[1],send[2]);
    send_info(send,from);
    free(send);
  }
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
  printf("broadcast message received from %d.%d: '%0.1x'\n",from->u8[0], from->u8[1],*msg);
  //request for an address
  if(*msg == CODE_ADDR_REQ && rimeaddr_cmp(def_addr,from)){
    printf("request addr, send addr of size %d\n",sizeof(rimeaddr_t));
    char *send;
    send = malloc(2);
    send[0] = CODE_ADDR_DEEP;
    send[1] = deep+1;
    send_info(send,from);
    free(send);
  }
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
void send_info(char *msg,rimeaddr_t *req_addr)
{
	packetbuf_copyfrom(msg,CODE_LENGTH+ADDR_LENGTH);
	runicast_send(&runicast, req_addr, MAX_RETRANSMISSIONS);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rime_root, ev, data)
{

  int i;
  static struct etimer et;
  if(set){
	def_addr = malloc(sizeof(rimeaddr_t));
	def_addr->u8[0] = 0;
	def_addr->u8[1] = 0;
	local_addr = malloc(sizeof(rimeaddr_t));
	local_addr->u8[0] = 1;
	local_addr->u8[1] = 0;
	req_addr = malloc(sizeof(rimeaddr_t));
	req_addr->u8[0] = 0;
	req_addr->u8[1] = 0;
	map_addr = malloc(pow(2,(RIMEADDR_SIZE*4)-1)*sizeof(rimeaddr_t));
	map_addr[0].u8[0] = 0;
	map_addr[0].u8[1] = 0;
	memcpy(map_addr+1,local_addr,sizeof(rimeaddr_t));
	printf("before seting map_addr\n");
	for(i = 2 ; i < pow(2,(RIMEADDR_SIZE*4)-1) ; i++)
	{
	memcpy((map_addr+i),map_addr,sizeof(rimeaddr_t));
	}
	child_addr = malloc(20*sizeof(rimeaddr_t));
	for(i = 0 ; i < 20 ; i++)
	{
	memcpy((map_addr+i),map_addr,sizeof(rimeaddr_t));
	}
        set = FALSE;
  }
  rimeaddr_set_node_addr(local_addr);
  PROCESS_EXITHANDLER(runicast_close(&runicast);)
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
    
  PROCESS_BEGIN();
  
  runicast_open(&runicast, 146, &runicast_callbacks);
  broadcast_open(&broadcast, 129, &broadcast_call);
  broadcast_send(&broadcast);
  while(1) {
   /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
