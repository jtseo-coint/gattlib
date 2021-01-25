

class iot_item{
public:
	iot_item();
	~iot_item();
	
	GOptionContext *context;
	GOptionGroup *gatt_group, *params_group, *char_rw_group;
	GError *gerr = NULL;
	gatt_connection_t *connection;
	unsigned long conn_options = 0;
	BtIOSecLevel sec_level;
	uint8_t dest_type;

};


class iot_manager
{

};