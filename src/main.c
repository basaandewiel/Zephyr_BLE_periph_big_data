/*
 * Peripheral sends advertising packets during certain amount of time every 100msec (defautl inverval of Zephyr).
 * When there is still no connection after this period of sending advertisement, the device goes into sleep mode 
 * to conserve power for a certain period of time.
 * When the sleep period is over, it starts again sending advertisements during certain amount of time.
 * etc. etc.
 *
 * If a connection is available the device will send big data packet via BLE. This data packet is splitted into chunks
 * because it is bigger than the MTU. Extra information is added so that the receiving side is able to reassemble the chunks.
 *
 * The central part that is reassembling the chunks is also available as a Python script.
 */

//#include <sys/printk.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
//#include <bluetooth/services/bas.h>
//#include <bluetooth/services/hrs.h>

bool periphConnected = false;
int err; //used in different functions
uint16_t CurrentMTUrx, CurrentMTUtx; //used to save current MTU

K_SEM_DEFINE(sem_wakeup, 0, 1); //semaphore to time sending of BLE advertisements

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_DIS_VAL))
};

//call back called when MTU is updated
void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	printk("Updated MTU: TX: %d RX: %d bytes\n", tx, rx);
	CurrentMTUrx = rx;
	CurrentMTUtx = tx;
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = mtu_updated
};


static void bt_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
	} else {
		printk("*** Something connected to this peripheral\n");
		periphConnected = true;
	}
}

static void bt_disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("*** The connection was disconnected (reason 0x%02x)\n", reason);
	periphConnected = false;
}

static bool le_param_requested(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	printk("***LE connection parameter update request*** NOT HANDLED");
	return false; //not handled
}

//Register a callback structure for connection events. 
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = bt_connected,
	.disconnected = bt_disconnected,  //supervision timeout is reason 8.
	.le_param_req = le_param_requested, //baswi inserted
	//.security_changed = security_changed,
	//.identity_resolved = identity_resolved,
	
	//bt_gatt_notify_cb  is called when the packet has been ACKed on the link layer - not used
};


static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.cancel = auth_cancel,
};


#define GATT_PERM_READ_MASK     (BT_GATT_PERM_READ | \
				 BT_GATT_PERM_READ_ENCRYPT | \
				 BT_GATT_PERM_READ_AUTHEN)
#define GATT_PERM_WRITE_MASK    (BT_GATT_PERM_WRITE | \
				 BT_GATT_PERM_WRITE_ENCRYPT | \
				 BT_GATT_PERM_WRITE_AUTHEN)

#ifndef CONFIG_BT_HRS_DEFAULT_PERM_RW_AUTHEN
#define CONFIG_BT_HRS_DEFAULT_PERM_RW_AUTHEN 0
#endif
#ifndef CONFIG_BT_HRS_DEFAULT_PERM_RW_ENCRYPT
#define CONFIG_BT_HRS_DEFAULT_PERM_RW_ENCRYPT 0
#endif
#ifndef CONFIG_BT_HRS_DEFAULT_PERM_RW
#define CONFIG_BT_HRS_DEFAULT_PERM_RW 0
#endif

#define HRS_GATT_PERM_DEFAULT (						\
	CONFIG_BT_HRS_DEFAULT_PERM_RW_AUTHEN ?				\
	(BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN) :	\
	CONFIG_BT_HRS_DEFAULT_PERM_RW_ENCRYPT ?				\
	(BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT) :	\
	(BT_GATT_PERM_READ | BT_GATT_PERM_WRITE))			\

static void ccc_changed(void)
{
	printk("***CCC_changed call back function\n");
	k_sleep(K_SECONDS(5));
}

/* Service Declaration */
BT_GATT_SERVICE_DEFINE(hrs_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_HRS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HRS_MEASUREMENT, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(ccc_changed, HRS_GATT_PERM_DEFAULT), //Client Characteristic Configuration; called when central part subcribes
											  //without this macro no notifications are sent by peripheral
											  //1st param: Configuration changed callback
	//https://nl.mathworks.com/help/matlab/import_export/work-with-device-characteristics-and-descriptors.html
);


