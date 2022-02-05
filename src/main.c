/*
 * Copyright (c) 2020 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Psuedo Code: 
 * &1. Startup 
 * &2. Enable ESP32 as a ble peripheral 
 * &3. Esp32 send an advertising packet 1x per minute containing device serial #, 
 *    waits 500ms for a response. 
 *    baswi: only one adv packet gives small chance for connection; I send adv packets with default inverval of Zephyr (100msec)
 * 			 during 500 ms (2 sec for testing purposes)   
 * If no response is found esp32 goes to light sleep with ble modem off to conserve power (battery-based application) 
 * 4. If a connection is available the esp32 will connect and send a 2kb json packet (broken down into chunks 
 * 5. Receiving python code end needs to re-assemble data do form original packet Deliverables Working Python and ESP-IDF Code (no android)
 */

#include <zephyr.h>
#include <sys/printk.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/services/bas.h>
//#include <bluetooth/services/hrs.h>

bool baswi_connected = false;
int err; //used in different functions

K_SEM_DEFINE(sem_wakeup, 0, 1);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_DIS_VAL))
};

static void bt_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
	} else {
		printk("*** Something connected to this peripheral; pairing not necessary?@@@\n");
		baswi_connected = true;
	}
}

static void bt_disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("*** The connection was disconnected (reason 0x%02x)\n", reason);
}


//Register a callback structure for connection events. 
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = bt_connected,
	.disconnected = bt_disconnected,  //supervision timeout is reason 8.
	//.security_changed = security_changed,
	//.identity_resolved = identity_resolved,
	
	//@@@bt_gatt_notify_cb  is called when the packet has been ACKed on the link layer

	//@@@ I can send data to the BLE Client by calling the function bt_gatt_notify_cb (sends notify packets)
	//modified the MTU in order to be able to send more than the 23 Bytes
	// loop that calls the bt_gatt_notify_cb function as many times as data sets are to be sent.
	//answer: Yes, you can do that. It could make sense to wait for the callback before you send more in this case.
	//
	//whether notifications or indications should be used. 
	//The only reason to use indications is if you need confirmation in the application layer that the indication has been received. 
	//This is only useful in some very specific cases.
	//Notifications "are still guaranteed to reach the target because of the link layer ACK
	//Yes, the link layer (which is either the Zephyr link layer or the SoftDevice controller) keeps track of transmitted packets and if they are acknowledged or not.
	//If retransmissions keep failing so that there are no packets exchanged during a supervision timeout (which is a basic connection parameter), the link will be closed due to supervision timeout. 
	//In that case the application will be notified that a supervision timeout has occurred.
	//No. You cannot send notifications (or indications) without a connection, and even if connected, the client needs to have enabled notifications/indications. 
	//It is correct and appropriate that the bt_gatt_notify_cb() function returns an error in this case.
	// bt_gatt_notify_cb  is called when the packet has been ACKed on the link layer. Any retransmissions etc happen in the link layer without the host knowing about it.

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

static void bas_notify(void)
{
	uint8_t battery_level = bt_bas_get_battery_level(); 

	battery_level--;

	if (!battery_level) {
		battery_level = 100U;
	}

	bt_bas_set_battery_level(battery_level);
}

//baswi added
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

/* Heart Rate Service Declaration */
BT_GATT_SERVICE_DEFINE(hrs_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_HRS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HRS_MEASUREMENT, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	//BT_GATT_CCC(hrmc_ccc_cfg_changed,       //baswi; called when real HR central connects
	BT_GATT_CCC(NULL,       //baswi; called when real HR central connects
		    HRS_GATT_PERM_DEFAULT),
	BT_GATT_CHARACTERISTIC(BT_UUID_HRS_BODY_SENSOR, BT_GATT_CHRC_READ,
			       HRS_GATT_PERM_DEFAULT & GATT_PERM_READ_MASK,
			       NULL, NULL, NULL), //read_blsc://baswi: Read attribute value from local database storing the result into buffer
	BT_GATT_CHARACTERISTIC(BT_UUID_HRS_CONTROL_POINT, BT_GATT_CHRC_WRITE,
			       HRS_GATT_PERM_DEFAULT & GATT_PERM_WRITE_MASK,
			       NULL, NULL, NULL),
);


static void hrs_notify(void)
{
	static uint8_t heartrate = 90U;

	/* Heartrate measurements simulation */
	heartrate++;
	if (heartrate == 160U) {
		heartrate = 90U;
	}

//	bt_hrs_notify(heartrate); //baswi
	int rc;
	static uint8_t hrm[3];

	hrm[0] = 0x01; //testing
	hrm[1] = 0x02;
    hrm[2] = 0x03;

	rc = bt_gatt_notify(NULL, &hrs_svc.attrs[1], &hrm, sizeof(hrm));

//baswi	return rc == -ENOTCONN ? 0 : rc;

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
		printk("TRHEAD1: sleep for 30 seconds\n");
		k_sleep(K_SECONDS(1)); 
		//k_sleep(K_MSEC(500));
	}
}



void main(void)
{
	priv_bt_enable();
	printk("*** Register authentication call backs\n");
	bt_conn_auth_cb_register(&auth_cb_display);

	while (!baswi_connected) {
		//wait till sem available->send advertisement during x seconds
		if  (k_sem_take(&sem_wakeup, K_FOREVER) == 0) { //probably sleep iso sem was also OK to guarantee sleeping
			//priv_bt_enable(); //cannot enable bt, because it is never disabled
			priv_bt_le_adv_start();
			k_sleep(K_SECONDS(10));
			priv_bt_le_adv_stop();
		};
		printk("busy wait indicator\n");
	}
	//POST: baswi_connected == true
	printk("STATUS = connected\n");


	/* Implement notification. At the moment there is no suitable way
	 * of starting delayed work so we do it here
	 */
	while (1) {
		k_sleep(K_SECONDS(1));

		/* Heartrate measurements simulation */
		hrs_notify(); //baswi: if something other than central_hr is connected, no updates are sent

		/* Battery level simulation */
		bas_notify();
	}


}	

K_THREAD_DEFINE(thread1, STACKSIZE, thread1_entry, NULL, NULL, NULL,
		7, K_USER, 0);
