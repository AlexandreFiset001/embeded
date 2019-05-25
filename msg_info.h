#ifndef __MSG_INFO__
#define __MSG_INFO__

#define TRUE 1
#define FALSE 0

#define CODE_MSG_HUM 0x1
#define CODE_MSG_TEMP 0x2
#define CODE_MSG_HUM_PER 0x3
#define CODE_MSG_TEMP_PER 0x4
#define CODE_MSG_HUM_CH 0x5
#define CODE_MSG_TEMP_CH 0x6
#define CODE_MSG_TEMP_STOP 0x7
#define CODE_MSG_HUM_STOP 0x8
#define CODE_ADDR_INFO 0x9
#define CODE_ADDR_REQ 0xA
#define CODE_ADDR_DEEP 0xB
#define CODE_ALIVE_PARENT 0xC
#define CODE_ALIVE_CHILD 0xD
#define CODE_DEL_NODE 0xE


#define CODE_LENGTH 1
#define DEEP_LENGTH 1
#define ADDR_LENGTH 1
#define DATA_MSG_LENGTH ADDR_LENGTH+1

#endif


