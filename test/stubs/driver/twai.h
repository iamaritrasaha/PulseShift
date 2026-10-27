#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "gpio.h"
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_ERR_TIMEOUT=263, ESP_FAIL=-1;
inline const char* esp_err_to_name(int e) { return e==ESP_OK?"ESP_OK":e==ESP_ERR_TIMEOUT?"ESP_ERR_TIMEOUT":"ESP_FAIL"; }
enum twai_mode_t { TWAI_MODE_NORMAL, TWAI_MODE_NO_ACK, TWAI_MODE_LISTEN_ONLY };
enum twai_state_t { TWAI_STATE_STOPPED, TWAI_STATE_RUNNING, TWAI_STATE_BUS_OFF };
constexpr uint32_t TWAI_ALERT_BUS_ERROR=1, TWAI_ALERT_BUS_OFF=2, TWAI_ALERT_RX_QUEUE_FULL=4,
 TWAI_ALERT_RX_FIFO_OVERRUN=8, TWAI_ALERT_RX_DATA=16, TWAI_ALERT_TX_SUCCESS=32,
 TWAI_ALERT_TX_FAILED=64, TWAI_ALERT_TX_IDLE=128;
struct twai_general_config_t { gpio_num_t tx_io,rx_io; twai_mode_t mode; unsigned tx_queue_len=5,rx_queue_len=5; uint32_t alerts_enabled=0; };
#define TWAI_GENERAL_CONFIG_DEFAULT(tx,rx,mode) twai_general_config_t{tx,rx,mode}
struct twai_timing_config_t { uint32_t bitrate; };
#define TWAI_TIMING_CONFIG_125KBITS() twai_timing_config_t{125000}
#define TWAI_TIMING_CONFIG_250KBITS() twai_timing_config_t{250000}
#define TWAI_TIMING_CONFIG_500KBITS() twai_timing_config_t{500000}
#define TWAI_TIMING_CONFIG_1MBITS() twai_timing_config_t{1000000}
struct twai_filter_config_t { int accept; };
#define TWAI_FILTER_CONFIG_ACCEPT_ALL() twai_filter_config_t{1}
struct twai_message_t { uint32_t identifier=0; uint8_t data_length_code=0; bool extd=false,rtr=false,self=false,ss=false; uint8_t data[8]{}; };
struct twai_status_info_t {
 twai_state_t state=TWAI_STATE_STOPPED; uint32_t msgs_to_tx=0,msgs_to_rx=0,
 tx_error_counter=0,rx_error_counter=0,tx_failed_count=0,bus_error_count=0,
 rx_missed_count=0,rx_overrun_count=0,arb_lost_count=0;
};
int twai_driver_install(const twai_general_config_t*,const twai_timing_config_t*,const twai_filter_config_t*);
int twai_start();
int twai_stop();
int twai_read_alerts(uint32_t*,uint32_t);
int twai_get_status_info(twai_status_info_t*);
int twai_receive(twai_message_t*,uint32_t);
// Deliberately no implementation in receive-only test binaries. Any call fails linkage.
int twai_transmit(const twai_message_t*,uint32_t);
