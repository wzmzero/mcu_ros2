#include "serial_transport.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "driver/usb_serial_jtag.h"
#include "driver/uart.h"
#include <limits.h>

#if !CONFIG_ESP_CONSOLE_NONE || !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#error "Disable consoles so printf/log output cannot enter the micro-ROS stream."
#endif

esp_err_t serial_transport_init(void)
{
#if CONFIG_APP_ROS_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 2048, .rx_buffer_size = 4096
    };
    return usb_serial_jtag_driver_install(&config);
#else
    uart_config_t config = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT
    };
    esp_err_t result = uart_param_config(UART_NUM_1, &config);
    if (result != ESP_OK) return result;
    result = uart_set_pin(UART_NUM_1, CONFIG_APP_ROS_UART_TX, CONFIG_APP_ROS_UART_RX,
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (result != ESP_OK) return result;
    return uart_driver_install(UART_NUM_1, 4096, 2048, 0, NULL, 0);
#endif
}

bool micro_ros_transport_open(struct uxrCustomTransport *transport)
{
    (void)transport;
    /* RMW opens/closes the port while pinging. Keep the driver's lifetime
       tied to the pinned ROS task, and discard stale bytes at each open. */
#if CONFIG_APP_ROS_USB_SERIAL_JTAG
    uint8_t discard[128];
    while (usb_serial_jtag_read_bytes(discard, sizeof(discard), 0) > 0) {}
    return true;
#else
    return uart_flush_input(UART_NUM_1) == ESP_OK;
#endif
}

bool micro_ros_transport_close(struct uxrCustomTransport *transport)
{
    (void)transport;
    return true;
}

size_t micro_ros_transport_write(struct uxrCustomTransport *transport,
                             const uint8_t *buffer, size_t length, uint8_t *error)
{
    (void)transport;
    if (length > INT_MAX) { *error = 1; return 0; }
#if CONFIG_APP_ROS_USB_SERIAL_JTAG
    int count = usb_serial_jtag_write_bytes(buffer, length, pdMS_TO_TICKS(200));
#else
    /* Buffered TX copies the caller's data before returning. */
    int count = uart_write_bytes(UART_NUM_1, buffer, length);
#endif
    *error = count < 0 || (size_t)count != length;
    return count > 0 ? (size_t)count : 0;
}

size_t micro_ros_transport_read(struct uxrCustomTransport *transport,
                            uint8_t *buffer, size_t length, int timeout_ms, uint8_t *error)
{
    (void)transport;
    if (length > INT_MAX) { *error = 1; return 0; }
    TickType_t ticks = timeout_ms > 0 ? pdMS_TO_TICKS(timeout_ms) : 0;
#if CONFIG_APP_ROS_USB_SERIAL_JTAG
    int count = usb_serial_jtag_read_bytes(buffer, length, ticks);
#else
    int count = uart_read_bytes(UART_NUM_1, buffer, length, ticks);
#endif
    *error = count < 0;
    return count > 0 ? (size_t)count : 0;
}
