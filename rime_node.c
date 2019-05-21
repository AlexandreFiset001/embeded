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
static rimeaddr_t *local_addr,*def_addr,*parrent_addr,*req_addr;
static char deep;
static SET = TRUE;
static INFO_PENDING = TRUE;
void send_info(char* msg,rimeaddr_t *addr,int code);
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
  printf("runicast message received from %d.%d, seqno %d, msg %0.3x, pending %d\n",
	 from->u8[0], from->u8[1], seqno, msg[0],INFO_PENDING);

  if(msg[0] == CODE_ADDR_INFO && INFO_PENDING){
	printf("in %d.%d\n",local_addr->u8[0], local_addr->u8[1]);
	if(rimeaddr_cmp(local_addr,def_addr)){
		printf("get 1 %d\n",msg[1]);
		printf("get 2 %d\n",msg[2]);
		local_addr->u8[0] = *(msg+1);
		local_addr->u8[1] = *(msg+2);
		printf("in %d.%d\n",local_addr->u8[0], local_addr->u8[1]);
		rimeaddr_set_node_addr(local_addr);
		memcpy(parrent_addr,from,sizeof(rimeaddr_t));
		printf("local addr after set %d.%d\n",local_addr->u8[0],local_addr->u8[1]);
	}
	else if(!rimeaddr_cmp(local_addr,def_addr)){
		printf("forward %d.%d\n",req_addr->u8[0],req_addr->u8[1]);
		send_info(msg,req_addr,CODE_ADDR_INFO);
		memcpy(req_addr,def_addr,sizeof(rimeaddr_t));
	}
	INFO_PENDING = FALSE;
  }
  else if(msg[0] == CODE_ADDR_REQ  && !INFO_PENDING )
  {
	printf("forward %d.%d  with parent %d.%d\n",parrent_addr->u8[0],parrent_addr->u8[1],msg[1],msg[2]);
	memcpy(req_addr,from,sizeof(rimeaddr_t));
	send_info(msg,parrent_addr,CODE_ADDR_REQ);
	INFO_PENDING = TRUE;
  }
  else if(msg[0] == CODE_ADDR_DEEP){
	if(deep == 0 || deep>msg[1]){
		memcpy(parrent_addr,from,sizeof(rimeaddr_t));
		printf(" parrent %d.%d\n",parrent_addr->u8[0],parrent_addr->u8[1]);
		memcpy(&deep,&msg[1],sizeof(char));
	} 
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
  printf("broadcast message received from %d.%d: '%0.1x'\n",
         from->u8[0], from->u8[1],*msg);
  if(*msg == CODE_ADDR_REQ && !rimeaddr_cmp(def_addr,local_addr) && rimeaddr_cmp(def_addr,from)){
    printf("request addr, send addr of size %d\n",sizeof(rimeaddr_t));
    char *send;
    send = malloc(2);
    send[0] = CODE_ADDR_DEEP;
    send[1] = deep+1;
    packetbuf_copyfrom(send,CODE_LENGTH+DEEP_LENGTH);
    runicast_send(&runicast, from, MAX_RETRANSMISSIONS);
    printf("send");
    free(send);
  }
  
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
void send_info(char *msg,rimeaddr_t *addr,int code)
{
	char *send;
    	send = malloc(3);
    	send[0] = code;
	send[1] = *(msg+1);
	send[2] = *(msg+2);
	packetbuf_copyfrom(send,CODE_LENGTH+ADDR_LENGTH);
	runicast_send(&runicast, addr, MAX_RETRANSMISSIONS);
	free(send);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rime_node, ev, data)
{
  static struct etimer et;
  if(SET){
          deep = 0;
	  def_addr = malloc(sizeof(rimeaddr_t));
	  def_addr->u8[0] = 0;
	  def_addr->u8[1] = 0;
          req_addr = malloc(sizeof(rimeaddr_t));
	  req_addr->u8[0] = 0;
	  req_addr->u8[1] = 0;
  	  local_addr = malloc(sizeof(rimeaddr_t));
	  local_addr->u8[0] = 0;
	  local_addr->u8[1] = 0;
	  parrent_addr = malloc(sizeof(rimeaddr_t));
  	  SET = FALSE;
  }
  rimeaddr_set_node_addr(local_addr);
  PROCESS_EXITHANDLER(runicast_close(&runicast);)
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
    
  PROCESS_BEGIN();
  

  runicast_open(&runicast, 146, &runicast_callbacks);
  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {
   /* Delay 5-10 seconds */
    etimer_set(&et, CLOCK_SECOND * 5 + random_rand() % (CLOCK_SECOND * 10));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if(rimeaddr_cmp(def_addr,local_addr)){
      printf("deep %d,pending %d\n",deep,INFO_PENDING);
      if(deep == 0){
	      char *msg;
	      *msg = CODE_ADDR_REQ;
	      packetbuf_copyfrom(msg, 1);
	      broadcast_send(&broadcast);
      }
      else{
	      char *msg = malloc(3);
	      *msg = CODE_ADDR_REQ;
              msg[1] = parrent_addr->u8[0];
	      msg[2] = parrent_addr->u8[1];
	      packetbuf_copyfrom(msg, 3);
	      runicast_send(&runicast, parrent_addr, MAX_RETRANSMISSIONS);
	      free(msg);
      }
    }
    else{
      printf("obtained addr %d.%d\n",local_addr->u8[0],local_addr->u8[1]);
      printf("parrent addr %d.%d\n",parrent_addr->u8[0],parrent_addr->u8[1]);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
