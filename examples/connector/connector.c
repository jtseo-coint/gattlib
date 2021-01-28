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
	static unsigned int accum = 0, last = 0, init = 0;
	struct timeval now;
	gettimeofday(&now, NULL);

	unsigned int cur = now.tv_usec/1000;
	if(init == 0)
		last = cur;
	
	init = 1;
	if(cur < last)
		last = 0;
	
	accum += cur - last;
	//printf("acc %ud,now %lud/", accum, now.tv_usec);
	
	last = cur;
	return accum;
}

typedef struct __iiot_slave__{
	uuid_t	uuid_noti, uuid_write, uuid_battery, uuid_serialnum;
	unsigned int	holding_time;
	unsigned int	last_update_time;
	unsigned int	time_to_rewrite;
	gatt_connection_t* connection;
	int	humit, degree, radio_pow, battery_lev;
	char device_str[128];
	char serial_str[128];
	char data[1024];
} STIIOT_Slave;

#define MAX_SLAVE	100
static	STIIOT_Slave	g_connections[MAX_SLAVE];
static	int				g_connection_cnt = 0;

int slave_reset()
{
	g_connection_cnt = 0;

	uuid_t uuid_noti, uuid_write, uuid_sn;
	const char *uuid_str = "2a25";
	if (gattlib_string_to_uuid(uuid_str, strlen(uuid_str) + 1, &uuid_sn) < 0) {
		return 0;
	}
	uuid_str = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
	if (gattlib_string_to_uuid(uuid_str, strlen(uuid_str) + 1, &uuid_write) < 0) {
		return 0;
	}
	uuid_str = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
	if (gattlib_string_to_uuid(uuid_str, strlen(uuid_str) + 1, &uuid_noti) < 0) {
		return 0;
	}
	for(int i=0; i<MAX_SLAVE; i++)
	{
		g_connections[i].uuid_noti	= uuid_noti;
		g_connections[i].uuid_write	= uuid_write;
		g_connections[i].uuid_serialnum	= uuid_sn;
		g_connections[i].device_str[0] = 0;
		g_connections[i].holding_time = 2000; //600000; // 60 * 10 sec(10 minute);
		g_connections[i].time_to_rewrite = 5000;
		g_connections[i].connection = NULL;
	}
	return 1;
}

int slave_disconnect(STIIOT_Slave *_slave)
{
	if(_slave->connection == NULL)
		return 0;

	gattlib_notification_stop(_slave->connection, &_slave->uuid_noti);
	gattlib_disconnect(_slave->connection);
	_slave->connection = NULL;
	printf("Disconnected(%s)\n", _slave->serial_str);
	return 1;
}

void notification_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
	STIIOT_Slave *slave = (STIIOT_Slave*)user_data;

	strncpy(slave->data, (char*)data, data_length);
	slave->last_update_time = timeGetTime();
	slave->data[data_length] = 0;
	printf("Notification: %s %s %d\n", slave->serial_str, slave->data, data_length);
	
	char buffer[1024];
	sprintf(buffer, "%s %s", slave->serial_str, slave->data);
	socket_idle(buffer);
	slave_disconnect(slave);
}

int slave_request(STIIOT_Slave *_slave, unsigned int _cur)
{
	int ret = 1;
	ret = gattlib_write_char_by_uuid(_slave->connection, &_slave->uuid_write, "T", 2);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Fail to write. %s\n", _slave->serial_str);
		return 0;
	}
	ret = 1;
	printf("requested: %s\n", _slave->serial_str);

	if(_cur == 0)
		_cur = timeGetTime();
	_slave->last_update_time = _cur;

	return ret;
}

int slave_on_count(unsigned int _cur)
{
	int cnt = 0;
	for(int i=0; i<g_connection_cnt; i++)
	{
		STIIOT_Slave *slave = &g_connections[i];
		if(slave->connection != NULL)
		{
			cnt++;
			if(_cur <= slave->last_update_time + slave->time_to_rewrite)
				continue;
			fprintf(stderr, "try to reconnect.\n");
			slave_disconnect(_slave);
		}
	}
	return cnt;
}

int slave_idle(STIIOT_Slave *_slave, unsigned int _cur)
{
	int ret = 1;
	gatt_connection_t *connection = _slave->connection;
	if(connection != NULL)
	{
		fprintf(stderr, "it's got connection, already.(%s)\n", _slave->serial_str);

		if(_cur <= _slave->last_update_time + _slave->time_to_rewrite)
			return 0;
		fprintf(stderr, "try to reconnect.\n");
		slave_disconnect(_slave);
	}
	connection = gattlib_connect(NULL, _slave->device_str, GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT);
	if (connection == NULL) {
		fprintf(stderr, "Fail to connect to the bluetooth device. %s\n", _slave->device_str);
		return 0;
	}
	_slave->connection = connection;
	printf("connected. %s %d\n", _slave->device_str, g_connection_cnt);

	if(_cur == 0)
	{
		size_t len;
		char *buffer = NULL;
		ret = gattlib_read_char_by_uuid(_slave->connection
			, &_slave->uuid_serialnum, (void **)&buffer, &len);

		if(ret != GATTLIB_SUCCESS)
		{
			slave_disconnect(_slave);
			return 0;
		}
		printf("serial: %s\n", buffer);
		strcpy(_slave->serial_str, buffer);
	}

	slave_request(_slave, _cur);

	gattlib_register_notification(connection, notification_handler, (void*)_slave);
	ret = gattlib_notification_start(connection, &_slave->uuid_noti);
	if (ret) {
		fprintf(stderr, "Fail to start notification.\n");
		slave_disconnect(_slave);
		return 0;
	}
	ret = 1;
	
	return ret;
}

