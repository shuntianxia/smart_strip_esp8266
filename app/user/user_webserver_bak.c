/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: user_webserver.c
 *
 * Description: The web server mode configration.
 *              Check your hardware connection with the host while use this mode.
 * Modification history:
 *     2014/3/12, v1.0 create this file.
*******************************************************************************/
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "mem.h"
#include "user_interface.h"

#include "user_iot_version.h"
#include "espconn.h"
#include "user_json.h"
#include "user_webserver.h"

#include "upgrade.h"
#if ESP_PLATFORM
#include "user_esp_platform.h"
#endif
#include "user_strip.h"

#define ESP_DEBUG

#ifdef ESP_DEBUG
#define ESP_DBG os_printf
#else
#define ESP_DBG
#endif

LOCAL os_timer_t wifi_timer;
LOCAL struct station_config *sta_conf;
LOCAL struct softap_config *ap_conf;
uint8 upgrade_lock = 0;
LOCAL os_timer_t app_upgrade_10s;
LOCAL os_timer_t upgrade_check_timer;
extern struct esp_platform_saved_param esp_param;

void ICACHE_FLASH_ATTR
user_esp_check_wifi(void)
{
	uint8 mode;
	struct ip_info ipconfig;
    os_timer_disarm(&wifi_timer);
	mode = wifi_get_opmode();
    wifi_get_ip_info(STATION_IF, &ipconfig);
	if(mode == STATION_MODE) {
		if ((wifi_station_get_connect_status() == STATION_WRONG_PASSWORD ||
                wifi_station_get_connect_status() == STATION_NO_AP_FOUND ||
                wifi_station_get_connect_status() == STATION_CONNECT_FAIL)) {
                //user_esp_reset_mode();
				wifi_set_opmode(STATIONAP_MODE);
				wifi_station_connect();
		}
	}
	else if(mode == STATIONAP_MODE) {
		if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0) {
			wifi_set_opmode(STATION_MODE);
		}
	}
}

