#include "tusb.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

uint8_t tud_network_mac_address[6];
static char serial[25];
static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = 64, .idVendor = 0x0483, .idProduct = 0x5741,
    .bcdDevice = 0x0200, .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3,
    .bNumConfigurations = 1,
};
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN, 0, 100),
    TUD_RNDIS_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
};
static const uint8_t compatible_id[] = {
    40,0,0,0, 0,1, 4,0, 1,0,0,0,0,0,0,0,
    0,1, 'R','N','D','I','S',0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,
};
void usb_descriptors_init(void)
{
    uint32_t a = HAL_GetUIDw0(), b = HAL_GetUIDw1(), c = HAL_GetUIDw2();
    snprintf(serial, sizeof(serial), "%08lX%08lX%08lX", (unsigned long)a,
             (unsigned long)b, (unsigned long)c);
    uint32_t hash = a ^ b ^ c;
    tud_network_mac_address[0] = 2; tud_network_mac_address[1] = 0xF4;
    for (unsigned i = 0; i < 4; ++i) tud_network_mac_address[i + 2] = hash >> (8 * i);
    /* This is the host NIC MAC. The MCU netif toggles the last byte. */
}
const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{ (void)index; return configuration; }
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language)
{
    (void)language;
    static const uint16_t os[] = {0x0312, 'M','S','F','T','1','0','0',1};
    static uint16_t value[33];
    static const char *strings[] = {NULL, "STM32", "STM32F407 micro-ROS RNDIS", serial, "RNDIS"};
    if (index == 0xEE) return os;
    if (!index) { value[0] = 0x0304; value[1] = 0x0409; return value; }
    if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
    size_t n = strlen(strings[index]);
    if (n > 32) n = 32;
    value[0] = (uint16_t)(0x0300 | (2 * n + 2));
    for (size_t i = 0; i < n; ++i) value[i + 1] = (uint8_t)strings[index][i];
    return value;
}
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    if (request->bRequest != 1 || request->wIndex != 4 ||
        request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR || !request->bmRequestType_bit.direction)
        return false;
    return stage != CONTROL_STAGE_SETUP ||
        tud_control_xfer(rhport, request, (void *)compatible_id, sizeof(compatible_id));
}
