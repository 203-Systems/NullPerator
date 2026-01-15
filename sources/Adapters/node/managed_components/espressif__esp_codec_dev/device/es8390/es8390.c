/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

#include "es_common.h"
#include "esp_codec_dev_vol.h"
#include "esp_log.h"

#include "es8390_codec.h"
#include "es8390_reg.h"

#define TAG "ES8390"

typedef struct {
    audio_codec_if_t   base;
    es8390_codec_cfg_t cfg;
    bool               is_open;
    bool               enabled;
    float              hw_gain;
    bool               use_mclk;
} audio_codec_es8390_t;

static const esp_codec_dev_vol_range_t vol_range = {
    .min_vol =
    {
        .vol = 0x0,
        .db_value = -95.5,
    },
    .max_vol =
    {
        .vol = 0xFF,
        .db_value = 32.0,
    },
};

static int es8390_write_reg(audio_codec_es8390_t *codec, int reg, int value)
{
    return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, reg, 1, &value, 1);
}

static int es8390_read_reg(audio_codec_es8390_t *codec, int reg, int *value)
{
    *value = 0;
    return codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, reg, 1, value, 1);
}

static int es8390_update_bits(audio_codec_es8390_t *codec, uint8_t reg_addr, uint8_t mask, uint8_t val)
{
    int regval = 0;

    (void)es8390_read_reg(codec, reg_addr, &regval);
    regval &= ~mask;
    regval |= (val & mask);
    return es8390_write_reg(codec, reg_addr, regval);
}

static void es8390_pa_power(audio_codec_es8390_t *codec, bool enable)
{
    if (codec->cfg.gpio_if && codec->cfg.pa_pin >= 0) {
        (void)codec->cfg.gpio_if->setup(codec->cfg.pa_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
        (void)codec->cfg.gpio_if->set(codec->cfg.pa_pin, codec->cfg.pa_reverted ? !enable : enable);
    }
}

static int es8390_set_bias_standby(audio_codec_es8390_t *codec)
{
    int ret = es8390_update_bits(codec, ES8390_DAC_CONTROL_REG0x40, 0x03, 0x03);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x10, 0xD4);
    vTaskDelay(pdMS_TO_TICKS(70));
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x61, 0x59);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x64, 0x00);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x03, 0x00);
    ret |= es8390_write_reg(codec, ES8390_RESET_REG0x00, 0x7E);
    ret |= es8390_update_bits(codec, ES8390_DAC_CONTROL_REG0x40, 0x03, 0x00);
    return ret;
}

static int es8390_set_bias_on(audio_codec_es8390_t *codec)
{
    int ret = es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x4D, 0x00);
    ret |= es8390_update_bits(codec, ES8390_ANALOG_CONTROL_REG0x69, 0x20, 0x20);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x61, 0xD9);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x64, 0x8F);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x10, 0xE4);
    ret |= es8390_write_reg(codec, ES8390_RESET_REG0x00, 0x01);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x03, 0xC3);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x24, 0x6A);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x25, (uint8_t)(0x0A + (0 << 6) + (0 << 5)));
    return ret;
}

static int es8390_config_fmt(audio_codec_es8390_t *codec, es_i2s_fmt_t fmt)
{
    int ret = ESP_CODEC_DEV_OK;
    int state = 0;

    switch (fmt) {
        case ES_I2S_NORMAL:
            state |= ES8390_DAIFMT_I2S;
            ret |= es8390_update_bits(codec, ES8390_CLK_MANAGER_REG0x0C, 0xE0, 0x00);
            break;
        case ES_I2S_LEFT:
        case ES_I2S_RIGHT:
            state |= ES8390_DAIFMT_LEFT_J;
            ret |= es8390_update_bits(codec, ES8390_CLK_MANAGER_REG0x0C, 0xE0, 0x40);
            break;
        case ES_I2S_DSP:
            state |= ES8390_DAIFMT_DSP_A;
            ret |= es8390_update_bits(codec, ES8390_CLK_MANAGER_REG0x0C, 0xE0, 0x80);
            break;
        default:
            state |= ES8390_DAIFMT_DSP_B;
            ret |= es8390_update_bits(codec, ES8390_CLK_MANAGER_REG0x0C, 0xE0, 0xA0);
            break;
    }

    ret |= es8390_update_bits(codec, ES8390_ADC_SP_CONTROL_REG0x20, ES8390_MASK_DAIFMT, state);
    ret |= es8390_update_bits(codec, ES8390_DAC_CONTROL_REG0x40, ES8390_MASK_DAIFMT, state);

    return ret;
}

