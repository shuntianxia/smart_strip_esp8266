/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: user_esp_platform.c
 *
 * Description: The client mode configration.
 *              Check your hardware connection with the host while use this mode.
 *
 * Modification history:
 *     2014/5/09, v1.0 create this file.
*******************************************************************************/
#include "ets_sys.h"
#include "os_type.h"
#include "mem.h"
#include "osapi.h"
#include "user_interface.h"

#include "espconn.h"
#include "user_esp_platform.h"
#include "user_iot_version.h"
#include "upgrade.h"
#include "user_strip.h"

#if ESP_PLATFORM

#define ESP_DEBUG

#ifdef ESP_DEBUG
#define ESP_DBG os_printf
#else
#define ESP_DBG
#endif

#define UPGRADE_FRAME  "{\"path\": \"/v1/messages/\", \"method\": \"POST\", \"meta\": {\"Authorization\": \"token %s\"},\
\"get\":{\"action\":\"%s\"},\"body\":{\"pre_rom_version\":\"%s\",\"rom_version\":\"%s\"}}\n"
#define BEACON_FRAME    "{\"path\": \"/v1/ping/\", \"method\": \"POST\",\"meta\": {\"Authorization\": \"token %s\"}}\n"
#define RPC_RESPONSE_FRAME  "{\"status\": 200, \"nonce\": %d, \"deliver_to_device\": true}\n"
#define TIMER_FRAME     "{\"body\": {}, \"get\":{\"is_humanize_format_simple\":\"true\"},\"meta\": {\"Authorization\": \"Token %s\"},\"path\": \"/v1/device/timers/\",\"post\":{},\"method\": \"GET\"}\n"
#define pheadbuffer "Connection: keep-alive\r\n\
Cache-Control: no-cache\r\n\
User-Agent: Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.1599.101 Safari/537.36 \r\n\
Accept: */*\r\n\
Accept-Encoding: gzip,deflate,sdch\r\n\
Accept-Language: zh-CN,zh;q=0.8\r\n\r\n"
LOCAL uint8 ping_status;
LOCAL os_timer_t beacon_timer;

#ifdef USE_DNS
ip_addr_t esp_server_ip;
#endif

LOCAL struct espconn user_conn;
LOCAL struct espconn ptrespconn; //add by gzq
LOCAL struct _esp_tcp user_tcp;
LOCAL os_timer_t client_timer;
LOCAL os_timer_t wifi_timer; //add by gzq

//LOCAL struct esp_platform_saved_param esp_param;
struct esp_platform_saved_param esp_param;
LOCAL uint8 device_status;
LOCAL uint8 device_recon_count = 0;
LOCAL uint32 active_nonce = 0;
LOCAL uint8 iot_version[20] = {0};
struct rst_info rtc_info;
void user_esp_platform_check_ip(uint8 reset_flag);

/******************************************************************************
 * FunctionName : user_esp_platform_load_param
 * Description  : load parameter from flash, toggle use two sector by flag value.
 * Parameters   : param--the parame point which write the flash
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_load_param(struct esp_platform_saved_param *param)
{
    struct esp_platform_sec_flag_param flag;

    spi_flash_read((ESP_PARAM_START_SEC + ESP_PARAM_FLAG) * SPI_FLASH_SEC_SIZE,
                   (uint32 *)&flag, sizeof(struct esp_platform_sec_flag_param));

    if (flag.flag == 0) {
        spi_flash_read((ESP_PARAM_START_SEC + ESP_PARAM_SAVE_0) * SPI_FLASH_SEC_SIZE,
                       (uint32 *)param, sizeof(struct esp_platform_saved_param));
    } else {
        spi_flash_read((ESP_PARAM_START_SEC + ESP_PARAM_SAVE_1) * SPI_FLASH_SEC_SIZE,
                       (uint32 *)param, sizeof(struct esp_platform_saved_param));
    }
}

/******************************************************************************
 * FunctionName : user_esp_platform_save_param
 * Description  : toggle save param to two sector by flag value,
 *              : protect write and erase data while power off.
 * Parameters   : param -- the parame point which write the flash
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_save_param(struct esp_platform_saved_param *param)
{
    struct esp_platform_sec_flag_param flag;

    spi_flash_read((ESP_PARAM_START_SEC + ESP_PARAM_FLAG) * SPI_FLASH_SEC_SIZE,
                   (uint32 *)&flag, sizeof(struct esp_platform_sec_flag_param));

    if (flag.flag == 0) {
        spi_flash_erase_sector(ESP_PARAM_START_SEC + ESP_PARAM_SAVE_1);
        spi_flash_write((ESP_PARAM_START_SEC + ESP_PARAM_SAVE_1) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)param, sizeof(struct esp_platform_saved_param));
        flag.flag = 1;
        spi_flash_erase_sector(ESP_PARAM_START_SEC + ESP_PARAM_FLAG);
        spi_flash_write((ESP_PARAM_START_SEC + ESP_PARAM_FLAG) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)&flag, sizeof(struct esp_platform_sec_flag_param));
    } else {
        spi_flash_erase_sector(ESP_PARAM_START_SEC + ESP_PARAM_SAVE_0);
        spi_flash_write((ESP_PARAM_START_SEC + ESP_PARAM_SAVE_0) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)param, sizeof(struct esp_platform_saved_param));
        flag.flag = 0;
        spi_flash_erase_sector(ESP_PARAM_START_SEC + ESP_PARAM_FLAG);
        spi_flash_write((ESP_PARAM_START_SEC + ESP_PARAM_FLAG) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)&flag, sizeof(struct esp_platform_sec_flag_param));
    }
}

/******************************************************************************
 * FunctionName : user_esp_platform_get_token
 * Description  : get the espressif's device token
 * Parameters   : token -- the parame point which write the flash
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_get_token(uint8_t *token)
{
    if (token == NULL) {
        return;
    }

    os_memcpy(token, esp_param.token, sizeof(esp_param.token));
}

/******************************************************************************
 * FunctionName : user_esp_platform_set_token
 * Description  : save the token for the espressif's device
 * Parameters   : token -- the parame point which write the flash
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_set_token(uint8_t *token)
{
    if (token == NULL) {
        return;
    }

    esp_param.activeflag = 0;
    os_memcpy(esp_param.token, token, os_strlen(token));
    user_esp_platform_save_param(&esp_param);
}

/******************************************************************************
 * FunctionName : user_esp_platform_set_active
 * Description  : set active flag
 * Parameters   : activeflag -- 0 or 1
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_set_active(uint8 activeflag)
{
    esp_param.activeflag = activeflag;
    user_esp_platform_save_param(&esp_param);
}

void ICACHE_FLASH_ATTR
user_esp_platform_set_connect_status(uint8 status)
{
    device_status = status;
}

/******************************************************************************
 * FunctionName : user_esp_platform_get_connect_status
 * Description  : get each connection step's status
 * Parameters   : none
 * Returns      : status
*******************************************************************************/
uint8 ICACHE_FLASH_ATTR
user_esp_platform_get_connect_status(void)
{
    uint8 status = wifi_station_get_connect_status();

    if (status == STATION_GOT_IP) {
        status = (device_status == 0) ? DEVICE_CONNECTING : device_status;
    }

    ESP_DBG("status %d\n", status);
    return status;
}

