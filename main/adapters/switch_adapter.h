#include "driver/gpio.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"


#define GPIO_OUTPUT_IO_0    15
#define GPIO_OUTPUT_IO_1    16
#define SW_1_GPIO_PIN 12
#define SW_2_GPIO_PIN 13
#define SW_3_GPIO_PIN 16

#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1))
#define SW_PIN_SEL ((1ULL<<SW_1_GPIO_PIN) | (1ULL<<SW_2_GPIO_PIN) | (1ULL<<SW_3_GPIO_PIN))

// HW switch was designed 'ON' at low level, 'OFF' at high level.
#define SW_SET_STATUS(p, s) (gpio_set_level(p, !(s & 0x1)))

enum switch_index {
  SW1 = 0,
  SW2,
  SW3
};

enum switch_type {
    // ON or OFF
    TOGGLING = 0,
    // ON->OFF->ON or OFF->ON->OFF
    LIMIT = 1
};

enum switch_status {
  STA_OFF = 0,
  STA_ON = 1
};

typedef void (*status_update_callback_t)(uint8_t sw_index, bool status);

typedef struct conf {
    uint8_t sw_hold_duration:6; // in seconds
    enum switch_type sw_type:1;
    enum switch_status sw_status:1;
} conf_t;

typedef union switch_conf {
  uint8_t value;
  conf_t conf;
} switch_conf_t;

typedef struct switch_context {
  uint8_t sw_gpio_pin;
  SemaphoreHandle_t  sw_mutex_req;
  TimerHandle_t sw_timer_handler;
  status_update_callback_t status_update_callback;
  switch_conf_t sw_conf;
} switch_context_t;

void switch_adapter_init();
esp_err_t switch_adapter_set_status(uint8_t sw_index, enum switch_status status);
esp_err_t switch_adapter_chg_sta(uint8_t sw_index, bool sw_status);
esp_err_t switch_adapter_get_status(uint8_t sw_index, uint8_t * status);
void switch_adapter_set_state_update_callback(uint8_t sw_index, void * state_update_callback);