static int es8390_set_bits_per_sample(audio_codec_es8390_t *codec, int bits)
{
    int ret = ESP_CODEC_DEV_OK;
    int state = 0;

    switch (bits) {
        case 16:
        default:
            state |= ES8390_S16_LE;
            break;
        case 18:
            state |= ES8390_S18_LE;
            break;
        case 20:
            state |= ES8390_S20_LE;
            break;
        case 24:
            state |= ES8390_S24_LE;
            break;
        case 32:
            state |= ES8390_S32_LE;
            break;
    }

    ret |= es8390_update_bits(codec, ES8390_ADC_SP_CONTROL_REG0x20, ES8390_MASK_DATALEN, state);
    ret |= es8390_update_bits(codec, ES8390_DAC_CONTROL_REG0x40, ES8390_MASK_DATALEN, state);
    return ret;
}

static int es8390_suspend(audio_codec_es8390_t *codec)
{
    int ret = ESP_CODEC_DEV_OK;
    ret |= es8390_update_bits(codec, ES8390_DAC_CONTROL_REG0x40, 0x03, 0x03);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x10, 0xD4);
    vTaskDelay(pdMS_TO_TICKS(70));
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x61, 0x59);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x64, 0x00);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x63, 0x00);
    ret |= es8390_write_reg(codec, ES8390_RESET_REG0x00, 0x7E);
    ret |= es8390_update_bits(codec, ES8390_DAC_CONTROL_REG0x40, 0x03, 0x00);

    ret |= es8390_write_reg(codec, ES8390_MISC_CONTROL_REG0x01, 0x28);
    ret |= es8390_update_bits(codec, ES8390_ANALOG_CONTROL_REG0x69, 0x20, 0x00);
    ret |= es8390_write_reg(codec, ES8390_VMID_CONTROL_REG0x60, 0x00);
    ret |= es8390_write_reg(codec, ES8390_RESET_REG0x00, 0x00);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x10, 0xCC);
    vTaskDelay(pdMS_TO_TICKS(500));
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x10, 0x00);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x61, 0x08);
    ret |= es8390_write_reg(codec, ES8390_ISOLATION_CONTROL_REG0xF3, 0xC1);
    ret |= es8390_write_reg(codec, ES8390_PULL_DOWN_CONTROL_REG0xF2, 0x00);

    return ret;
}

static int es8390_start(audio_codec_es8390_t *codec)
{
    return es8390_set_bias_on(codec);
}

