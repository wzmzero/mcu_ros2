# Local patches

`src/class/net/ecm_rndis_device.c`: RNDIS receive validation checks the minimum
message size, payload offset, length within MessageLength and Ethernet MTU using
subtraction before pointer arithmetic. Malformed offsets cannot wrap or point
into the RNDIS header. Valid packets and permitted USB padding remain supported.

`UPSTREAM.json` records the original imported hashes; this file is intentionally
different. `Tools/tests/test_rndis_packet.py` runs the actual parser function in a
host harness, including malformed boundaries and random headers under sanitizers.

`src/portable/synopsys/dwc2/dcd_dwc2.c`: restore device B-session validity override
when VBUS sensing is disabled and the STM32 GCCFG configuration call. The imported
ESP32 copy had replaced these with a "No overrides" block. STM32F407 without PA9
VBUS sensing needs NOVBUSSENS and a valid B session before enumeration. This follows
the [upstream initialization](https://github.com/hathach/tinyusb/blob/master/src/portable/synopsys/dwc2/dcd_dwc2.c).
The original imported hash remains in UPSTREAM.json. ESP32's separate copy is unchanged.
