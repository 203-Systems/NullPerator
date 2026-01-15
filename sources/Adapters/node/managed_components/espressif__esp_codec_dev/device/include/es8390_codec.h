/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ES8390_CODEC_H_
#define _ES8390_CODEC_H_

#include "audio_codec_if.h"
#include "audio_codec_ctrl_if.h"
#include "audio_codec_gpio_if.h"
#include "esp_codec_dev_vol.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: esp_codec_dev's I2C control uses an 8-bit I2C address byte (7-bit << 1).
// ES8390 7-bit address range is 0x10-0x13 (AD1:AD0), so the 8-bit range is 0x20-0x26.
#define ES8390_CODEC_DEFAULT_ADDR (0x20)

/**
 * @brief ES8390 codec configuration
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;     /*!< Codec Control interface */
    const audio_codec_gpio_if_t *gpio_if;     /*!< Codec GPIO interface */
    esp_codec_dec_work_mode_t    codec_mode;  /*!< Codec work mode: ADC or DAC */
    int16_t                      pa_pin;      /*!< PA chip power pin */
    bool                         pa_reverted; /*!< false: enable PA when pin set to 1, true: enable PA when pin set to 0 */
    bool                         master_mode; /*!< Whether codec works as I2S master or not */
    bool                         use_mclk;    /*!< Whether use external MCLK clock */
    bool                         digital_mic; /*!< Whether use digital microphone */
    bool                         invert_mclk; /*!< MCLK clock signal inverted or not */
    bool                         invert_sclk; /*!< SCLK clock signal inverted or not */
    esp_codec_dev_hw_gain_t      hw_gain;     /*!< Hardware gain */
    bool                         no_dac_ref;  /*!< When record 2 channel data
                                                   false: right channel filled with dac output
                                                   true: right channel leave empty
                                              */
    uint16_t                     mclk_div;    /*!< MCLK/LRCK default is 256 if not provided */
} es8390_codec_cfg_t;

/**
 * @brief         New ES8390 codec interface
 * @param         codec_cfg: ES8390 codec configuration
 * @return        NULL: Fail to new ES8390 codec interface
 *                -Others: ES8390 codec interface
 */
const audio_codec_if_t *es8390_codec_new(es8390_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif

#endif
