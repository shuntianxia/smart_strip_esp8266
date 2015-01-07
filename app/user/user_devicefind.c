/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: user_devicefind.c
 *
 * Description: Find your hardware's information while working any mode.
 *
 * Modification history:
 *     2014/3/12, v1.0 create this file.
*******************************************************************************/
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "mem.h"
#include "user_interface.h"

#include "espconn.h"
#include "user_json.h"
#include "user_devicefind.h"

const char *device_find_request = "Are You Espressif IOT Smart Device?";
#if PLUG_DEVICE
const char *device_find_response_ok = "I'm Plug.";
#elif LIGHT_DEVICE
const char *device_find_response_ok = "I'm Light.";
#elif SENSOR_DEVICE
#if HUMITURE_SUB_DEVICE
const char *device_find_response_ok = "I'm Humiture.";
#elif FLAMMABLE_GAS_SUB_DEVICE
const char *device_find_response_ok = "I'm Flammable Gas.";
#endif
#endif

/*---------------------------------------------------------------------------*/
LOCAL struct espconn ptrespconn;

/******************************************************************************
 * FunctionName : user_devicefind_recv
 * Description  : Processing the received data from the host
 * Parameters   : arg -- Additional argument to pass to the callback function
 *                pusrdata -- The received data (or NULL when the connection has been closed!)
 *                length -- The length of received data
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_devicefind_recv(void *arg, char *pusrdata, unsigned short length)
{
    char DeviceBuffer[40] = {0};
    char Device_mac_buffer[60] = {0};
    char hwaddr[6];
	char *q = DeviceBuffer;

    struct ip_info ipconfig;

    if (wifi_get_opmode() != STATION_MODE) {
        wifi_get_ip_info(SOFTAP_IF, &ipconfig);
        wifi_get_macaddr(SOFTAP_IF, hwaddr);

        if (!ip_addr_netcmp((struct ip_addr *)ptrespconn.proto.udp->remote_ip, &ipconfig.ip, &ipconfig.netmask)) {
            wifi_get_ip_info(STATION_IF, &ipconfig);
            wifi_get_macaddr(STATION_IF, hwaddr);
        }
    } else {
        wifi_get_ip_info(STATION_IF, &ipconfig);
        wifi_get_macaddr(STATION_IF, hwaddr);
    }

    if (pusrdata == NULL) {
        return;
    }
/*
    if (length == 2 && pusrdata[0] == 0xbd && pusrdata[1] == 0x01) {
		*(q+0) = 0xbd;
		*(q+1) = 0x01;
		*(q+2) = 0xbd;
		*(q+3) = 0xbe;
        os_memcpy(q+4, hwaddr, 6);
        length = 10;
        espconn_sent(&ptrespconn, DeviceBuffer, length);
    }*/
	if (length == 5 && pusrdata[0] == 0xbd && pusrdata[1] == 0x01) {
		os_memcpy(q, pusrdata, 3);
		*(q+3) = 0x00;
		*(q+4) = 0x09;
		*(q+5) = 0x01;
		*(q+6) = 0xbd;
		*(q+7) = 0xbe;
        os_memcpy(q+8, hwaddr, 6);
        length = 14;
        espconn_sent(&ptrespconn, DeviceBuffer, length);
    }
}

/******************************************************************************
 * FunctionName : user_devicefind_init
 * Description  : the espconn struct parame init
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_devicefind_init(void)
{
    ptrespconn.type = ESPCONN_UDP;
    ptrespconn.proto.udp = (esp_udp *)os_zalloc(sizeof(esp_udp));
    ptrespconn.proto.udp->local_port = 8088;
    espconn_regist_recvcb(&ptrespconn, user_devicefind_recv);
    espconn_create(&ptrespconn);
}