static int es8390_open(const audio_codec_if_t *h, void *cfg, int cfg_size)
{
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)h;
    es8390_codec_cfg_t *codec_cfg = (es8390_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL || cfg_size != sizeof(es8390_codec_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memcpy(&codec->cfg, cfg, sizeof(es8390_codec_cfg_t));
    if (codec->cfg.mclk_div == 0) {
        codec->cfg.mclk_div = MCLK_DEFAULT_DIV;
    }

    int ret = ESP_CODEC_DEV_OK;

    ret |= es8390_write_reg(codec, ES8390_ISOLATION_CONTROL_REG0xF3, 0x00);
    ret |= es8390_write_reg(codec, ES8390_RESET_REG0x00, 0x7E);
    ret |= es8390_write_reg(codec, ES8390_ISOLATION_CONTROL_REG0xF3, 0x38);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x24, 0x64);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x25, (int)(0x04 + (0 << 6) + (0 << 5)));
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x45, (int)(0x03 + (0 << 6) + (0 << 5)));
    ret |= es8390_write_reg(codec, ES8390_VMID_CONTROL_REG0x60, 0x2A);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x61, 0xC9);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x62, 0x4F);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x63, 0x06);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x6B, 0x00);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x6D, (int)(0x16 + (0 & 0xC0)));
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x6E, 0xAA);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x6F, 0x66);
    ret |= es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x70, 0x99);

    if (ES8390_Analog_DriveSel == ES8390_DriveSel_LowPower) {
        (void)es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x6B, 0x80);
        (void)es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x6C, 0x0F);
        (void)es8390_write_reg(codec, ES8390_ANALOG_CONTROL_REG0x70, 0x66);
    }

    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x23,
                            (int)(0x00 + (0 & 0xC0) + (0 << 2) + (0 & 0x03)));
    ret |= es8390_write_reg(codec, ES8390_PGA1_GAIN_CONTROL_REG0x72, (int)((1 << 4) + 0));
    ret |= es8390_write_reg(codec, ES8390_PGA1_GAIN_CONTROL_REG0x73, (int)((1 << 4) + 0));
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x10, 0xC4);
    ret |= es8390_write_reg(codec, ES8390_MISC_CONTROL_REG0x01,
                            (int)(0x08 + (0 << 7) + (0 << 6) + (0 << 5) + (0 << 0)));
    ret |= es8390_write_reg(codec, ES8390_CSM_STATE_REG0xF1, 0x00);
    ret |= es8390_write_reg(codec, 0x12, 0x01);
    ret |= es8390_write_reg(codec, 0x13, 0x01);
    ret |= es8390_write_reg(codec, 0x14, 0x01);
    ret |= es8390_write_reg(codec, 0x15, 0x01);
    ret |= es8390_write_reg(codec, 0x16, 0x35);
    ret |= es8390_write_reg(codec, 0x17, 0x09);
    ret |= es8390_write_reg(codec, 0x18, 0x91);
    ret |= es8390_write_reg(codec, 0x19, 0x28);
    ret |= es8390_write_reg(codec, 0x1A, 0x01);
    ret |= es8390_write_reg(codec, 0x1B, 0x01);
    ret |= es8390_write_reg(codec, 0x1C, 0x11);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x2A, (int)(0x00 + (0 << 4)));
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x20,
                            (int)(0x00 + ES8390_S16_LE + (0 << 4) + ES8390_DAIFMT_I2S + (0 << 1) + (0 << 0)));
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x40,
                            (int)(0x00 + ES8390_S16_LE + (0 << 4) + ES8390_DAIFMT_I2S + (0 << 1) + (0 << 0)));
    ret |= es8390_write_reg(codec, ES8390_CHIP_MISC_CONTROL_REG0xF0, (int)(0x1 + (0 << 3) + (0 << 2)));
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x02, (int)(0x00 + (0 << 6) + (0 << 1) + (0 << 0)));
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x04, 0x00);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x05, 0x10);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x06, 0x00);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x07, 0xC0);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x08, 0x00);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x09, 0xC0);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x0A, 0x80);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x0B, 4);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x0C, (int)(256 >> 8));
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x0D, (int)(256 & 0xFF));
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x0F, 0x10);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x21, 0x1F);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x22, 0x7F);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x2F, 0xC0);
    ret |= es8390_write_reg(codec, 0x30, 0xF4);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x31, (int)(0x00 + (0 << 7) + (0 << 6)));
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x44,
                            (int)(0x00 + (0 << 3) + (0 << 2) + (0 << 1) + (0 << 0)));
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x41, 0x7F);
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x42, 0x7F);
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x43, 0x10);
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x49, (int)(0x0F + (0 << 4)));
    ret |= es8390_write_reg(codec, 0x4C, 0xC0);

    ret |= es8390_write_reg(codec, ES8390_RESET_REG0x00, 0x00);
    ret |= es8390_write_reg(codec, ES8390_CLK_MANAGER_REG0x03, 0xC1);
    ret |= es8390_write_reg(codec, ES8390_RESET_REG0x00, 0x01);
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x4D, 0x00);

    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x26, 191);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x27, 191);
    ret |= es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x28, 191);
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x46, 191);
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x47, 191);
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x48, (int)(95 << 1));

    if (codec_cfg->master_mode) {
        ESP_LOGI(TAG, "Work in Master mode");
        (void)es8390_update_bits(codec, ES8390_MISC_CONTROL_REG0x01, ES8390_MASK_MSModeSel, 1);
    } else {
        ESP_LOGI(TAG, "Work in Slave mode");
        (void)es8390_update_bits(codec, ES8390_MISC_CONTROL_REG0x01, ES8390_MASK_MSModeSel, 0);
    }

    // Select clock source for internal mclk
    if (codec_cfg->use_mclk) {
        (void)es8390_update_bits(codec, ES8390_CLK_MANAGER_REG0x02, 0xC0, 0 << 6);
        codec->use_mclk = true;
    } else {
        (void)es8390_update_bits(codec, ES8390_CLK_MANAGER_REG0x02, 0xC0, 1 << 6);
        codec->use_mclk = false;
    }
    // MCLK inverted or not
    if (codec_cfg->invert_mclk) {
        (void)es8390_update_bits(codec, ES8390_CLK_MANAGER_REG0x02, 0x02, 1 << 1);
    } else {
        (void)es8390_update_bits(codec, ES8390_CLK_MANAGER_REG0x02, 0x02, 0 << 1);
    }

    // Set ADC and DAC data format
    if (codec_cfg->no_dac_ref == false) {
        /* set internal reference signal (ADCL + DACR) */
        ret |= es8390_write_reg(codec, ES8390_CHIP_MISC_CONTROL_REG0xF0, (int)(0x12 + (1 << 3) + (0 << 2)));
        ESP_LOGI(TAG, "Set internal reference signal");
    } else {
        ret |= es8390_write_reg(codec, ES8390_CHIP_MISC_CONTROL_REG0xF0, (int)(0x12 + (0 << 3) + (0 << 2)));
    }

    // SCLK inverted or not
    if (codec_cfg->invert_sclk) {
        (void)es8390_update_bits(codec, ES8390_CLK_MANAGER_REG0x02, 0x01, 1 << 0);
    } else {
        (void)es8390_update_bits(codec, ES8390_CLK_MANAGER_REG0x02, 0x01, 0 << 0);
    }

    if (ret != 0) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    es8390_pa_power(codec, ES_PA_SETUP | ES_PA_ENABLE);
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int es8390_close(const audio_codec_if_t *h)
{
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        es8390_pa_power(codec, ES_PA_DISABLE);
        (void)es8390_suspend(codec);
        codec->is_open = false;
    }
    return ESP_CODEC_DEV_OK;
}

