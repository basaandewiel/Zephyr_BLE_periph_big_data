# Zephyr_BT_periph_big_data
Bluetooth peripheral (server side) - Zephyr - sends big data splitted into chunks
This application can be used together with
* Zephyr_BT_central_big_data
* Python_BT_central_big_data

# What is does
It sends periodic bluetooth advertisements; when nothing is connected within x seconds, advertising stops and device goes into sleep.

When another device connects, it starts sending (test)data every second. This test data is > MTU size, so it is sent in chunks. The size of the chunks
depends on the negotiated MTU.

For the receiving side to be able to reassemble the data sent, extra data is inserted: 
* <ChunkNbr> number of the Chunk starting at 0 (size is one byte)
* <MoreDataFollows> has value of '1' if more data (chunks) follow, otherwise is '0' (size is also 1 byte)
  
So two extra bytes are inserted before the payload in each chunk sent.
The <MoreDataFollows> is comparable with the MoreFragment flag in IPv4.

When the other device disconnects, it starts sending periodic advertisements again.
  

# Building and running
For how to build and run, see https://basaandewiel.github.io/Zephyr_bluetooth_programming/