static void data_notify(void)
{
	int rc;

	char dataToBeSent[] = "12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890";
	char*  datapart = k_malloc(CurrentMTUtx); //allocate memory for datapart
	
	printk("CurrentMTUtx= %d\n", CurrentMTUtx);

	unsigned char ChunkNbr=0; //Sequence number, starts with zero
	unsigned char MoreDataFollows=0; //indicates whether more data chunks follow;
									 //equals to zero for last data chunk, or if data consists of only one chunk
	unsigned char ProtocolOverhead = sizeof(ChunkNbr) + sizeof(MoreDataFollows); //overhead in each chunk
	
	uint16_t nbrBytesPayloadToBeSent = sizeof(dataToBeSent);	//nbr of bytes payload *still* to be sent
	uint16_t nbrBytesPayloadAlreadySent = 0;				//nbr of bytes payload that are already sent

	while (nbrBytesPayloadToBeSent >0) {
		printk("nbrBytesPayloadToBeSent= %d\n", nbrBytesPayloadToBeSent); 
		uint16_t chunkSize = (CurrentMTUtx-4); //4 bytes overhead for L2CAP header
		if ((nbrBytesPayloadToBeSent + ProtocolOverhead) <= chunkSize) { //if <= max chunksize bytes need to be sent
			chunkSize = nbrBytesPayloadToBeSent + ProtocolOverhead;
			MoreDataFollows = 0; //false
		}
		else {
			MoreDataFollows = 1; //true
		}
		printk("chunkSize= %d\n", chunkSize); 
		printk("ChunkNbr= %d\n", ChunkNbr); 

        //Data PDU: <ChunkNbr> <MoreDataFollows> <payload>		
		memcpy(datapart, &ChunkNbr, sizeof(ChunkNbr)); //insert chunkNbr
		ChunkNbr++;
		memcpy(datapart+sizeof(ChunkNbr), &MoreDataFollows, sizeof(MoreDataFollows)); //insert MoreDataFollows flag
		memcpy(datapart+sizeof(ChunkNbr)+sizeof(MoreDataFollows), (dataToBeSent+nbrBytesPayloadAlreadySent), (chunkSize - ProtocolOverhead));   	//copy chunksize data to local data 
		nbrBytesPayloadAlreadySent += (chunkSize - ProtocolOverhead); //substract protocol overhead from chunkSize
		nbrBytesPayloadToBeSent -= (chunkSize - ProtocolOverhead);
		
		rc = bt_gatt_notify(NULL, &hrs_svc.attrs[1], datapart, chunkSize); //can I delete the other attributes?
    	    //1st param=Connection object;if NULL notify all peer that have notification enabled via CCC
			//    						 otherwise do a direct notification only the given connection.
        	//&hrs_svc_attrs[1] – Characteristic or Characteristic Value attribute. [1]=first? attribute??
        	//datapart – Pointer to Attribute data.
       	 	//chunkSize – Attribute value length.
	}
	k_free(datapart);

/* this was try to use att_notify_cb function
    struct bt_gatt_notify_params params = {0};
    const struct bt_gatt_attr *attr = NULL;
	unsigned char bas_data = 0x0d;

    //@@@attr = &hrs_svc.attrs;
 
    bt_gatt_complete_func_t func;
    void *user_data; //Notification Value callback user data

	//params.uuid //Notification Attribute UUID type.
							   // Optional, use to search for an attribute with matching UUID when the attribute object pointer is not known.
    params.attr = attr; //Notification Attribute object.
						//Optional if uuid is provided, in this case it will be used as start range to search for the attribute with the given UUID.
    //params.data = &data->value; //Notification Value data
	params.data = &bas_data; //Notification Value data
    params.len = 1; //Notification Value length
    params.func = NULL; //Notification Value callback@@@

    //if (!conn)
    //{
        //LOG_DBG("Notification send to all connected peers");
        //return bt_gatt_notify_cb(NULL, &params);
		//printk("BEFORE notify_cb\n");
		//bt_gatt_notify_cb(NULL, &params); @@@does NOT WORK - probalby params.attr is wrong
    //}
    //else if (bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY))
    //{
    //    return bt_gatt_notify_cb(conn, &params);
    //}
    //else
    //{
    //    return -EINVAL;
    //}
    //baswi	return rc == -ENOTCONN ? 0 : rc;
*/
}


void priv_bt_enable(void) {
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}
	printk("*** BT enabled\n");
}

void priv_bt_disable(void) {
	/*err = bt_disable(NULL); //function does not exist in Zephyr
	if (err) {
		printk("Bluetooth disabling failed (err %d)\n", err);
		return;
	}
	printk("*** BT disabled\n");*/
}

void priv_bt_le_adv_start(void) {
	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}
	printk("Advertising started\n");
}

void priv_bt_le_adv_stop(void) {
	err = bt_le_adv_stop();
	if (err) {
		printk("Stopping advertising FAILED\n");
		return;
	}
	printk("Advertising stopped\n");
}


#define STACKSIZE 512

static void thread1_entry(void *p1, void *p2, void *p3)
{
	while (1) {
		k_sem_give(&sem_wakeup); //start advertising
		printk("TRHEAD1: sleep for x seconds\n");
		k_sleep(K_SECONDS(5)); 
	}
}
K_THREAD_DEFINE(thread1_id, STACKSIZE, thread1_entry, NULL, NULL, NULL,
		7, K_USER, 0);


void main(void)
{
	priv_bt_enable();
	printk("*** Register authentication call backs\n");

	bt_gatt_cb_register(&gatt_callbacks); 	//needed for mtu_updated call back function
	bt_conn_auth_cb_register(&auth_cb_display);

	//struct bt_gatt_attr *vnd_ind_attr; //baswi added
	//vnd_ind_attr = bt_gatt_find_by_uuid(hrs_svc.attrs, hrs_svc.attr_count, 	//baswi added
	//				    NULL);

	while (1) {
		while (!periphConnected) { //while this peripheral is not connected
			//wait till sem available->send advertisement during x seconds
			if  (k_sem_take(&sem_wakeup, K_FOREVER) == 0) { //sleep iso sem was also OK to guarantee sleeping
				priv_bt_le_adv_start();
				k_sleep(K_SECONDS(2)); //advertise during x seconds
				priv_bt_le_adv_stop();
			};
		}
		printk("STATUS = connected\n"); //POST: periphConnected == true
		k_thread_suspend (thread1_id);  //stop advertising
		k_sleep(K_SECONDS(2)); //give some time for service discovery

		while (periphConnected) {
			k_sleep(K_SECONDS(1));
			data_notify(); //baswi: if something other than central_hr is connected, no updates are sent
		}
		k_thread_resume (thread1_id); //resume advertising
	}	
}	
