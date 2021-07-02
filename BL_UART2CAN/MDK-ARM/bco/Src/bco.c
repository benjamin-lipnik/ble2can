#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bco.h"

//oblika : (st_parametrov : 3), (id:5)
#define MSG_TYPE_BAD (0)
#define MSG_TYPE_R_E ((3 << 5) | 1)
#define MSG_TYPE_W_E ((5 << 5) | 2)
#define MSG_TYPE_W_S ((4 << 5) | 3)
#define MSG_TYPE_D_W ((2 << 5) | 4)
#define MSG_TYPE_R   ((3 << 5) | 5)
#define MSG_TYPE_D_R ((0 << 5) | 6)

typedef struct{
	uint16_t node_id;
	uint16_t index;
	uint8_t cmd;
	uint8_t sub;
	uint8_t data[7];
}CANOpen_msg;

CANOpen_msg last_request;

static const uint32_t bco_error_codes[] = {
	0x05030000,
	0x05040000,
	0x05040001,
	0x05040005,
	0x06010000,
	0x06010001,
	0x06010002,
	0x06020000,
	0x06040041,
	0x06040042,
	0x06040043,
	0x06040047,
	0x06060000,
	0x06070010,
	0x06070012,
	0x06070013,
	0x06090011,
	0x06090030,
	0x06090031,
	0x06090032,
	0x06090036,
	0x08000000,
	0x08000020,
	0x08000021,
	0x08000022,
	0x08000023,
	0x08000024
};

static const char* bco_error_strings[] = {
	"Toggle bit not alternated.",
	"SDO protocol timed out.",
	"Client/server command specifier not valid or unknown.",
	"Out of memory.",
	"Unsupported access to an object.",
	"Attempt to read a write only object.",
	"Attempt to write a read only object.",
	"Object does not exist in the object dictionary.",
	"Object cannot be mapped to the PDO.",
	"The number and length of the objects to be mapped would exceedPDO length.",
	"General parameter incompatibility reason.",
	"General internal incompatibility in the device.",
	"Access failed due to an hardware error.",
	"Data type does not match, length of service parameter does not match.",
	"Data type does not match, length of service parameter too high.",
	"Data type does not match, length of service parameter too low.",
	"Sub-index does not exist.",
	"Value range of parameter exceeded.",
	"Value of parameter written too high.",
	"Value of parameter written too low.",
	"Maximum value is less than minimum value.",
	"General error.",
	"Data could not be transfered or stored.",
	"Data could not be transfered due to 'local control'.",
	"Data could not be transfered due to 'device state'.",
	"Object dictionary does not exist."
};

char* bco_get_error_string(uint32_t error_code) {
	uint8_t len = sizeof(bco_error_strings) / sizeof(char*);
	for(int i = 0; i < len; i++) {
		if(bco_error_codes[i] == error_code) {
			return (char*)bco_error_strings[i];
		}
	}
	return "Unknown error message.";
}

CAN_msg bco_std_frame_to_can(CANOpen_msg msg) {
	CAN_msg result = {0};
	result.can_id = msg.node_id;
	memcpy(result.data  , &msg.cmd  , 1);
	memcpy(result.data+1, &msg.index, 2);
	memcpy(result.data+3, &msg.sub  , 1);
	memcpy(result.data+4, msg.data  , 4);
	return result;
}

CAN_msg bco_segment_frame_to_can(CANOpen_msg msg) {
	CAN_msg result = {0};
	result.can_id = msg.node_id;
	memcpy(result.data  , &msg.cmd, 1);
	memcpy(result.data+1, msg.data, 7);
	return result;
}

CANOpen_msg bco_can_to_std_frame(CAN_msg msg) {
	CANOpen_msg result = {0};
	result.node_id = 0x127;
	memcpy(&result.cmd  , msg.data  , 1);
	memcpy(&result.index, msg.data+1, 2);
	memcpy(&result.sub  , msg.data+3, 1);
	memcpy(result.data  , msg.data+4, 4);
	return result;
}

void* bco_reverse_memcpy(void* dest, void* source, uint8_t len) {
	for(uint8_t i = 0; i < len; i++) {
		((uint8_t*)dest)[i] = ((uint8_t*)source)[len-1-i];
	}
	return dest;
}

//EXPEDITED
//sdo_r_e NodeID,Index,Sub\r
CANOpen_msg bco_expedited_read_req(uint16_t node_id, uint16_t index, uint8_t sub) {
	CANOpen_msg result = {0};

	result.node_id = node_id;
	result.cmd = 0x40;
	result.index = index;
	result.sub = sub;

	last_request = result;
	last_request.cmd ^= (1<<4);
	return result;
}