int slave_find(const char *_device_str)
{
	for(int i=0; i<g_connection_cnt; i++)
	{
		if(strcmp(_device_str, g_connections[i].device_str)==0)
			return i;
	}
	return -1;
}

int slave_add(const char *_device_str, STIIOT_Slave *_slave)
{
	int ret = 1;
	if(slave_find(_device_str) >= 0)
	{
		fprintf(stderr, "%s is in list.", _device_str);
		return 0;
	}

	if(g_connection_cnt >= MAX_SLAVE)
	{
		fprintf(stderr, "fail to add slave.(over max limit %d)\n", MAX_SLAVE);
		return 0;
	}

	strcpy(_slave->device_str, _device_str);

	ret = slave_idle(_slave, 0);

	//mark_
	
	return ret;
}

int g_sockfd, g_portno = 1337;
struct sockaddr_in g_serv_addr;
struct hostent *g_server;

int socket_connect()
{
    //g_portno = 1337;//atoi(argv[2]);
    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0) 
	{
        fprintf(stderr,"ERROR opening socket");
		return 0;
	}
    g_server = gethostbyname("127.0.0.1");
    if (g_server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        return 0;
    }
    bzero((char *) &g_serv_addr, sizeof(g_serv_addr));
    g_serv_addr.sin_family = AF_INET;
    bcopy((char *)g_server->h_addr, (char *)&g_serv_addr.sin_addr.s_addr, g_server->h_length);
    g_serv_addr.sin_port = htons(g_portno);
    if (connect(g_sockfd,(struct sockaddr *) &g_serv_addr,sizeof(g_serv_addr)) < 0) 
	{
        fprintf(stderr,"ERROR connecting");
		return 0;
	}
	return 1;
}

int socket_idle(const char *_send_data)
{
	if(g_sockfd < 0)
		return 0;

	int n;
    n = write(g_sockfd,_send_data,strlen(_send_data));
    if (n < 0) 
         fprintf(stderr,"ERROR writing to socket\n");
    char buffer[1024];
	n = read(g_sockfd,buffer,1024);
    if (n < 0) 
	{
         fprintf(stderr,"ERROR reading from socket");
		 return 0;
	}

	if(n == 0)
		return 1;

	char device_str[255];
	float holding_time;

	n = sscanf(buffer, "%s %f", device_str, &holding_time);
	if(n != 2)
	{
		fprintf(stderr, "Fail to parse packet %s.", buffer);
		return 0;
	}
	STIIOT_Slave *slave = &g_connections[g_connection_cnt];
	unsigned int holding_msec = (int)(holding_time * 1000);
	slave->holding_time = holding_msec;
	int ret = slave_add(device_str, slave);
	if(ret != 0)
		g_connection_cnt++;
	return ret;
}

int socket_disconnect()
{
    close(g_sockfd);
	return 1;
}

static void on_user_abort(int arg) {
	g_main_loop_quit(m_main_loop);
}

static void usage(char *argv[]) {
	printf("%s <device_address>\n", argv[0]);
}

gboolean master_idle(gpointer _data)
{
	unsigned int _time_cur = timeGetTime();

	if(slave_on_count(_time_cur) >= 5) // devices what keep connection are over than 5, wait for next turn.
		return TRUE;

	for(int i=0; i<g_connection_cnt; i++)
	{
		STIIOT_Slave *slave = &g_connections[i];

		if(slave->last_update_time + slave->holding_time < _time_cur)
		{
			slave_idle(slave, _time_cur);
		}
	}
	//return 1;
	return TRUE;
}

int main(int argc, char *argv[]) {
	int ret=1;
	
	//mark_
	// Catch CTRL-C
	signal(SIGINT, on_user_abort);
	m_main_loop = g_main_loop_new(NULL, 0);
	
	slave_reset();

	if(slave_add("D1:A6:5A:2C:B0:36", &g_connections[g_connection_cnt]))
	{
		g_connection_cnt++;
		slave_add("D1:A6:5A:2C:B0:36", &g_connections[g_connection_cnt]);
		g_connection_cnt++;
	}
	else{
		printf("fail to make default connection.\n");
		return 0;
	}
	g_idle_add(master_idle, NULL);
	g_main_loop_run(m_main_loop);
	
	// In case we quit the main loop, clean the connection
	g_main_loop_unref(m_main_loop);

	for(int i=0; i<g_connection_cnt; i++)
		slave_disconnect(&g_connections[i]);
		
	puts("Done");
	return ret;
}
