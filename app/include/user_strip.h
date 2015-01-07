#ifndef __USER_ESPSWITCH_H__
#define __USER_ESPSWITCH_H__

#include "driver/uart.h"
#include "espconn.h"

//#include "driver/key.h"

/* NOTICE---this is for 512KB spi flash.
 * you can change to other sector if you use other size spi flash. */
#define PRIV_PARAM_START_SEC		0x3C

#define PRIV_PARAM_SAVE     0

#define PLUG_KEY_NUM            1


struct strip_saved_param {
    uint8_t status;
    uint8_t pad[3];
};

struct uart_param {
	uint8_t buf[128];
	unsigned short len;
	struct espconn *pespconn;
};

void parse_by_mcu(void *arg, char *pusrdata, unsigned short length);

#endif

