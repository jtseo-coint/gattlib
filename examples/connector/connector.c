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

#include <glib.h>

#include "iot_slave.h"
#include "gattlib.h"

// Battery Level UUID
const uuid_t g_battery_level_uuid = CREATE_UUID16(0x2A19);

static GMainLoop *m_main_loop;

typedef struct __iiot_slave__{
	
} STIIOT_Slave;

void notification_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
	
	char uuid_str[255];

	gattlib_uuid_to_string(uuid, uuid_str, sizeof(uuid_str));

	printf("uuid:%s\n", uuid_str);

	strncpy(uuid_str, (char*)data, data_length);
	uuid_str[data_length] = 0;
	printf("Notification Handler: %s %d\n", uuid_str, data_length);
	
}

static void on_user_abort(int arg) {
	g_main_loop_quit(m_main_loop);
}

static void usage(char *argv[]) {
	printf("%s <device_address>\n", argv[0]);
}


int main(int argc, char *argv[]) {
	int ret;
	gatt_connection_t* connection;

	iot_slave slave();
	slave.world();

	if (argc != 2) {
		usage(argv);
		return 1;
	}

	connection = gattlib_connect(NULL, argv[1], GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT);
	if (connection == NULL) {
		fprintf(stderr, "Fail to connect to the bluetooth device.\n");
		return 1;
	}
	printf("connect\n");

	uuid_t uuid, uuid_noti;
	const char *uuid_sn = "2a25";
	if (gattlib_string_to_uuid(uuid_sn, strlen(uuid_sn) + 1, &uuid) < 0) {
		usage(argv);
		return 1;
	}

	size_t len;
	char *buffer = NULL;
	ret = gattlib_read_char_by_uuid(connection, &uuid, (void **)&buffer, &len);

	if(ret == GATTLIB_SUCCESS)
	{
		printf("rec:%s, %d\n",buffer, len);
	}

	const char *uuid_read = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

	if (gattlib_string_to_uuid(uuid_read, strlen(uuid_read) + 1, &uuid) < 0) {
		usage(argv);
		return 1;
	}

	const char *uuid_noti_str = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

	if (gattlib_string_to_uuid(uuid_noti_str, strlen(uuid_noti_str) + 1, &uuid_noti) < 0) {
		usage(argv);
		return 1;
	}

	ret = gattlib_write_char_by_uuid(connection, &uuid, "T", 2);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Fail to write.\n");
	}
	
	gattlib_register_notification(connection, notification_handler, NULL);

	ret = gattlib_notification_start(connection, &uuid_noti);
	if (ret) {
		fprintf(stderr, "Fail to start notification.\n");
		goto DISCONNECT;
	}

	// Catch CTRL-C
	signal(SIGINT, on_user_abort);

	m_main_loop = g_main_loop_new(NULL, 0);
	g_main_loop_run(m_main_loop);

	// In case we quit the main loop, clean the connection
	gattlib_notification_stop(connection, &uuid_noti);
	g_main_loop_unref(m_main_loop);

DISCONNECT:
	gattlib_disconnect(connection);
	puts("Done");
	return ret;
}
