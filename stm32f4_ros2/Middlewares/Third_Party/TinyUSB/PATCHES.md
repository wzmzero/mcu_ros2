# Local patch

`src/class/net/ecm_rndis_device.c`: RNDIS receive validation checks the minimum
message size, payload offset, length within MessageLength and Ethernet MTU using
subtraction before pointer arithmetic. Malformed offsets cannot wrap or point
into the RNDIS header. Valid packets and permitted USB padding remain supported.

`UPSTREAM.json` records the original imported hashes; this file is intentionally
different. `Tools/tests/test_rndis_packet.py` runs the actual parser function in a
host harness, including malformed boundaries and random headers under sanitizers.