//sdo_w_e NodeID,Index,Sub,Len,Data
CANOpen_msg bco_expedited_write_req(uint16_t node_id, uint16_t index, uint8_t sub, uint8_t len, void* data) {
	CANOpen_msg result = {0};

	if(len>4) {
		WAR_PRINTF("Error: Max len = 4.\r\n");
		return result;
	}

	result.node_id = node_id;
	result.cmd = 0x23 | (4-len) << 2;
	result.index = index;
	result.sub = sub;
	bco_reverse_memcpy(result.data, data, 4);

	last_request = result;
	return result;
}

//SEGMENTED
//sdo_d_r
CANOpen_msg bco_segment_read_req() {
	CANOpen_msg result = {0};

	uint8_t last_toggle = last_request.cmd & (1<<4);

	result.node_id = last_request.node_id;
	result.cmd = (0x60 | last_toggle) ^ (1<<4);

	last_request = result;
	return result;
}

//sdo_w_s NodeID,Index,Sub,Len
CANOpen_msg bco_segmented_write_init_req(uint16_t node_id, uint16_t index, uint8_t sub, uint32_t len) {
	CANOpen_msg result = {0};

	result.cmd = 0x21;
	result.node_id = node_id;
	result.index = index;
	result.sub = sub;
	memcpy(result.data, &len, 4);

	last_request = result;
	last_request.cmd ^= (1<<4);
	return result;
}

//sdo_d_w Len,Data
CANOpen_msg bco_segment_write(uint32_t len, void* data) {
	CANOpen_msg result = {0};

	uint8_t last_toggle = last_request.cmd & (1<<4);

	result.node_id = last_request.node_id;
	result.cmd = (0x00 | last_toggle) ^ (1<<4);
	if(len < 7) {
		result.cmd |= (7-len) << 1; //n
		result.cmd |= 0x01; 				//c
	}
	bco_reverse_memcpy(result.data, data, len);

	last_request = result;
	return result;
}

/////
uint8_t bco_hex_char_to_nibble(char c) {
        if (c >= '0' && c <= '9') c = c - '0';
        else if (c >= 'a' && c <='f') c = c - 'a' + 10;
        else if (c >= 'A' && c <='F') c = c - 'A' + 10; 
        return c & 0x0F;
}
void bco_hex_str_to_int(char* hex, void* data_in) {
		uint8_t* data = (uint8_t*)data_in;
		if(hex[0] == 'x' || hex[0] == 'X')
			hex++;
		else if(hex[1] == 'x' || hex[1] == 'X')
			hex+=2;
		if(!hex)
				return;

		uint8_t hex_len = strlen(hex);
		uint8_t skip_first = hex_len & 0x01;
		char l;
		char d;

		if(skip_first) {
				l = '0';
				d = hex[0];
		}
		else {
				l = hex[0];
				d = hex[1];
		}

		*(uint8_t*)(data++) = bco_hex_char_to_nibble(l) << 4 | bco_hex_char_to_nibble(d);

		for(uint8_t i = 2 - skip_first; i < hex_len; i+=2) {
				l = hex[i];
				d = hex[i+1];
				*(uint8_t*)(data++) = bco_hex_char_to_nibble(l) << 4 | bco_hex_char_to_nibble(d);
		}		
}

