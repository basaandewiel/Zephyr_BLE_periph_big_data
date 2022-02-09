# Zephyr_BT_periph
Bluetooth peripheral (server side) - Zephyr

# What is does
It sends periodic bluetooth advertisements; when nothing is connected within x seconds, advertising stops and device goes into sleep.

When another device connects, it starts sending (test)data every second. This test data is > MTU size, so it is sent in chunks. The size of the chunks
depends on the negotiated MTU.

For the receiving side to be able to reassemble the data sent, extra data is inserted: MoreDataFollow flag; currenty implemented as one byte.
This MoreDataFollow flag is comparable with the MoreFragment flag in IPv4.


When the other device disconnects, it starts sending periodic advertisements again.
