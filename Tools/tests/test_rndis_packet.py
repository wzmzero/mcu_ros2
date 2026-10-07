"""Run the imported RNDIS packet parser with host mocks and memory sanitizers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TINY = ROOT / 'stm32f4_ros2/Middlewares/Third_Party/TinyUSB'
source = (TINY / 'src/class/net/ecm_rndis_device.c').read_text()
parser = 'static void handle_incoming_packet' + source.split(
    'static void handle_incoming_packet', 1)[1].split('\nbool netd_xfer_cb', 1)[0]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rndis_protocol.h"
#define CFG_TUD_NET_MTU 1514
static struct { _Alignas(4) uint8_t rx[2048]; } _netd_epbuf;
static struct { bool ecm_mode; } _netd_itf;
static uint16_t received;
static bool tud_network_recv_cb(const uint8_t *data, uint16_t size) {
  assert(size <= CFG_TUD_NET_MTU);
  assert(data >= _netd_epbuf.rx && data <= _netd_epbuf.rx + sizeof(_netd_epbuf.rx) - size);
  volatile uint8_t byte = 0;
  for (uint16_t i = 0; i < size; ++i) byte ^= data[i];
  (void)byte;
  received = size;
  return true;
}
static void tud_network_recv_renew(void) {}
'''
suffix = r'''
static rndis_data_packet_t *init(void) {
  memset(&_netd_epbuf, 0, sizeof(_netd_epbuf));
  rndis_data_packet_t *r = (void *)_netd_epbuf.rx;
  r->MessageType = REMOTE_NDIS_PACKET_MSG;
  r->MessageLength = sizeof(*r) + 14;
  r->DataOffset = sizeof(*r) - offsetof(rndis_data_packet_t, DataOffset);
  r->DataLength = 14;
  return r;
}
int main(void) {
  rndis_data_packet_t *r = init();
  handle_incoming_packet(r->MessageLength); assert(received == 14);
  handle_incoming_packet(r->MessageLength + 16); assert(received == 14);
  for (uint32_t len = 0; len < sizeof(*r); ++len) {
    handle_incoming_packet(len); assert(received == 0);
  }
  r = init(); r->DataOffset = 0; handle_incoming_packet(58); assert(received == 0);
  r = init(); r->DataOffset = UINT32_MAX - 7; handle_incoming_packet(58); assert(received == 0);
  r = init(); r->DataLength = UINT32_MAX; handle_incoming_packet(58); assert(received == 0);
  r = init(); r->MessageLength = 43; handle_incoming_packet(58); assert(received == 0);
  r = init(); r->DataLength = 15; handle_incoming_packet(58); assert(received == 0);
  r = init(); r->DataLength = 1515; r->MessageLength = 1559;
  handle_incoming_packet(1559); assert(received == 0);
  r = init(); r->DataLength = 1514; r->MessageLength = 1558;
  handle_incoming_packet(1558); assert(received == 1514);
  uint32_t random = 42;
  for (unsigned i = 0; i < 100000; ++i) {
    r = init();
    random = random * 1664525u + 1013904223u; r->DataOffset = random;
    random = random * 1664525u + 1013904223u; r->DataLength = random;
    random = random * 1664525u + 1013904223u; r->MessageLength = random % 2049;
    handle_incoming_packet(2048); assert(received == 0);
  }
  puts("RNDIS packet boundaries and random headers passed");
}
'''
with tempfile.TemporaryDirectory(prefix='stm32-rndis-test-') as directory:
    folder = Path(directory)
    path = folder / 'test.c'
    path.write_text(prefix + parser + suffix)
    binary = folder / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I', str(TINY / 'lib/networking'), str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
