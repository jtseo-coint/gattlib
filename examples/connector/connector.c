/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2017  Olivier Martin <olivier@labapart.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <sys/time.h>

#include <glib.h>

#include "iot_slave.h"
#include "gattlib.h"

// Battery Level UUID
const uuid_t g_battery_level_uuid = CREATE_UUID16(0x2A19);

static GMainLoop *m_main_loop;

unsigned int timeGetTime()
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_usec/1000;
}

typedef struct __iiot_slave__{
	uuid_t	uuid_noti, uuid_write, uuid_battery, uuid_serialnum;
	unsigned int	holding_time;
	unsigned int	last_update_time;
	gatt_connection_t* connection;
	int	humit, degree, radio_pow, battery_lev;
	char device_str[128];
	char serial_str[128];
	char data[1024];
} STIIOT_Slave;

#define MAX_SLAVE	100
static	STIIOT_Slave	g_connections[MAX_SLAVE];
static	int				g_connection_cnt = 0;

bool iot_reset()
{
	g_connection_cnt = 0;

	uuid_t uuid_noti, uuid_write, uuid_sn;
	const char *uuid_str = "2a25";
	if (gattlib_string_to_uuid(uuid_str, strlen(uuid_str) + 1, &uuid_sn) < 0) {
		return false;
	}
	uuid_str = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
	if (gattlib_string_to_uuid(uuid_str, strlen(uuid_str) + 1, &uuid_write) < 0) {
		return false;
	}
	uuid_str = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
	if (gattlib_string_to_uuid(uuid_str, strlen(uuid_str) + 1, &uuid_noti) < 0) {
		return false;
	}
	for(int i=0; i<MAX_SLAVE; i++)
	{
		g_connections[i].uuid_noti	= uuid_noti;
		g_connections[i].uuid_write	= uuid_write;
		g_connections[i].uuid_serialnum	= uuid_sn;
		g_connections[i].device_str[0] = 0;
		g_connections[i].holding_time = 600000; // 60 * 10 sec(10 minute);
		g_connections[i].connection = NULL;
	}
}

void notification_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
	STIIOT_Slave *slave = (STIIOT_Slave*)user_data;

	strncpy(slave->data, (char*)data, data_length);
	slave->last_update_time = timeGetTime();
	slave->data[data_length] = 0;
	printf("Notification: %s %s %d\n", slave->serial_str, slave->data, data_length);
}

bool slave_disconnect(STIIOT_Slave *_slave)
{
	gattlib_notification_stop(_slave->connection, &_slave->uuid_noti);
	gattlib_disconnect(_slave->connection);
	_slave->connection = NULL;
	return true;
}

bool slave_add(const char *_device_str, STIIOT_Slave *_slave)
{
	gatt_connection_t* connection;
	if(g_connection_cnt >= MAX_SLAVE)
	{
		fprintf(stderr, "fail to add slave.(over max limit %d)\n", MAX_SLAVE);
		return false;
	}

	strcpy(_slave->device_str, _device_str);
	
	connection = gattlib_connect(NULL, _device_str, GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT);
	if (connection == NULL) {
		fprintf(stderr, "Fail to connect to the bluetooth device. %s\n", _device_str);
		return false;
	}
	_slave->connection = connection;
	printf("connected. %s %d\n", _device_str, g_connection_cnt);

	size_t len;
	char *buffer = NULL;
	ret = gattlib_read_char_by_uuid(connection, &_slave->uuid_serialnum, (void **)&buffer, &len);

	if(ret != GATTLIB_SUCCESS)
	{
		slave_disconnect(_slave);
		return false;
	}
	strcpy(_slave->serial_str, buffer);

	gattlib_register_notification(connection, notification_handler, (void*)_slave);
	ret = gattlib_notification_start(connection, &_slave->uuid_noti);
	if (ret) {
		fprintf(stderr, "Fail to start notification.\n");
		slave_disconnect(_slave);
		return false;
	}
	//mark_
	
	return true;
}

int g_sockfd, g_portno = 1337;
struct sockaddr_in g_serv_addr;
struct hostent *g_server;

bool socket_connect()
{
    //g_portno = 1337;//atoi(argv[2]);
    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0) 
	{
        error("ERROR opening socket");
		return false;
	}
    g_server = gethostbyname("127.0.0.1");
    if (g_server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        return false;
    }
    bzero((char *) &g_serv_addr, sizeof(g_serv_addr));
    g_serv_addr.sin_family = AzF_INET;
    bcopy((char *)g_server->h_addr, (char *)&g_serv_addr.sin_addr.s_addr, g_server->h_length);
    g_serv_addr.sin_port = htons(g_portno);
    if (connect(sockfd,(struct sockaddr *) &g_serv_addr,sizeof(g_serv_addr)) < 0) 
	{
        error("ERROR connecting");
		return false;
	}
	return true;
}

bool socket_disconnect()
{
    close(g_sockfd);
	return true;
}

static void on_user_abort(int arg) {
	g_main_loop_quit(m_main_loop);
}

static void usage(char *argv[]) {
	printf("%s <device_address>\n", argv[0]);
}

int main(int argc, char *argv[]) {
	int ret=1;
	
	if(slave_add("D1:A6:5A:2C:B0:36", &g_connections[g_connection_cnt]))
		g_connection_cnt++;
	
	//mark_
	// Catch CTRL-C
	signal(SIGINT, on_user_abort);

	m_main_loop = g_main_loop_new(NULL, 0);
	g_main_loop_run(m_main_loop);

	// In case we quit the main loop, clean the connection
	g_main_loop_unref(m_main_loop);

	for(int i=0; i<g_connection_cnt; i++)
		slave_disconnect(&g_connections[i]);
		
	puts("Done");
	return ret;
}