static int es8390_set_fs(const audio_codec_if_t *h, esp_codec_dev_sample_info_t *fs)
{
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    if (!codec->use_mclk) {
        ESP_LOGW(TAG, "use_mclk=false not supported yet; continuing without coeff config");
    }

    (void)es8390_set_bits_per_sample(codec, fs->bits_per_sample);
    (void)es8390_config_fmt(codec, ES_I2S_NORMAL);
    (void)es8390_set_bias_standby(codec);
    (void)es8390_set_bias_on(codec);
    return ESP_CODEC_DEV_OK;
}

static int es8390_enable(const audio_codec_if_t *h, bool enable)
{
    int ret = ESP_CODEC_DEV_OK;
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (enable == codec->enabled) {
        return ESP_CODEC_DEV_OK;
    }
    if (enable) {
        ret = es8390_start(codec);
        es8390_pa_power(codec, ES_PA_ENABLE);
    } else {
        es8390_pa_power(codec, ES_PA_DISABLE);
        ret = es8390_suspend(codec);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        codec->enabled = enable;
    }
    return ret;
}

static int es8390_set_mute(const audio_codec_if_t *h, bool mute)
{
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int regv = 0;
    int ret = es8390_read_reg(codec, ES8390_ADC_SP_CONTROL_REG0x20, &regv);
    regv &= 0xFC;
    if (mute) {
        regv |= 0x03;
    }
    (void)es8390_write_reg(codec, ES8390_ADC_SP_CONTROL_REG0x20, regv);
    return ret;
}

static int es8390_set_vol(const audio_codec_if_t *h, float db_value)
{
    int ret = ESP_CODEC_DEV_OK;
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)h;

    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }

    db_value -= codec->hw_gain;
    int reg = esp_codec_dev_vol_calc_reg(&vol_range, db_value);
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x46, (uint8_t)reg);
    ret |= es8390_write_reg(codec, ES8390_DAC_CONTROL_REG0x47, (uint8_t)reg);
    return ret;
}