CAN_msg bco_str_command_parser(char* str) {
	uint32_t len;
	uint8_t msg_type = (MSG_TYPE_BAD);
	CANOpen_msg result = {0};

	if(!str) {
		return bco_std_frame_to_can(result);
	}

	//Drobljenje ukaza na posamezne dele
	char* str_values[8] = {str,0};
	uint8_t str_values_cnt = 1;
	for(char i = 1, l = str[0]; str[i]; i++) {
		if (str[i] == ',' || str[i] == ' ') {
			str[i] = 0;
		}else if(!l) {
			str_values[str_values_cnt++] = (str+i);
		}
		l = str[i];
	}

	//Ugotavljanje veljavnosti in tip ukaza
	if(strncmp(str_values[0], "sdo_", 4) != 0)
		msg_type = 0;
	else {
		str_values[0] += 4;
		msg_type =
			(MSG_TYPE_R_E) * !strncmp(str_values[0], "r_e", 4) +
			(MSG_TYPE_W_E) * !strncmp(str_values[0], "w_e", 4) +
			(MSG_TYPE_W_S) * !strncmp(str_values[0], "w_s", 4) +
			(MSG_TYPE_D_W) * !strncmp(str_values[0], "d_w", 4) +
			(MSG_TYPE_R)   * !strncmp(str_values[0], "r", 2)   +
			(MSG_TYPE_D_R) * !strncmp(str_values[0], "d_r", 4);
	}

	if(!msg_type) {
		WAR_PRINTF("Error: Neznan ukaz.\r\n");
		return bco_std_frame_to_can(result);
	}

	uint8_t str_expected_cnt = msg_type >> 5;
	if((str_values_cnt - 1) != str_expected_cnt) {
		WAR_PRINTF("Error: Nepravilno stevilo parametrov. Pricakovano: %d, Realno: %d\r\n", str_expected_cnt, str_values_cnt-1);
		return bco_std_frame_to_can(result);
	}

	//Interpretiranje ukaza
	char* tmp;
	switch(msg_type) { //NodeID, Index,Sub
		case (MSG_TYPE_R_E):
		case (MSG_TYPE_W_E):
		case (MSG_TYPE_W_S):
		case (MSG_TYPE_R):
			result.node_id = atoi(str_values[1]);
			result.index = strtol(str_values[2], &tmp, 16);
			result.sub = atoi(str_values[3]);
	}
	switch(msg_type) {
		case (MSG_TYPE_W_E): //(NodeID,Index,Sub),Len,Data
			{
				uint32_t tmp_data = strtol(str_values[5], &tmp, 16);
				memcpy(result.data, (void*)&tmp_data, 4);
			}
		case (MSG_TYPE_W_S): //(NodeID,Index,Sub),Len
			len = atoi(str_values[4]);
			break;
		case (MSG_TYPE_D_W): //Len,Data
			len = atoi(str_values[1]);
		  {
				uint8_t hex_buffer[14] = {0};
				bco_hex_str_to_int(str_values[2], hex_buffer);
				bco_reverse_memcpy(result.data, hex_buffer, len);
			}
			break;
	}
	switch(msg_type) {
		case (MSG_TYPE_R_E):
		case (MSG_TYPE_R):
			return bco_std_frame_to_can(bco_expedited_read_req(result.node_id, result.index,result.sub));
		case (MSG_TYPE_W_E):
			return bco_std_frame_to_can(bco_expedited_write_req(result.node_id, result.index, result.sub, len, result.data));
		case (MSG_TYPE_D_R):
			return bco_std_frame_to_can(bco_segment_read_req());
		case (MSG_TYPE_W_S):
			return bco_std_frame_to_can(bco_segmented_write_init_req(result.node_id, result.index, result.sub, len));
		case (MSG_TYPE_D_W):
			//SEGMENT FRAME
			return bco_segment_frame_to_can(bco_segment_write(len, result.data));
	}
	return bco_std_frame_to_can(result);
}

char* bco_convert_arr_to_str(uint8_t len, void* data) {
	static char result_text[256] = {0};
	sprintf(result_text, "");

	if(!len) {
		return result_text;
	}
	if(len > 128) {
		WAR_PRINTF("Error: Function bco_convert_arr_to_str can only convert up to 128 bytes.\r\n");
		return result_text;
	}
	while(len--) {
		sprintf(result_text, "%s%02x", result_text, ((uint8_t*)data)[len]);
	}
	return result_text;
}

char* bco_response_parser(CAN_msg can_response) {
	CANOpen_msg response = bco_can_to_std_frame(can_response);

	static char result_text[256];
	snprintf(result_text, 256, "Error: Nepoznan CANOpen SDO frame response.\r\n");

	//uint16_t node_id = 0;
	uint16_t index = 0;
	//uint8_t cmd = 0;
	//uint8_t sub = 0;
	uint8_t len = 0;
	uint8_t data[7] = {0};

	switch(response.cmd) {
		case 0x20: //segmente write response ok (sdo_d_w)
		case 0x30:
		case 0x41: //segmented read request response
		case 0x60: //expedited write response (sdo_w_e OK), segmented_ write init response (sdo_w_s) ok
			snprintf(result_text, 256, "OK\r\n");
			return result_text;
		case 0x80: //Error
			{
				uint32_t error_code;
				memcpy((void*)&error_code, response.data, 4);
				snprintf(result_text, 256, "ERR 0x%02x, %s\r\n", error_code, bco_get_error_string(error_code));
			}
			return result_text;
	}

	if(response.cmd <= 0x1F) { //Segmented read resp
		uint8_t last_segment = response.cmd & 1;
		len = 7 - ((response.cmd >> 1) & 0x07);
		char data_buffer[7];
		bco_reverse_memcpy(data_buffer, can_response.data+1,len); //Podatke vzame diretktno iz can_response.data, ne pa iz response.data
		snprintf(result_text, 256, "OK %u,%u,%s\r\n", last_segment, len, bco_convert_arr_to_str(len,data_buffer));
		return result_text;
	}
	if((response.cmd & 0xE3) == 0x43) { //Expedited read response
		uint8_t len = 4 - ((response.cmd >> 2) & 0x03);
		bco_reverse_memcpy(&index, &response.index, 2);
		//sub = response.sub;
		bco_reverse_memcpy(data, response.data, 4);
		snprintf(result_text, 256, "OK, %u,0x%02x,0x%s\r\n", 1, len, bco_convert_arr_to_str(len, data));
		return result_text;
	}
	return result_text;
}
