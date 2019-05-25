#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "msg_info.h" 		// informations about message structure

#include "lib/list.h"
#include "lib/memb.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>


#define MAX_RETRANSMISSIONS 4
#define NUM_HISTORY_ENTRIES 4

/*---------------------------------------------------------------------------*/
// Start all processes
PROCESS(rime_root, "rime root");
PROCESS(alive,"alive process");
PROCESS(is_alive,"check alive process");
AUTOSTART_PROCESSES(&rime_root,&alive,&is_alive);
/*---------------------------------------------------------------------------*/
// Definition of variables used by all processes
//variables to manage the addresses
static rimeaddr_t *local_addr,*def_addr;
static char deep = 0;

//flags variables
static SET = TRUE;

//routing table
static char *map_addr,local;

//structure use to keep track of child, it is construct has a double circular linked-list
typedef struct child_node child_node;
struct child_node{
	char child_addr; 	// address of the child
	child_node *next; 	// next child addresse
	child_node *bef;  	// previous child addresse
	int send;		// flag to know if a keep alive was send to him	
	int resp;	  	// flag to know if a keep alive msg was respond by him
};

//variable to track the circular linked list
static child_node *curs_child; // pointer to a child in the list
static int nb = 0; // number of child in the list

void send_info(char* msg,rimeaddr_t *addr,int length);
void read_msg(char *type,char *msg);
void del_addr(char *rem_addr);
/*---------------------------------------------------------------------------*/
// add a child in the list
void add_child(char *addr)
{
	printf("adding the child %d\n",*addr);
	//allocation and initialisation of the new structure
	child_node *new_node = malloc(sizeof(child_node));
	new_node->child_addr = *addr;				// setting the address
	new_node->resp = 1;					// setting the response to 1 to prevent a first remove of the child list
	//if the list is empty creat a new one;
	if(nb == 0)
	{
		new_node->next = new_node;
		new_node->bef = new_node;
		curs_child = new_node;
	}
	else{
		new_node->bef = curs_child;
		new_node->next = curs_child->next;
		curs_child->next->bef = new_node;
		curs_child->next = new_node; 
		
	}
	nb ++;
}
/*---------------------------------------------------------------------------*/
//remove the given child_node from the list and also update the routing table
void remove_child(child_node *node)
{
	printf("removing the child %d\n",node->child_addr);
	del_addr(&(node->child_addr));
	// if the list has more than one element
	if(nb != 1){
		node->bef->next = node->next;
		node->next->bef = node->bef;
	}
	free(node);
	node = NULL;
	nb = nb-1;
}
/*---------------------------------------------------------------------------*/
//check if all the child are alive, otherwise remove those which didn't responde to the msg 
void check_child()
{
	printf("checking the childs\n");
	int i = 0;					
	child_node *cursor = curs_child;		
	while(i<nb){	
		//remove the child if no response from him was detected
		printf("testing child %d with send %d and resp %d\n",cursor->child_addr,cursor->send,cursor->resp);
		if(cursor->resp == 0 && cursor->send == 1)
		{
			//moving next
			child_node *temp = cursor->next;
			remove_child(cursor);
			cursor = temp;
		}
		//reseting the flag
		cursor->resp = 0;
		cursor->send = 0;
		i++;
	}
}
/*--------------------------------------------------------------------------*/
//return the next adrress of the child in the list 
char get_next_child()
{
	if(nb != 0){
		curs_child->send = 1;
		printf("next child %d\n",curs_child->next->child_addr);
		curs_child = curs_child->next;
		return curs_child->bef->child_addr;
	}
	//return the default address if there is no child
	else
	{
		return 0;
	}
}
/*--------------------------------------------------------------------------*/
//set the resp flag to the child which have the address addr
void set_flag(char *addr){
	int i = 0;
	printf("set the flag of child %d\n",*addr);
	child_node *cursor = curs_child;
	while(i<nb){
		if(*addr == cursor->child_addr){
			cursor->resp = 1;
			return;
		}
		cursor = cursor->next;
	}
	return;
}
/*---------------------------------------------------------------------------*/
//init the routing table with the local addr
void init_map(char *local_addr){
	map_addr = malloc(256*sizeof(char));
	int i;
	for(i = 0 ; i < 256 ; i++)
	{
		*(map_addr+i) = 0;
	}
	local = *local_addr;
	*(map_addr+*local_addr) = *local_addr; //making the routing table looping on it 
}
/*---------------------------------------------------------------------------*/
//delet the existing routing table
void del_map(){
	free(map_addr);
	map_addr = NULL; 
}
/*---------------------------------------------------------------------------*/
//set a new entry in the routing table ( construct as a union-find map -> the entry at index i is equal to the index in the map of his parent)
char get_new_addr(char *parent_addr)
{
  char i;
	//check if the parent is the root of the subtree
	if(*parent_addr == 0){
		parent_addr = &local;
	}
	// address 0 and 1 are reserve for default and root addresses
	for(i = 2 ; i < 256 ; i++)
	{
		//we must find an address not use in the network (u8[0] == 0)
		if(*(map_addr+i) == 0)
		{
			memcpy((map_addr+i),parent_addr,sizeof(char));
			printf("Put at index %d parent %d\n",i,*(map_addr+i));
			return i;
		}
	}
	return 0;
}
/*---------------------------------------------------------------------------*/
//return true if parent have the child in his subtree or if the child have no parent
int find(char *child_addr,char *parent_addr)
{
	//if the child equal to the parent return TRUE or if there is no parent
	if(*child_addr == *parent_addr || *child_addr == 0){
		return 1;
	}
	//return FALSE if we reach the end of the subtree
	if(*child_addr == local){
		return 0;
	}
        // recursive call on the direct parent of the child
	return find(map_addr+(*child_addr),parent_addr);
}
/*---------------------------------------------------------------------------*/
//delet an entry in the table and all his childs
void del_addr(char *rem_addr)
{
	printf("deleting the address %d\n",*rem_addr);
	int i;
	for(i = 2 ; i < 256 ; i++)
	{
		//remove child which have the rem_addr as parent or have no parent
		if(find((map_addr+i),rem_addr)){
			printf("removing address %d\n",i);
			*(map_addr+i) = 0;
		}
	}
}
/*--------------------------------------------------------------------------*/
//add an address to the routing table
void add_addr(char *add_addr,char *parent_addr)
{
	*(map_addr+*add_addr) = *parent_addr; 
}
/*---------------------------------------------------------------------------*/
//recived reliable unicast message handler
static void
recv_runicast(struct runicast_conn *c, const rimeaddr_t *from, uint8_t seqno)
{
	//get the message
	char *msg = (char *)packetbuf_dataptr();
	printf("runicast message received from %d.%d, seqno %d, msg %0.1x\n",from->u8[0], from->u8[1], seqno,*msg);

	//case for the different received code define in msg_info.h
	char temp_addr;
	switch(*msg){
		//ask for an address to enter in the network
		case CODE_ADDR_REQ:
			// ask a new address which is not use in the routing table and update it with the new parent
			temp_addr = get_new_addr(from->u8);
			// send the new address to the asking node
			char *send;
    			send = malloc(2);
    			send[0] = CODE_ADDR_INFO;
    			send[1] = temp_addr;
			send_info(send,from,CODE_LENGTH+ADDR_LENGTH);
			// check if the node is a child and add it to the childs if it is the case
			if(from->u8[0] == 0){
				add_child(&send[1]);
			}
			free(send);
			break;
		//check if a child respond to the alive message
		case CODE_ALIVE_CHILD:
			set_flag(from->u8);
			break;
		//message to inform that a node left the network
		case CODE_DEL_NODE:
			del_addr(msg+1);
			break;
		//message containing temperature datas
		case CODE_MSG_TEMP:
			read_msg("temp",msg);
			break;
		//message containing humidity datas
		case CODE_MSG_HUM:
			read_msg("hum",msg);
			break;
		//unknow msg
		default:
			break;
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
	printf("runicast message timed out when sending to %d.%d, retransmissions %d\n",to->u8[0], to->u8[1], retransmissions);
}
static const struct runicast_callbacks runicast_callbacks = {recv_runicast,
							     sent_runicast,
							     timedout_runicast};
static struct runicast_conn runicast;
/*---------------------------------------------------------------------------*/
//recived broadcast message handler
static void
broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
	//getting the message
	char *msg = (char *)packetbuf_dataptr();
	printf("broadcast message received from %d.%d: '%0.1x'\n",from->u8[0], from->u8[1],*msg);

	//request for an address of an non connected child
	if(*msg == CODE_ADDR_REQ && from->u8[0] == 0){
	    printf("request addr, send addr of size %d\n",sizeof(rimeaddr_t));
	    char *send;
	    send = malloc(2);
	    send[0] = CODE_ADDR_DEEP;
	    //sending the deep that the child will have if it take this node has parent
	    send[1] = deep+1;
	    send_info(send,from,CODE_LENGTH+DEEP_LENGTH);
	    free(send);
	  }
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
//general methode to send information message
void send_info(char *msg,rimeaddr_t *send, int length)
{
	printf("send unicast messsage to %d.%d\n",send->u8[0],send->u8[1]);
	packetbuf_copyfrom(msg,length);
	runicast_send(&runicast, send, MAX_RETRANSMISSIONS);
}
/*--------------------------------------------------------------------------*/
//general methode to read data message
void read_msg(char *type,char *msg)
{	
	int i = 1;
	// read message until the end of the message is reach
	while(*(msg+i) != 0){
		//print data on the standard output to be taken by the gate away
		if(strcmp(type,"hum")){
			printf("/hum/node_%d %d\n",*(msg+i),*(msg+i+1));
			i = i+2;
		}else{
			printf("/temp/node_%d %d\n",*(msg+i),*(msg+i+1));
			i = i+2;
		}
	}
}
/*---------------------------------------------------------------------------*/
//main process of the node, it initialize the variable and the communication channel
PROCESS_THREAD(rime_root, ev, data)
{
	static struct etimer et;

	//set the variable once to avoid run out of memory
	if(SET){
		//default address use by new node joining the network
		def_addr = malloc(sizeof(rimeaddr_t));
		def_addr->u8[0] = 0;
		def_addr->u8[1] = 0;
		//local_addr of the node is by default the first address
		local_addr = malloc(sizeof(rimeaddr_t));
		local_addr->u8[0] = 1;
		local_addr->u8[1] = 0;
		//initialisation of the map
		init_map(local_addr->u8);
        	SET = FALSE;
  	}
	
	//setting the node address
	rimeaddr_set_node_addr(local_addr);

	//configuring the process
	PROCESS_EXITHANDLER(runicast_close(&runicast);)
 	PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
  	PROCESS_BEGIN();
	
	//opening the communication
  	runicast_open(&runicast, 144, &runicast_callbacks);
  	broadcast_open(&broadcast, 129, &broadcast_call);

	//infinite loop
 	while(1) {
   		/* Delay 5-10 seconds */
    		etimer_set(&et, CLOCK_SECOND * 5 + random_rand() % (CLOCK_SECOND * 10));
    		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  	}
  	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
//checkin if the childs are still alive
PROCESS_THREAD(alive, ev, data)
{
  	static struct etimer et;
   
  	PROCESS_BEGIN();
  
  	while(1) {
   		/* Delay 30-40 seconds */
    		etimer_set(&et, CLOCK_SECOND * 30 + random_rand() % (CLOCK_SECOND * 40));
    		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
		//getting the next child addr to test if it is still alive 
		char *msg = malloc(1);
		rimeaddr_t *child_addr = malloc(sizeof(rimeaddr_t));
		child_addr->u8[0] = get_next_child();
		child_addr->u8[1] = 0;
		*msg = CODE_ALIVE_PARENT;
		printf("send message to child %d.%d\n",child_addr->u8[0],child_addr->u8[1]);
		//send only if the address exist in the network
		if(child_addr->u8[0] != 0){
			send_info(msg,child_addr,CODE_LENGTH);
		}
		free(msg);
		free(child_addr);
	}
    
  	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
//remove not alive child
PROCESS_THREAD(is_alive, ev, data)
{
  	static struct etimer et;
   
  	PROCESS_BEGIN();
  
  	while(1) {
   		/* Delay 360 seconds */
    		etimer_set(&et, CLOCK_SECOND * 180);
    		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
		check_child();
	}
    
  	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