static int es8390_set_mic_gain(const audio_codec_if_t *h, float db)
{
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    es8390_mic_gain_t gain_db = ES8390_MIC_GAIN_0DB;
    if (db < 6) {
        gain_db = ES8390_MIC_GAIN_3_5DB;
    } else if (db < 9) {
        gain_db = ES8390_MIC_GAIN_6_5DB;
    } else if (db < 12) {
        gain_db = ES8390_MIC_GAIN_9_5DB;
    } else if (db < 15) {
        gain_db = ES8390_MIC_GAIN_12_5DB;
    } else if (db < 18) {
        gain_db = ES8390_MIC_GAIN_15_5DB;
    } else if (db < 21) {
        gain_db = ES8390_MIC_GAIN_18_5DB;
    } else if (db < 24) {
        gain_db = ES8390_MIC_GAIN_21_5DB;
    } else if (db < 27) {
        gain_db = ES8390_MIC_GAIN_24_5DB;
    } else if (db < 30) {
        gain_db = ES8390_MIC_GAIN_27_5DB;
    } else if (db < 33) {
        gain_db = ES8390_MIC_GAIN_30_5DB;
    } else if (db < 36) {
        gain_db = ES8390_MIC_GAIN_33_5DB;
    } else {
        gain_db = ES8390_MIC_GAIN_36_5DB;
    }
    (void)es8390_write_reg(codec, ES8390_PGA1_GAIN_CONTROL_REG0x72, (int)((1 << 4) + gain_db));
    (void)es8390_write_reg(codec, ES8390_PGA1_GAIN_CONTROL_REG0x73, (int)((1 << 4) + gain_db));
    return ESP_CODEC_DEV_OK;
}

static int es8390_set_reg(const audio_codec_if_t *h, int reg, int value)
{
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return es8390_write_reg(codec, reg, value);
}

static int es8390_get_reg(const audio_codec_if_t *h, int reg, int *value)
{
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return es8390_read_reg(codec, reg, value);
}

static void es8390_dump(const audio_codec_if_t *h)
{
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return;
    }
    for (int i = 0; i < ES8390_MAX_REGISTER; i++) {
        int value = 0;
        int ret = es8390_read_reg(codec, i, &value);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        ESP_LOGI(TAG, "%02x: %02x", i, value);
    }
}

const audio_codec_if_t *es8390_codec_new(es8390_codec_cfg_t *codec_cfg)
{
    if (codec_cfg == NULL || codec_cfg->ctrl_if == NULL) {
        ESP_LOGE(TAG, "Wrong codec config");
        return NULL;
    }
    if (codec_cfg->ctrl_if->is_open(codec_cfg->ctrl_if) == false) {
        ESP_LOGE(TAG, "Control interface not open yet");
        return NULL;
    }
    audio_codec_es8390_t *codec = (audio_codec_es8390_t *)calloc(1, sizeof(audio_codec_es8390_t));
    if (codec == NULL) {
        CODEC_MEM_CHECK(codec);
        return NULL;
    }
    codec->base.open = es8390_open;
    codec->base.enable = es8390_enable;
    codec->base.set_fs = es8390_set_fs;
    codec->base.set_vol = es8390_set_vol;
    codec->base.set_mic_gain = es8390_set_mic_gain;
    codec->base.mute = es8390_set_mute;
    codec->base.set_reg = es8390_set_reg;
    codec->base.get_reg = es8390_get_reg;
    codec->base.dump_reg = es8390_dump;
    codec->base.close = es8390_close;
    codec->hw_gain = esp_codec_dev_col_calc_hw_gain(&codec_cfg->hw_gain);
    do {
        int ret = codec->base.open(&codec->base, codec_cfg, sizeof(es8390_codec_cfg_t));
        if (ret != 0) {
            ESP_LOGE(TAG, "Open fail");
            break;
        }
        return &codec->base;
    } while (0);
    free(codec);
    return NULL;
}