/******************************************************************************
 * FunctionName : user_esp_platform_reconnect
 * Description  : reconnect with host after get ip
 * Parameters   : pespconn -- the espconn used to reconnect with host
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_reconnect(struct espconn *pespconn)
{
    ESP_DBG("user_esp_platform_reconnect\n");

    user_esp_platform_check_ip(0);
}

/******************************************************************************
 * FunctionName : user_esp_platform_discon_cb
 * Description  : disconnect successfully with the host
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_discon_cb(void *arg)
{
    struct espconn *pespconn = arg;

    ESP_DBG("user_esp_platform_discon_cb\n");

    os_timer_disarm(&beacon_timer);

    if (pespconn == NULL) {
        return;
    }

    pespconn->proto.tcp->local_port = espconn_port();

    //user_link_led_output(1);

    user_esp_platform_reconnect(pespconn);
}

/******************************************************************************
 * FunctionName : user_esp_platform_discon
 * Description  : A new incoming connection has been disconnected.
 * Parameters   : espconn -- the espconn used to disconnect with host
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_discon(struct espconn *pespconn)
{
    ESP_DBG("user_esp_platform_discon\n");

    //user_link_led_output(1);

#ifdef CLIENT_SSL_ENABLE
    espconn_secure_disconnect(pespconn);
#else
    espconn_disconnect(pespconn);
#endif
}

/******************************************************************************
 * FunctionName : user_esp_platform_sent_cb
 * Description  : Data has been sent successfully and acknowledged by the remote host.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_sent_cb(void *arg)
{
    struct espconn *pespconn = arg;

    ESP_DBG("user_esp_platform_sent_cb\n");
}

/******************************************************************************
 * FunctionName : user_esp_platform_sent
 * Description  : Processing the application data and sending it to the host
 * Parameters   : pespconn -- the espconn used to connetion with the host
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_sent(struct espconn *pespconn)
{
	char pbuf[128] = {0};
	char *p = pbuf;

    if (esp_param.activeflag == 0xFF) {
        esp_param.activeflag = 0;
    }

    if (esp_param.activeflag == 0) {
		*(p + 1) = 0x01;
	}
    else {
		*(p + 1) = 0x02;
    }
	*(p + 0) = 0xb1;
	*(p + 2) = 0xbd;
	*(p + 3) = 0xbe;
	wifi_get_macaddr(STATION_IF, p+4);

#ifdef CLIENT_SSL_ENABLE
    espconn_secure_sent(pespconn, pbuf, 10);
#else
    espconn_sent(pespconn, pbuf, 10);
#endif

}

/******************************************************************************
 * FunctionName : user_esp_platform_sent_beacon
 * Description  : sent beacon frame for connection with the host is activate
 * Parameters   : pespconn -- the espconn used to connetion with the host
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_sent_beacon(struct espconn *pespconn)
{
    if (pespconn == NULL) {
        return;
    }

    if (pespconn->state == ESPCONN_CONNECT) {
        if (esp_param.activeflag == 0) {
            ESP_DBG("plese check device is activated.\n");
            user_esp_platform_sent(pespconn);
        } else {

            ESP_DBG("user_esp_platform_sent_beacon %u\n", system_get_time());

            if (ping_status == 0) {
                ESP_DBG("user_esp_platform_sent_beacon sent fail!\n");
                user_esp_platform_discon(pespconn);
            } else {
				char pbuf[128] = {0};
				pbuf[0] = 0xb1;
				pbuf[1] = 0x03;

#ifdef CLIENT_SSL_ENABLE
                espconn_secure_sent(pespconn, pbuf, 2);
#else
                espconn_sent(pespconn, pbuf, 2);
#endif
                ping_status = 0;
                os_timer_arm(&beacon_timer, BEACON_TIME, 0);
            }
        }
    } else {
        ESP_DBG("user_esp_platform_sent_beacon sent fail!\n");
        user_esp_platform_discon(pespconn);
    }
}

/******************************************************************************
 * FunctionName : user_platform_rpc_set_rsp
 * Description  : response the message to server to show setting info is received
 * Parameters   : pespconn -- the espconn used to connetion with the host
 *                nonce -- mark the message received from server
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_platform_rpc_set_rsp(struct espconn *pespconn, int nonce)
{
    char *pbuf = (char *)os_zalloc(packet_size);

    if (pespconn == NULL) {
        return;
    }

    os_sprintf(pbuf, RPC_RESPONSE_FRAME, nonce);
    ESP_DBG("%s\n", pbuf);
#ifdef CLIENT_SSL_ENABLE
    espconn_secure_sent(pespconn, pbuf, os_strlen(pbuf));
#else
    espconn_sent(pespconn, pbuf, os_strlen(pbuf));
#endif
    os_free(pbuf);
}

/******************************************************************************
 * FunctionName : user_platform_timer_get
 * Description  : get the timers from server
 * Parameters   : pespconn -- the espconn used to connetion with the host
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_platform_timer_get(struct espconn *pespconn)
{
    uint8 devkey[token_size] = {0};
    char *pbuf = (char *)os_zalloc(packet_size);
    os_memcpy(devkey, esp_param.devkey, 40);

    if (pespconn == NULL) {
        return;
    }

    os_sprintf(pbuf, TIMER_FRAME, devkey);
    ESP_DBG("%s\n", pbuf);
#ifdef CLIENT_SSL_ENABLE
    espconn_secure_sent(pespconn, pbuf, os_strlen(pbuf));
#else
    espconn_sent(pespconn, pbuf, os_strlen(pbuf));
#endif
    os_free(pbuf);
}

/******************************************************************************
 * FunctionName : user_esp_platform_upgrade_cb
 * Description  : Processing the downloaded data from the server
 * Parameters   : pespconn -- the espconn used to connetion with the host
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_upgrade_rsp(void *arg)
{
    struct upgrade_server_info *server = arg;
    struct espconn *pespconn = server->pespconn;
    uint8 devkey[41] = {0};
    uint8 *pbuf = NULL;
    char *action = NULL;

    os_memcpy(devkey, esp_param.devkey, 40);
    pbuf = (char *)os_zalloc(packet_size);

    if (server->upgrade_flag == true) {
        ESP_DBG("user_esp_platform_upgarde_successfully\n");
        action = "device_upgrade_success";
        os_sprintf(pbuf, UPGRADE_FRAME, devkey, action, server->pre_version, server->upgrade_version);
        ESP_DBG(pbuf);

#ifdef CLIENT_SSL_ENABLE
        espconn_secure_sent(pespconn, pbuf, os_strlen(pbuf));
#else
        espconn_sent(pespconn, pbuf, os_strlen(pbuf));
#endif

        if (pbuf != NULL) {
            os_free(pbuf);
            pbuf = NULL;
        }
    } else {
        ESP_DBG("user_esp_platform_upgrade_failed\n");
        action = "device_upgrade_failed";
        os_sprintf(pbuf, UPGRADE_FRAME, devkey, action,server->pre_version, server->upgrade_version);
        ESP_DBG(pbuf);

#ifdef CLIENT_SSL_ENABLE
        espconn_secure_sent(pespconn, pbuf, os_strlen(pbuf));
#else
        espconn_sent(pespconn, pbuf, os_strlen(pbuf));
#endif

        if (pbuf != NULL) {
            os_free(pbuf);
            pbuf = NULL;
        }
    }

    os_free(server->url);
    server->url = NULL;
    os_free(server);
    server = NULL;
}

/******************************************************************************
 * FunctionName : user_esp_platform_upgrade_begin
 * Description  : Processing the received data from the server
 * Parameters   : pespconn -- the espconn used to connetion with the host
 *                server -- upgrade param
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_upgrade_begin(struct espconn *pespconn, struct upgrade_server_info *server)
{
    uint8 user_bin[9] = {0};
    uint8 devkey[41] = {0};

    server->pespconn = pespconn;

    os_memcpy(devkey, esp_param.devkey, 40);
    os_memcpy(server->ip, pespconn->proto.tcp->remote_ip, 4);

#ifdef UPGRADE_SSL_ENABLE
    server->port = 443;
#else
    server->port = 8080;
#endif

    server->check_cb = user_esp_platform_upgrade_rsp;
    server->check_times = 120000;

    if (server->url == NULL) {
        server->url = (uint8 *)os_zalloc(512);
    }

    if (system_upgrade_userbin_check() == UPGRADE_FW_BIN1) {
        os_memcpy(user_bin, "user2.bin", 10);
    } else if (system_upgrade_userbin_check() == UPGRADE_FW_BIN2) {
        os_memcpy(user_bin, "user1.bin", 10);
    }

//    os_sprintf(server->url, "GET /v1/device/rom/?action=download_rom&version=%s&filename=%s HTTP/1.0\r\nHost: "IPSTR":%d\r\n"pheadbuffer"",
//               server->upgrade_version, user_bin, IP2STR(server->ip),
//               server->port, devkey);
	os_sprintf(server->url, "GET /smart_strip/%s HTTP/1.0\r\nHost: "IPSTR":%d\r\n"pheadbuffer"", user_bin, IP2STR(server->ip), server->port); 
    ESP_DBG(server->url);

#ifdef UPGRADE_SSL_ENABLE

    if (system_upgrade_start_ssl(server) == false) {
#else

    if (system_upgrade_start(server) == false) {
#endif
        ESP_DBG("upgrade is already started\n");
    }
}

/******************************************************************************
 * FunctionName : user_esp_platform_recv_cb
 * Description  : Processing the received data from the server
 * Parameters   : arg -- Additional argument to pass to the callback function
 *                pusrdata -- The received data (or NULL when the connection has been closed!)
 *                length -- The length of received data
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_recv_cb(void *arg, char *pusrdata, unsigned short length)
{
    char *pstr = NULL;
    LOCAL char pbuffer[128] = {0};
    struct espconn *pespconn = arg;
	char *p = pusrdata;
	char *q = pbuffer;
	unsigned short cmd_head = (*p << 8) | *(p+1);
	uint16 snd_buf_len = 0;
	os_memcpy(pbuffer, pusrdata, 2);

    //ESP_DBG("user_esp_platform_recv_cb %s\n", pusrdata);

    os_timer_disarm(&beacon_timer);

	if((cmd_head >> 8) == 0xb1) {
		if (cmd_head == 0xb101) {
			if (*(p + 2) == 0) {
	            ESP_DBG("device activates successful.\n");
				ESP_DBG("cmd_head is %2x %2x\n", pusrdata[0], pusrdata[1]);

	            device_status = DEVICE_ACTIVE_DONE;
	            esp_param.activeflag = 1;
	            user_esp_platform_save_param(&esp_param);
	            user_esp_platform_sent(pespconn);
	        } else {
	            ESP_DBG("device activates failed.\n");
	            device_status = DEVICE_ACTIVE_FAIL;
	        }
	    }
	    else if (cmd_head == 0xb102) {
			//
	    } else if (cmd_head == 0xb103) {
	    	ping_status = 1;
	    	//
	    }
		else if (cmd_head == 0xb104) {
            struct upgrade_server_info *server = NULL;
            //user_platform_rpc_set_rsp(pespconn, nonce);

            server = (struct upgrade_server_info *)os_zalloc(sizeof(struct upgrade_server_info));
            //os_memcpy(server->upgrade_version, pstr + 12, 16);
            //server->upgrade_version[15] = '\0';
            //os_sprintf(server->pre_version,"%s%d.%d.%dt%d(%s)",VERSION_TYPE,IOT_VERSION_MAJOR,\
            //    	IOT_VERSION_MINOR,IOT_VERSION_REVISION,device_type,UPGRADE_FALG);
            user_esp_platform_upgrade_begin(pespconn, server);
        } else if (cmd_head == 0xb105) {
            os_timer_disarm(&client_timer);
            os_timer_setfn(&client_timer, (os_timer_func_t *)system_upgrade_reboot, NULL);
            os_timer_arm(&client_timer, 1000, 0);
        } else if (cmd_head == 0xb106) {
            //user_platform_rpc_set_rsp(pespconn, nonce);
            os_timer_disarm(&client_timer);
            os_timer_setfn(&client_timer, (os_timer_func_t *)user_platform_timer_get, pespconn);
            os_timer_arm(&client_timer, 2000, 0);
        } else if (cmd_head == 0xb107) {
            user_platform_timer_start(pusrdata , pespconn);
        }
	}
	else if((cmd_head >> 8) == 0xbd || (cmd_head >> 8) == 0xbe) {
		parse_by_mcu(arg, pusrdata, length);
		ESP_DBG("smart strip command, transfer to mcu via uart0\n");
	}
	
    os_memset(pbuffer, 0, sizeof(pbuffer));
    os_timer_arm(&beacon_timer, BEACON_TIME, 0);
}

LOCAL bool ICACHE_FLASH_ATTR
user_esp_platform_reset_mode(void)
{
    if (wifi_get_opmode() == STATION_MODE) {
        wifi_set_opmode(STATIONAP_MODE);
    }
    return false;
}

/******************************************************************************
 * FunctionName : user_esp_platform_recon_cb
 * Description  : The connection had an error and is already deallocated.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_recon_cb(void *arg, sint8 err)
{
    struct espconn *pespconn = (struct espconn *)arg;

    ESP_DBG("user_esp_platform_recon_cb\n");

    os_timer_disarm(&beacon_timer);

    user_link_led_output(1);

    if (++device_recon_count == 5) {
        device_status = DEVICE_CONNECT_SERVER_FAIL;

        if (user_esp_platform_reset_mode()) {
            return;
        }
    }
    os_timer_disarm(&client_timer);
    os_timer_setfn(&client_timer, (os_timer_func_t *)user_esp_platform_reconnect, pespconn);
    os_timer_arm(&client_timer, 1000, 0);
}

/******************************************************************************
 * FunctionName : user_esp_platform_connect_cb
 * Description  : A new incoming connection has been connected.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_connect_cb(void *arg)
{
    struct espconn *pespconn = arg;

    ESP_DBG("user_esp_platform_connect_cb\n");

    if (wifi_get_opmode() ==  STATIONAP_MODE ) {
        wifi_set_opmode(STATION_MODE);
    }

//    user_link_led_timer_done();

    device_recon_count = 0;
    espconn_regist_recvcb(pespconn, user_esp_platform_recv_cb);
    espconn_regist_sentcb(pespconn, user_esp_platform_sent_cb);
    user_esp_platform_sent(pespconn);
}

/******************************************************************************
 * FunctionName : user_esp_platform_connect
 * Description  : The function given as the connect with the host
 * Parameters   : espconn -- the espconn used to connect the connection
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_connect(struct espconn *pespconn)
{
    ESP_DBG("user_esp_platform_connect\n");

#ifdef CLIENT_SSL_ENABLE
    espconn_secure_connect(pespconn);
#else
    espconn_connect(pespconn);
#endif
}

#ifdef USE_DNS
/******************************************************************************
 * FunctionName : user_esp_platform_dns_found
 * Description  : dns found callback
 * Parameters   : name -- pointer to the name that was looked up.
 *                ipaddr -- pointer to an ip_addr_t containing the IP address of
 *                the hostname, or NULL if the name could not be found (or on any
 *                other error).
 *                callback_arg -- a user-specified callback argument passed to
 *                dns_gethostbyname
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_dns_found(const char *name, ip_addr_t *ipaddr, void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;

    if (ipaddr == NULL) {
        ESP_DBG("user_esp_platform_dns_found NULL\n");

        if (++device_recon_count == 5) {
            device_status = DEVICE_CONNECT_SERVER_FAIL;

            user_esp_platform_reset_mode();
        }

        return;
    }

    ESP_DBG("user_esp_platform_dns_found %d.%d.%d.%d\n",
            *((uint8 *)&ipaddr->addr), *((uint8 *)&ipaddr->addr + 1),
            *((uint8 *)&ipaddr->addr + 2), *((uint8 *)&ipaddr->addr + 3));

    if (esp_server_ip.addr == 0 && ipaddr->addr != 0) {
        os_timer_disarm(&client_timer);
        esp_server_ip.addr = ipaddr->addr;
        os_memcpy(pespconn->proto.tcp->remote_ip, &ipaddr->addr, 4);

        pespconn->proto.tcp->local_port = espconn_port();

#ifdef CLIENT_SSL_ENABLE
        pespconn->proto.tcp->remote_port = 8443;
#else
        pespconn->proto.tcp->remote_port = 8000;
#endif

        ping_status = 1;

        espconn_regist_connectcb(pespconn, user_esp_platform_connect_cb);
        espconn_regist_disconcb(pespconn, user_esp_platform_discon_cb);
        espconn_regist_reconcb(pespconn, user_esp_platform_recon_cb);
        user_esp_platform_connect(pespconn);
    }
}

/******************************************************************************
 * FunctionName : user_esp_platform_dns_check_cb
 * Description  : 1s time callback to check dns found
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_dns_check_cb(void *arg)
{
    struct espconn *pespconn = arg;

    ESP_DBG("user_esp_platform_dns_check_cb\n");

    espconn_gethostbyname(pespconn, ESP_DOMAIN, &esp_server_ip, user_esp_platform_dns_found);

    os_timer_arm(&client_timer, 1000, 0);
}

LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_start_dns(struct espconn *pespconn)
{
    esp_server_ip.addr = 0;
    espconn_gethostbyname(pespconn, ESP_DOMAIN, &esp_server_ip, user_esp_platform_dns_found);

    os_timer_disarm(&client_timer);
    os_timer_setfn(&client_timer, (os_timer_func_t *)user_esp_platform_dns_check_cb, pespconn);
    os_timer_arm(&client_timer, 1000, 0);
}
#endif

/******************************************************************************
 * FunctionName : user_esp_platform_check_ip
 * Description  : espconn struct parame init when get ip addr
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_check_ip(uint8 reset_flag)
{
    struct ip_info ipconfig;

    os_timer_disarm(&client_timer);

    wifi_get_ip_info(STATION_IF, &ipconfig);

    if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0) {

        //user_link_led_timer_init();

        user_conn.proto.tcp = &user_tcp;
        user_conn.type = ESPCONN_TCP;
        user_conn.state = ESPCONN_NONE;

        device_status = DEVICE_CONNECTING;

        if (reset_flag) {
            device_recon_count = 0;
        }

        os_timer_disarm(&beacon_timer);
        os_timer_setfn(&beacon_timer, (os_timer_func_t *)user_esp_platform_sent_beacon, &user_conn);

#ifdef USE_DNS
        user_esp_platform_start_dns(&user_conn);
#else
        const char esp_server_ip[4] = {192, 168, 0, 154};

        os_memcpy(user_conn.proto.tcp->remote_ip, esp_server_ip, 4);
        user_conn.proto.tcp->local_port = espconn_port();

#ifdef CLIENT_SSL_ENABLE
        user_conn.proto.tcp->remote_port = 8443;
#else
        user_conn.proto.tcp->remote_port = 8000;
#endif
		ping_status = 1;

        espconn_regist_connectcb(&user_conn, user_esp_platform_connect_cb);
		espconn_regist_disconcb(&user_conn, user_esp_platform_discon_cb);
        espconn_regist_reconcb(&user_conn, user_esp_platform_recon_cb);
        user_esp_platform_connect(&user_conn);
#endif
    } else {
        /* if there are wrong while connecting to some AP, then reset mode */
        if ((wifi_station_get_connect_status() == STATION_WRONG_PASSWORD ||
                wifi_station_get_connect_status() == STATION_NO_AP_FOUND ||
                wifi_station_get_connect_status() == STATION_CONNECT_FAIL)) {
            user_esp_platform_reset_mode();
        } else {
            os_timer_setfn(&client_timer, (os_timer_func_t *)user_esp_platform_check_ip, NULL);
            os_timer_arm(&client_timer, 100, 0);
        }
    }
}

