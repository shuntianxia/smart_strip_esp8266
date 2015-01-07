/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: user_plug.c
 *
 * Description: plug demo's function realization
 *
 * Modification history:
 *     2014/5/1, v1.0 create this file.
*******************************************************************************/
#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"
#include "mem.h"
#include "user_interface.h"
#include "driver/uart.h"

#include "user_strip.h"

#define ESP_DEBUG

#ifdef ESP_DEBUG
#define ESP_DBG os_printf
#else
#define ESP_DBG
#endif

LOCAL os_timer_t uart_timer;
extern UartMsg uart_msg;

LOCAL void ICACHE_FLASH_ATTR
uart_data_check(void *arg)
{
	struct uart_param *param = (struct uart_param *)arg;
	char buf_err[6] = {0};
	if(uart_msg.state == DATA_READY)
	{
		espconn_sent(param->pespconn, uart_msg.msg_buf, uart_msg.pos);
		ESP_DBG("read uart success: 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x\n", uart_msg.msg_buf[0],uart_msg.msg_buf[1],uart_msg.msg_buf[2],uart_msg.msg_buf[3],uart_msg.msg_buf[4]);
	}
	else
	{
		ESP_DBG("read uart failed\n");
		os_memcpy(buf_err, param->buf, 3);
		buf_err[4] = 0;
		buf_err[5] = 1;
		espconn_sent(param->pespconn, buf_err, 6);
	}
	uart_msg.pos = 0;
	uart_msg.state = DATA_IDLE;
	os_free(param);
}

void ICACHE_FLASH_ATTR
parse_by_mcu(void *arg, char *pusrdata, unsigned short length)
{
	struct uart_param *param = (struct uart_param *)os_zalloc(sizeof(struct uart_param));
	os_memcpy(param->buf, pusrdata, sizeof(param->buf));
	param->len = length;
	param->pespconn = arg;
	
	uart0_tx_buffer(pusrdata, length);
	os_timer_disarm(&uart_timer);
	os_timer_setfn(&uart_timer, (os_timer_func_t *)uart_data_check, param);
	os_timer_arm(&uart_timer, 80, 0);
}