/******************************************************************************
 * FunctionName : webserver_recv
 * Description  : Processing the received data from the server
 * Parameters   : arg -- Additional argument to pass to the callback function
 *                pusrdata -- The received data (or NULL when the connection has been closed!)
 *                length -- The length of received data
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
webserver_recv(void *arg, char *pusrdata, unsigned short length)
{
    struct espconn *ptrespconn = arg;
    char DeviceBuffer[40] = {0};
	struct station_config sta_config;

    if (pusrdata == NULL) {
        return;
    }
	char *p = pusrdata;
	char *q = DeviceBuffer;
	unsigned short cmd_head = (*p << 8) | *(p+1);
	uint16 snd_buf_len = 0;
	os_memcpy(DeviceBuffer,pusrdata,3);

	if((cmd_head >> 8) == 0xbd || (cmd_head >> 8) == 0xbe)
	{
		switch(cmd_head)
		{
			case 0xbd01:
				*(q+2) = 0xbd;
				*(q+3) = 0xbe;
				wifi_get_macaddr(STATION_IF, q+4);
				if(esp_param.is_set_name == 1) {
					*(q+10) = os_strlen(esp_param.devname);
					os_memcpy(q+11, esp_param.devname, os_strlen(esp_param.devname));
					snd_buf_len = 11 + os_strlen(esp_param.devname);
				}
				else {
					*(q+10) = 0;
					snd_buf_len = 11;
				}
        		espconn_sent(ptrespconn, DeviceBuffer, snd_buf_len);
				break;

			case 0xbd02:
				os_memset(esp_param.devname, 0, sizeof(esp_param));
				os_memcpy(esp_param.devname, p+3, *(p+2));
				esp_param.is_set_name = 1;
				user_esp_platform_save_param(&esp_param);
				*(q+2) = 0;
				snd_buf_len = 3;
				espconn_sent(ptrespconn, DeviceBuffer, snd_buf_len);
				
				break;
		
			case 0xbd03:
				*(q+4) = 0;
				snd_buf_len = CMD_HEAD_BYTES + *(q+4);
				espconn_sent(ptrespconn, DeviceBuffer, snd_buf_len);
				if (*(p+5) == SOFTAP_MODE) {
					if(wifi_get_opmode() != SOFTAP_MODE) {
						ESP_DBG("change to SOFTAP mode\n");
						wifi_station_disconnect();
						wifi_set_opmode(SOFTAP_MODE);
						system_restart();
						//os_timer_disarm(&wifi_timer);
					}
				}
				else if(*(p+5) == STATION_MODE)
				{
					if(wifi_get_opmode() == SOFTAP_MODE) {
						ESP_DBG("change to STATION mode\n");
						wifi_set_opmode(STATIONAP_MODE);
						system_restart();
						//os_timer_disarm(&wifi_timer);
						//os_timer_setfn(&wifi_timer, (os_timer_func_t *)user_esp_check_wifi, NULL);
	            		//os_timer_arm(&wifi_timer, 3000, 1);
					}
				}
				break;
				
			case 0xbd04:
				os_memset(&sta_config, 0, sizeof(struct station_config));
				os_memcpy(&sta_config.ssid, p+7, *(p+5));
				os_memcpy(&sta_config.password, p+7+*(p+5), *(p+6));
				*(q+4) = 0;
				snd_buf_len = CMD_HEAD_BYTES + *(q+4);
				ESP_DBG("ssid is %s, passwd is %s.\n", &sta_config.ssid, &sta_config.password);
				espconn_sent(ptrespconn, DeviceBuffer, snd_buf_len);
				
				if (&sta_config.ssid[0] != 0x00) {
					if (wifi_get_opmode() == SOFTAP_MODE) {
						ESP_DBG("change to STATIONAP mode\n");
						wifi_set_opmode(STATIONAP_MODE);
						wifi_station_set_config(&sta_config);
						system_restart();
						//wifi_station_connect();
						//os_timer_disarm(&wifi_timer);
						//os_timer_setfn(&wifi_timer, (os_timer_func_t *)user_esp_check_wifi, NULL);
            			//os_timer_arm(&wifi_timer, 100, 0);
					} else {
						ESP_DBG("station mode reconnect\n");
						wifi_station_set_config(&sta_config);
						wifi_station_disconnect();
						wifi_station_connect();
					}
				}
				break;
				
			default:
				parse_by_mcu(arg, pusrdata, length);
				ESP_DBG("smart plug command, transfer to mcu via uart0\n");
				break;
		}
	}
}

/******************************************************************************
 * FunctionName : webserver_recon
 * Description  : the connection has been err, reconnection
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL ICACHE_FLASH_ATTR
void webserver_recon(void *arg, sint8 err)
{
    struct espconn *pesp_conn = arg;

    os_printf("webserver's %d.%d.%d.%d:%d err %d reconnect\n", pesp_conn->proto.tcp->remote_ip[0],
    		pesp_conn->proto.tcp->remote_ip[1],pesp_conn->proto.tcp->remote_ip[2],
    		pesp_conn->proto.tcp->remote_ip[3],pesp_conn->proto.tcp->remote_port, err);
}

/******************************************************************************
 * FunctionName : webserver_recon
 * Description  : the connection has been err, reconnection
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL ICACHE_FLASH_ATTR
void webserver_discon(void *arg)
{
    struct espconn *pesp_conn = arg;

    os_printf("webserver's %d.%d.%d.%d:%d disconnect\n", pesp_conn->proto.tcp->remote_ip[0],
        		pesp_conn->proto.tcp->remote_ip[1],pesp_conn->proto.tcp->remote_ip[2],
        		pesp_conn->proto.tcp->remote_ip[3],pesp_conn->proto.tcp->remote_port);
}

/******************************************************************************
 * FunctionName : user_accept_listen
 * Description  : server listened a connection successfully
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
webserver_listen(void *arg)
{
    struct espconn *pesp_conn = arg;

    espconn_regist_recvcb(pesp_conn, webserver_recv);
    espconn_regist_reconcb(pesp_conn, webserver_recon);
    espconn_regist_disconcb(pesp_conn, webserver_discon);
}

//////////////////////////////////////////////////////////////
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
    }
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
    }*/
}

///////////////////////////////////////////////////////////////

/******************************************************************************
 * FunctionName : user_webserver_init
 * Description  : parameter initialize as a server
 * Parameters   : port -- server port
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_webserver_init(uint32 port)
{
	if (wifi_get_opmode() != STATION_MODE) {
        struct softap_config config;
        char password[33];
        char macaddr[6];

        wifi_softap_get_config(&config);
        wifi_get_macaddr(SOFTAP_IF, macaddr);

		os_sprintf(config.ssid, "PT-%02X%02X%02X", macaddr[3], macaddr[4], macaddr[5]);
        os_memset(config.password, 0, sizeof(config.password));
		config.authmode = AUTH_OPEN;

        wifi_softap_set_config(&config);
    }
/*
    LOCAL struct espconn esp_conn;
    LOCAL esp_tcp esptcp;

    esp_conn.type = ESPCONN_TCP;
    esp_conn.state = ESPCONN_NONE;
    esp_conn.proto.tcp = &esptcp;
    esp_conn.proto.tcp->local_port = port;
    espconn_regist_connectcb(&esp_conn, webserver_listen);

#ifdef SERVER_SSL_ENABLE
    espconn_secure_accept(&esp_conn);
#else
    espconn_accept(&esp_conn);
#endif*/

	if (wifi_get_opmode() != SOFTAP_MODE) {
		os_timer_disarm(&wifi_timer);
		os_timer_setfn(&wifi_timer, (os_timer_func_t *)user_esp_check_wifi, NULL);
		os_timer_arm(&wifi_timer, 3000, 1);
	}

	//add for tmp use

	ptrespconn.type = ESPCONN_UDP;
    ptrespconn.proto.udp = (esp_udp *)os_zalloc(sizeof(esp_udp));
    ptrespconn.proto.udp->local_port = 8088;
    espconn_regist_recvcb(&ptrespconn, webserver_recv);
    espconn_create(&ptrespconn);
}