LOCAL void ICACHE_FLASH_ATTR
local_recv_cb(void *arg, char *pusrdata, unsigned short length)
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
					char macaddr[6];
					wifi_get_macaddr(STATION_IF, macaddr);
					os_sprintf(q+11, "PT-%02X%02X%02X", macaddr[3], macaddr[4], macaddr[5]);
		
					*(q+10) = 9;
					snd_buf_len = 11 + *(q+10);
				}
        		espconn_sent(ptrespconn, DeviceBuffer, snd_buf_len);
				break;

			case 0xbd02:
				os_memset(esp_param.devname, 0, sizeof(esp_param.devname));
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
						wifi_set_opmode(STATION_MODE);
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
						ESP_DBG("change to STATION mode\n");
						wifi_set_opmode(STATION_MODE);
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

			case 0xbd11: //dev_activate //dev_bind
				if(esp_param.activeflag == 0) {
					

				}
				break;
				
			default:
				parse_by_mcu(arg, pusrdata, length);
				ESP_DBG("smart plug command, transfer to mcu via uart0\n");
				break;
		}
	}
}

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
 * FunctionName : user_esp_platform_init
 * Description  : device parame init based on espressif platform
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_init(void)
{

	os_sprintf(iot_version,"%s%d.%d.%dt%d(%s)",VERSION_TYPE,IOT_VERSION_MAJOR,\
	IOT_VERSION_MINOR,IOT_VERSION_REVISION,device_type,UPGRADE_FALG);
	os_printf("IOT VERSION = %s\n",iot_version);

    user_esp_platform_load_param(&esp_param);

    system_rtc_mem_read(0,&rtc_info,sizeof(struct rst_info));
     if(rtc_info.flag == 1 || rtc_info.flag == 2) {
    	 ESP_DBG("flag = %d,epc1 = 0x%08x,epc2=0x%08x,epc3=0x%08x,excvaddr=0x%08x,depc=0x%08x,\nFatal \
exception (%d): \n",rtc_info.flag,rtc_info.epc1,rtc_info.epc2,rtc_info.epc3,rtc_info.excvaddr,rtc_info.depc,rtc_info.exccause);
     }
    struct rst_info info = {0};
    system_rtc_mem_write(0,&info,sizeof(struct rst_info));
	if (wifi_get_opmode() != STATION_MODE) {
		struct softap_config config;
		char password[33];
		char macaddr[6];
		
		wifi_softap_get_config(&config);
		wifi_get_macaddr(SOFTAP_IF, macaddr);
		
		os_memset(config.ssid, 0, sizeof(config.ssid));
		os_sprintf(config.ssid, "PT-%02X%02X%02X", macaddr[3], macaddr[4], macaddr[5]);
		config.ssid_len = os_strlen(config.ssid);

		config.authmode = AUTH_OPEN;
		
		wifi_softap_set_config(&config);
	}
	//wifi_set_opmode(STATIONAP_MODE);
	
	ptrespconn.type = ESPCONN_UDP;
	ptrespconn.proto.udp = (esp_udp *)os_zalloc(sizeof(esp_udp));
	ptrespconn.proto.udp->local_port = 8088;
	espconn_regist_recvcb(&ptrespconn, local_recv_cb);
	espconn_create(&ptrespconn);

	//user_esp_platform_load_param(&esp_param);
	if(esp_param.lan_only == 1) {
		ESP_DBG("local mode.\n");
		if (wifi_get_opmode() != SOFTAP_MODE) {
			os_timer_disarm(&wifi_timer);
			os_timer_setfn(&wifi_timer, (os_timer_func_t *)user_esp_check_wifi, NULL);
			os_timer_arm(&wifi_timer, 3000, 1);
		}
	}
	else {
		ESP_DBG("local and remote mode.\n");
		if (wifi_get_opmode() != SOFTAP_MODE) {
	        os_timer_disarm(&client_timer);
	        os_timer_setfn(&client_timer, (os_timer_func_t *)user_esp_platform_check_ip, 1);
	        os_timer_arm(&client_timer, 100, 0);
    	}
	}
}

#endif
