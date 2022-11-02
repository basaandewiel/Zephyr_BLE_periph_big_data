![Build qemu_x86](https://github.com/basaandewiel/Zephyr_BLE_periph_big_data/actions/workflows/InstallZephyrSDKBuildTest.yml.yml/badge.svg)


# Zephyr_BLE_periph_big_data
Bluetooth peripheral (server side) - Zephyr - sends big amount of data splitted into chunks
This application can be used together with
* https://github.com/basaandewiel/Zephyr_BLE_central_big_data, or
* https://github.com/basaandewiel/Python_BLE_central_big_data

# What is does
The peripheral sends BLE advertising packets during a certain amount of time every 100msec (default inverval of Zephyr).
* When there is BLE device connected after this period of sending advertisements, the peripheral goes into sleep mode for a certain period of time to conserve power.
* When the sleep period is over, it starts again sending BLE advertisements during a certain amount of time.
 * etc. etc.

When another BLE device connects, it starts sending (test)data every second. The size of this test data is > MTU size, so it has to be split into chunks. The size of the chunks depends on the negotiated MTU.

For the receiving side to be able to reassemble the data sent, extra data is inserted: 
* <ChunkNbr> number of the Chunk starting at 0 (size is one byte)
* <MoreDataFollows> has value of '1' if more data (chunks) follow, otherwise is '0' (size is also 1 byte)
  
So two extra bytes are inserted before the payload in each chunk sent.
The <MoreDataFollows> is comparable with the MoreFragment flag in IPv4.

When the other device disconnects, it starts sending periodic advertisements again.
  

# Building and running
For how to build and run, see https://basaandewiel.github.io/Zephyr_bluetooth_programming/
