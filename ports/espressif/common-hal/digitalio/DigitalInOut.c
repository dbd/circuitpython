// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2017-2020 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "shared-bindings/digitalio/DigitalInOut.h"
#include "py/runtime.h"

#include "driver/gpio.h"
#include "hal/gpio_hal.h"
#include "esp_idf_version.h"

static bool _pin_is_input(uint8_t pin_number) {
    gpio_io_config_t io;
    if (gpio_get_io_config((gpio_num_t)pin_number, &io) != ESP_OK) {
        return false;
    }
    // 'ie' == input enable flag
    return io.ie;
}

void digitalio_digitalinout_preserve_for_deep_sleep(size_t n_dios, digitalio_digitalinout_obj_t *preserve_dios[]) {
    // Mark the pin states of the given DigitalInOuts for preservation during deep sleep
    for (size_t i = 0; i < n_dios; i++) {
        if (!common_hal_digitalio_digitalinout_deinited(preserve_dios[i])) {
            preserve_pin_number(preserve_dios[i]->pin->number);
        }
    }
}

void common_hal_digitalio_digitalinout_never_reset(
    digitalio_digitalinout_obj_t *self) {
    never_reset_pin_number(self->pin->number);
}

digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self, const mcu_pin_obj_t *pin) {

    claim_pin(pin);
    self->pin = pin;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin->number),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        return DIGITALINOUT_PIN_BUSY;
    }

    // Initialize cached flags/values that DO exist in your struct.
    self->output_value = false;  // matches “low” if/when configured as output

    // RISC-V family (no stable register getter for open-drain): start as push-pull.
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || \
    defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32C61)
    self->is_open_drain_cached = false;
#endif

    return DIGITALINOUT_OK;
}

bool common_hal_digitalio_digitalinout_deinited(digitalio_digitalinout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_digitalio_digitalinout_deinit(digitalio_digitalinout_obj_t *self) {
    if (common_hal_digitalio_digitalinout_deinited(self)) {
        return;
    }

    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_input(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    common_hal_digitalio_digitalinout_set_pull(self, pull);
    gpio_set_direction(self->pin->number, GPIO_MODE_INPUT);
    return DIGITALINOUT_OK;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_output(
    digitalio_digitalinout_obj_t *self, bool value,
    digitalio_drive_mode_t drive_mode) {
    common_hal_digitalio_digitalinout_set_value(self, value);
    return common_hal_digitalio_digitalinout_set_drive_mode(self, drive_mode);
}

digitalio_direction_t common_hal_digitalio_digitalinout_get_direction(
    digitalio_digitalinout_obj_t *self) {
    if (_pin_is_input(self->pin->number)) {
        return DIRECTION_INPUT;
    }
    return DIRECTION_OUTPUT;
}

void common_hal_digitalio_digitalinout_set_value(
    digitalio_digitalinout_obj_t *self, bool value) {
    self->output_value = value;
    gpio_set_level(self->pin->number, value);
}

bool common_hal_digitalio_digitalinout_get_value(
    digitalio_digitalinout_obj_t *self) {
    if (common_hal_digitalio_digitalinout_get_direction(self) == DIRECTION_INPUT) {
        return gpio_get_level(self->pin->number) == 1;
    }
    return self->output_value;
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_drive_mode(
    digitalio_digitalinout_obj_t *self,
    digitalio_drive_mode_t drive_mode) {

    const gpio_num_t gpio = (gpio_num_t)self->pin->number;

    esp_err_t err;
    if (drive_mode == DRIVE_MODE_OPEN_DRAIN) {
        // Open-drain expects an external pull-up; float internals.
        (void)gpio_set_pull_mode(gpio, GPIO_FLOATING);
        err = gpio_set_direction(gpio, GPIO_MODE_OUTPUT_OD);
        if (err != ESP_OK) {
            return DIGITALINOUT_INPUT_ONLY;
        }
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || \
    defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32C61)
        self->is_open_drain_cached = true;
#endif
    } else {
        // Push-pull
        err = gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
        if (err != ESP_OK) {
            return DIGITALINOUT_INPUT_ONLY;
        }
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || \
    defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32C61)
        self->is_open_drain_cached = false;
#endif
    }

    return DIGITALINOUT_OK;
}

digitalio_drive_mode_t common_hal_digitalio_digitalinout_get_drive_mode(
    digitalio_digitalinout_obj_t *self) {

    /* On IDF ≥ 5.3 (your 5.4.1) and on RISC-V chips, don’t poke legacy
     * gpio_dev_t.pin[].pad_driver (it’s gone). Use our cached value. */
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || \
    defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32C61) || \
    (ESP_IDF_VERSION_MAJOR > 5) || \
    (ESP_IDF_VERSION_MAJOR == 5 && ESP_IDF_VERSION_MINOR >= 3)
    #if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || \
        defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32C61)
    return self->is_open_drain_cached ? DRIVE_MODE_OPEN_DRAIN : DRIVE_MODE_PUSH_PULL;
    #else
    /* Newer IDF on Xtensa but no cache: default to push-pull. */
    return DRIVE_MODE_PUSH_PULL;
    #endif

#else
    /* Older IDF where .pad_driver exists (classic ESP32/S2/S3 builds). */
    if (GPIO_HAL_GET_HW(GPIO_PORT_0)->pin[self->pin->number].pad_driver == 1) {
        return DRIVE_MODE_OPEN_DRAIN;
    }
    return DRIVE_MODE_PUSH_PULL;
#endif
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_pull(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    gpio_num_t number = self->pin->number;
    gpio_pullup_dis(number);
    gpio_pulldown_dis(number);
    if (pull == PULL_UP) {
        gpio_pullup_en(number);
    } else if (pull == PULL_DOWN) {
        gpio_pulldown_en(number);
    }
    return DIGITALINOUT_OK;
}


digitalio_pull_t common_hal_digitalio_digitalinout_get_pull(
    digitalio_digitalinout_obj_t *self) {

    gpio_io_config_t io;
    if (gpio_get_io_config((gpio_num_t)self->pin->number, &io) != ESP_OK) {
        return PULL_NONE;
    }

    // io.pu = pull-up enabled, io.pd = pull-down enabled
    if (io.pu) return PULL_UP;
    if (io.pd) return PULL_DOWN;
    return PULL_NONE;
}
