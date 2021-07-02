//Benjamin Lipnik, 1. 7. 2021
//Hekaton izziv BLE2CAN, Domel.d.o.o

#ifndef BCO_H
#define BCO_H

#include <stdint.h>


/*
	Zakomentiraj SHOW_WARNINGS, ce ne zelis, da se izpisejo opozorila 	
	kot so opozorila o napacnih ukazih iz ble modula.
*/

#define SHOW_WARNINGS
#ifdef SHOW_WARNINGS
	//printf se lahko zamenja s kaksno drugo funkcijo, ce bi bilo potrebno.
	#define WAR_PRINTF(format, ...) printf(format, ##__VA_ARGS__)
#else
	#define WAR_PRINTF(format, ...)
#endif

typedef struct {
	uint16_t can_id; //Node_ID
	uint8_t data[8];
}CAN_msg;

char* bco_response_parser(CAN_msg response);
CAN_msg bco_str_command_parser(char* str);

#endif
