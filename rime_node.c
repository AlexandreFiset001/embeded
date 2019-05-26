#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "msg_info.h" 		// informations about message structure

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_RETRANSMISSIONS 4
#define NUM_HISTORY_ENTRIES 4

/*---------------------------------------------------------------------------*/
// Start all processes
PROCESS(rime_node, "rime node");
PROCESS(get_alive, "check parent alive");
PROCESS(alive,"alive process");
PROCESS(is_alive,"check alive childs");
PROCESS(temp,"temp producer");
PROCESS(hum,"hum producer");
AUTOSTART_PROCESSES(&rime_node,&get_alive,&alive,&temp,&hum,&is_alive);
/*---------------------------------------------------------------------------*/
// Definition of variables used by all processes
//variables to manage the addresses
static rimeaddr_t *local_addr,*def_addr,*parent_addr,*req_addr,*parent_req_addr;
static char deep;

//variables for to manage the data
static char *tab_temp,*tab_hum;						// circular buffer
static int i_temp_prod,i_temp_cons,i_hum_prod,i_hum_cons;		// index for producer and consumer

//flags variables
static SET = TRUE;
static INFO_PENDING = TRUE;						// flag to test if an address is already requested
static ALIVE_PARENT = FALSE;						// flag to see if the parent is still alive
static RESET_CONN = FALSE;						// flag to reset the node connection 
static SEND_TEMP = FALSE;						// flag to allow sending temperature data
static SEND_HUM = FALSE;						// flag to allow sending humidity data
static SEND_TEMP_CH = FALSE;						// flag to send temperature data only when there is a change
static SEND_HUM_CH = FALSE;						// flag to send humidity data only when there is a change

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
//find the child who have the target address in his subtree
char get_path(char *target)
{
	printf(" target value %d at index %d\n",*(map_addr+*target),*target);
	if(*(map_addr+*target) == local){
		return *target;
	}
	else{
		return get_path((map_addr+*target));
	}
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
		*(map_addr+*rem_addr) = 0;
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
  	printf("runicast message received from %d.%d, seqno %d, msg %0.3x, pending %d\n",
	 from->u8[0], from->u8[1], seqno, msg[0],INFO_PENDING);
	
	//case for the different received code define in msg_info.h
	rimeaddr_t *temp;
	temp = malloc(sizeof(rimeaddr_t));
	switch(*msg){
		// message to forward or get the response to an address requested
		case CODE_ADDR_INFO:
			//check if an address request is pending
			if(INFO_PENDING){
				//the address is for this node if his address is still the default address
				if(rimeaddr_cmp(local_addr,def_addr)){
					//setting the new local address
					local_addr->u8[0] = *(msg+1);
					local_addr->u8[1] = 0;
					//reseting the connection with the new address
					RESET_CONN = TRUE;
					//remembering the parent
					memcpy(parent_addr,from,sizeof(rimeaddr_t));
				}
				//the address is for a node in the subtree
				else{
					//forward the message to the one who request it
					send_info(msg,req_addr,CODE_LENGTH+ADDR_LENGTH);
					//adding the address to the routing table
					//if the one who request the address is not in the network ( have a default node address ), add it to the child and the routing table
					if(req_addr->u8[0] == 0){
						add_child(&msg[1]);
						add_addr(&msg[1],local_addr->u8);
					}
					//otherwise just add it to the routing table
					else{
						add_addr(&msg[1],parent_req_addr->u8);
					}
				}
				//set the flag to allow new address to be manage
				INFO_PENDING = FALSE;
			}
			break;
		// message to request a new address to enter in the network
		case CODE_ADDR_REQ:
			//check if no address is already required
			if(!INFO_PENDING){
				//forward the message to the parent to be handled by the root
				send_info(msg,parent_addr,CODE_LENGTH+ADDR_LENGTH);
				//keep track of the parent of the new child to update the routing table
				parent_req_addr->u8[0] = msg[1];
				parent_req_addr->u8[1] = 0;
				//set the flag to signal that an address is already required
				INFO_PENDING = TRUE;
			}
			break;
		// message that informe of the deep send by a parent
		case CODE_ADDR_DEEP:
			//check if it is the first deep recieve or if the deep recieve is lower than the recieved ones
			if(deep == 0 || deep>msg[1]){
				//set the parent
				memcpy(parent_addr,from,sizeof(rimeaddr_t));
				//set the deep
				memcpy(&deep,&msg[1],sizeof(char));
			}
			break;
		// message that informe that the parent is alive and want to know if the child is alive
		case CODE_ALIVE_PARENT:
			ALIVE_PARENT = TRUE;
			//send the message to informe that the child is alive
			*msg = CODE_ALIVE_CHILD;
			send_info(msg,from,CODE_LENGTH);
			break;
		// message that informe that the child is alive 
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
		//command message
		case CODE_MSG_HUM_PER:
			//message for this node
			if(*(msg+1) == local_addr->u8[0]){
				SEND_HUM = TRUE;
			}
			//otherwise forward it to the concerned node
			else{
				//find the path to the concerned node
				temp = get_path(&msg[1]);
				temp->u8[0] = temp;
				temp->u8[1] = 0;
				send_info(msg,temp,CODE_LENGTH+ADDR_LENGTH);
			}
		case CODE_MSG_TEMP_PER:
			//message for this node
			if(*(msg+1) == local_addr->u8[0]){
				SEND_TEMP = TRUE;
			}
			//otherwise forward it to the concerned node
			else{
				//find the path to the concerned node
				temp = get_path(&msg[1]);
				temp->u8[0] = temp;
				temp->u8[1] = 0;
				send_info(msg,temp,CODE_LENGTH+ADDR_LENGTH);
			}
		case CODE_MSG_HUM_CH:
			//message for this node
			if(*(msg+1) == local_addr->u8[0]){
				SEND_HUM = TRUE;
				SEND_HUM_CH = TRUE;
			}
			//otherwise forward it to the concerned node
			else{
				//find the path to the concerned node
				temp = get_path(&msg[1]);
				temp->u8[0] = temp;
				temp->u8[1] = 0;
				send_info(msg,temp,CODE_LENGTH+ADDR_LENGTH);
			}
		case CODE_MSG_TEMP_CH:
			//message for this node
			if(*(msg+1) == local_addr->u8[0]){
				SEND_TEMP = TRUE;
				SEND_TEMP_CH = TRUE;
			}
			//otherwise forward it to the concerned node
			else{
				//find the path to the concerned node
				temp = get_path(&msg[1]);
				temp->u8[0] = temp;
				temp->u8[1] = 0;
				send_info(msg,temp,CODE_LENGTH+ADDR_LENGTH);
			}
		case CODE_MSG_HUM_STOP:
			//message for this node
			if(*(msg+1) == local_addr->u8[0]){
				SEND_HUM = FALSE;
				SEND_HUM_CH = FALSE;
			}
			//otherwise forward it to the concerned node
			else{
				//find the path to the concerned node
				temp = get_path(&msg[1]);
				temp->u8[0] = temp;
				temp->u8[1] = 0;
				send_info(msg,temp,CODE_LENGTH+ADDR_LENGTH);
			}
		case CODE_MSG_TEMP_STOP:
			//message for this node
			if(*(msg+1) == local_addr->u8[0]){
				SEND_TEMP = TRUE;
				SEND_TEMP_CH = TRUE;
			}
			//otherwise forward it to the concerned node
			else{
				//find the path to the concerned node
				temp = get_path(&msg[1]);
				temp->u8[0] = temp;
				temp->u8[1] = 0;
				send_info(msg,temp,CODE_LENGTH+ADDR_LENGTH);
			}
		//unknow msg
		default:
			break;
	}
	free(temp);
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
//recived broadcast message handler
static void
broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
	// get the message
  	char *msg = (char *)packetbuf_dataptr();
  	printf("broadcast message received from %d.%d: '%0.1x'\n",from->u8[0], from->u8[1],*msg);
	
	//request for an address of an not connected child when the node his not managing an other address
  	if(*msg == CODE_ADDR_REQ && !INFO_PENDING && from->u8[0] == 0){
    		printf("request addr, send addr of size %d\n",sizeof(rimeaddr_t));
    		char *send;
    		send = malloc(2);
    		send[0] = CODE_ADDR_DEEP;
    		send[1] = deep+1;
    		send_info(send,from,CODE_LENGTH+DEEP_LENGTH);
	    	free(send);
	}	  
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
//general methode to send information message
void send_info(char *msg,rimeaddr_t *addr,int length)
{
	packetbuf_copyfrom(msg,length);
	runicast_send(&runicast, addr, MAX_RETRANSMISSIONS);
}
/*--------------------------------------------------------------------------*/
//general methode to send data message
void send(char *type)
{
	//send a message of 9 data at maximum ( addr of the node which produce the data and the data him self)
	char *msg = malloc(20*sizeof(char));
	int i = 1;
	if(!strcmp(type,"hum")){
		*msg = CODE_MSG_HUM;
		do{
			*(msg+i) = *(tab_hum+i_hum_cons);
			*(msg+i+1) = *(tab_hum+i_hum_cons+1);
			i = i + 2;
			i_hum_cons = (i_hum_cons+2)%100;
		}while(i_hum_cons != i_hum_prod && i<19);
	}else{
		*msg = CODE_MSG_TEMP;
		do{
			*(msg+i) = *(tab_temp+i_temp_cons);
			*(msg+i+1) = *(tab_temp+i_temp_cons+1);
			i = i+2;
			i_temp_cons = (i_temp_cons+2)%100;
		}while(i_hum_cons != i_hum_prod && i<19);
	}
	// end of data marker
	*(msg+i) = 0;
	printf("send code %d \n",*msg);
	printf("send addr %d \n",*(msg+1));
	printf("send data %d \n",*(msg+2));
	packetbuf_copyfrom(msg,i*(DATA_MSG_LENGTH));
	runicast_send(&runicast, parent_addr, MAX_RETRANSMISSIONS);
}
/*--------------------------------------------------------------------------*/
//general methode to read data message
void read_msg(char *type,char *msg)
{	
	int i = 1;
	// read message until the end of the message is reach
	while(*(msg+i) != 0){
		//put data in the buffer
		if(!strcmp(type,"hum")){
			*(tab_hum+i_hum_cons) = *(msg+i);
			*(tab_hum+i_hum_cons+1) = *(msg+i+1);
			i = i + 2;
			i_hum_cons = (i_hum_cons+2)%100;
		}else{
			*(tab_temp+i_temp_cons) = *(msg+i);
			*(tab_temp+i_temp_cons+1) = *(msg+i+1);
			i = i+2;
			i_temp_cons = (i_temp_cons+2)%100;
		}
	}
}
/*---------------------------------------------------------------------------*/
//main process of the node, it initialize the variable and the communication channel
PROCESS_THREAD(rime_node, ev, data)
{
  	static struct etimer et;
	
	//set the variable once to avoid run out of memory
  	if(SET){
		//when the node is not connected the default deep is 0
          	deep = 0;
		//default address use by new node joining the network
	  	def_addr = malloc(sizeof(rimeaddr_t));
	 	def_addr->u8[0] = 0;
	  	def_addr->u8[1] = 0;
		//allocation of memory to retain the node who ask the information who is pending
          	req_addr = malloc(sizeof(rimeaddr_t));
	  	req_addr->u8[0] = 0;
	  	req_addr->u8[1] = 0;
		//allocation of memory to retain the parent of the new child
          	req_addr = malloc(sizeof(rimeaddr_t));
	  	req_addr->u8[0] = 0;
	  	req_addr->u8[1] = 0;
		//local_addr of the node is by default the null address for a network node
  	  	local_addr = malloc(sizeof(rimeaddr_t));
	  	local_addr->u8[0] = 0;
	  	local_addr->u8[1] = 0;
		//allocation of memeory for the address of the parent
	  	parent_addr = malloc(sizeof(rimeaddr_t));
		parent_addr->u8[0] = 0;
	  	parent_addr->u8[1] = 0;
		//initialisation of the variable use for the data management
	  	tab_temp = malloc(sizeof(char)*100);
          	tab_hum = malloc(sizeof(char)*100);
	  	i_temp_prod = 0;
          	i_temp_cons = 0;
	  	i_hum_prod = 0;
	  	i_hum_cons = 0;
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
		//if the node need to reset the connection with an other address
    		if(RESET_CONN){
        		runicast_close(&runicast);
        		broadcast_close(&broadcast);
			rimeaddr_set_node_addr(local_addr);
			del_map();
			init_map(local_addr->u8);
        		runicast_open(&runicast, 144, &runicast_callbacks);
   			broadcast_open(&broadcast, 129, &broadcast_call);
			RESET_CONN = FALSE;
    		}
		//if the node is not in the network
    		if(rimeaddr_cmp(def_addr,local_addr)){
      		printf("deep %d,pending %d\n",deep,INFO_PENDING);
			//when no parent was detected
      			if(deep == 0){
		      		char *msg;
		      		*msg = CODE_ADDR_REQ;
	      			packetbuf_copyfrom(msg, CODE_LENGTH);
    				broadcast_send(&broadcast);
      			}
			//if a parent is detected, ask for an address to him
      			else{
	      			char *msg = malloc(2);
	      			*msg = CODE_ADDR_REQ;
              			msg[1] = parent_addr->u8[0];
	      			send_info(msg,parent_addr,CODE_LENGTH+ADDR_LENGTH);
	      			free(msg);
      			}
    		}
		//if the node is in the network
   		else{
			//send data if the circular buffer is not empty
			printf("cons %d prod %d\n",i_temp_cons,i_temp_prod);
      			if(i_temp_cons != i_temp_prod){
				printf("send temp\n");
				send("temp");
	      		}
	      		else if(i_hum_cons != i_hum_prod){
				send("hum");
      			}
      			printf("obtained addr %d.%d\n",local_addr->u8[0],local_addr->u8[1]);
      			printf("parent addr %d.%d\n",parent_addr->u8[0],parent_addr->u8[1]);
    		}
  	}

  	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
//check if the parent is alive
PROCESS_THREAD(get_alive, ev, data)
{
  	static struct etimer et;
  
  	PROCESS_BEGIN();


  	while(1) {
   		/* Delay 360 seconds */
    		etimer_set(&et, CLOCK_SECOND * 360);
    		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
		
		//if no message from the parent was recieved in the elapsed time, reset to an not connected node
    		if(!ALIVE_PARENT)
    		{
			//seting the variable to thiere default value
			deep = 0;
			INFO_PENDING = TRUE;
			ALIVE_PARENT = FALSE;
			SEND_TEMP = FALSE;						
			SEND_HUM = FALSE;						
			SEND_TEMP_CH = FALSE;						
			SEND_HUM_CH = FALSE;
			def_addr->u8[1] = random_rand() % 256;
			def_addr->u8[1] =  random_rand() % 256;
			memcpy(local_addr,def_addr,sizeof(rimeaddr_t));
			printf("reset addr\n");
			RESET_CONN = TRUE;
    		}
		// the parent is alive
    		else{
			ALIVE_PARENT = FALSE;
    		}
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
//generate the temperature data
PROCESS_THREAD(temp, ev, data)
{
  static char bef_temp = 0;
  static struct etimer et;
  
  PROCESS_BEGIN();


  while(1) {
   /* Delay 60 seconds */
    etimer_set(&et, CLOCK_SECOND * 60);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    char temp = random_rand()%256;
    if(SEND_TEMP)
    {
	//send data only if there is a change
	if(SEND_TEMP_CH){
		if(temp != bef_temp){
			bef_temp = temp;
			memcpy((tab_temp+i_temp_prod),local_addr->u8,1);
			memcpy((tab_temp+i_temp_prod+1),&temp,1);
			i_temp_prod = ( i_temp_prod + 2) % 100;
		}
	}
	else
	{
		memcpy((tab_temp+i_temp_prod),local_addr->u8,1);
		memcpy((tab_temp+i_temp_prod+1),&temp,1);
		i_temp_prod = ( i_temp_prod + 2) % 100;
	}
    }
  }

  PROCESS_END();
}
/*--------------------------------------------------------------------------*/
//generate the humidity data
PROCESS_THREAD(hum, ev, data)
{
  static struct etimer et;
  static char bef_hum = 0;
  
  PROCESS_BEGIN();


  while(1) {
   /* Delay 60 seconds */
    etimer_set(&et, CLOCK_SECOND * 60);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    char hum = random_rand()%256;
    if(SEND_HUM)
    {
	//send data only if there is a change
	if(SEND_HUM_CH){
		if(hum != bef_hum){
			bef_hum = hum;
			memcpy((tab_temp+i_hum_prod),local_addr->u8,1);
			memcpy((tab_hum+i_hum_prod+1),&hum,1);
			i_hum_prod = ( i_hum_prod + 2) % 100;
		}
	}
	else
	{
		memcpy((tab_temp+i_hum_prod),local_addr->u8,1);
		memcpy((tab_hum+i_hum_prod+1),&hum,1);
		i_hum_prod = ( i_hum_prod + 2) % 100;
	}
    }
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
		//avoid stucked node
		INFO_PENDING = FALSE;
	}
    
  	PROCESS_END();
}
/*---------------------------------------------------------------------------*/

