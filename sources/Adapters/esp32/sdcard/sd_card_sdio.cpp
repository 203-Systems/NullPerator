/**
 * Copyright (c) 2011-2024 Bill Greiman
 * This file is part of the SdFat library for SD memory cards.
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "System/Console/Trace.h"
#include "Adapters/esp32/platform/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "soc/gpio_sig_map.h"
#include "sdmmc_cmd.h"
#include <SdCard/SdCardInfo.h>
#include <SdFat.h>

static const uint8_t IDLE_STATE = 0;
static const uint8_t READ_STATE = 1;
static const uint8_t WRITE_STATE = 2;
uint32_t m_curSector;
SdioConfig m_sdioConfig;
uint8_t m_curState = IDLE_STATE;

// Newly added member variables
sdmmc_card_t* m_card = nullptr;
uint8_t m_type = 0;

bool SdioCard::begin(SdioConfig sdioConfig) {
    m_sdioConfig = sdioConfig;
    esp_err_t ret;

    // Use SDMMC host
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    gpio_iomux_out(SD_CLK_PIN, SDHOST_CCLK_OUT_1_IDX, false);

    gpio_iomux_in(SD_CMD_PIN, SDHOST_CCMD_IN_1_IDX);
    gpio_iomux_out(SD_CMD_PIN, SDHOST_CCMD_OUT_1_IDX, false);

    gpio_iomux_in(SD_D0_PIN, SDHOST_CDATA_IN_10_IDX);
    gpio_iomux_out(SD_D0_PIN, SDHOST_CDATA_OUT_10_IDX, false);

    gpio_iomux_in(SD_D1_PIN, SDHOST_CDATA_IN_11_IDX);
    gpio_iomux_out(SD_D1_PIN, SDHOST_CDATA_OUT_11_IDX, false);

    gpio_iomux_in(SD_D2_PIN, SDHOST_CDATA_IN_12_IDX);
    gpio_iomux_out(SD_D2_PIN, SDHOST_CDATA_OUT_12_IDX, false);

    gpio_iomux_in(SD_D3_PIN, SDHOST_CDATA_IN_13_IDX);
    gpio_iomux_out(SD_D3_PIN, SDHOST_CDATA_OUT_13_IDX, false);

    gpio_iomux_in(SD_CD_PIN, SDHOST_CARD_INT_N_1_IDX);

    // Configure the host to use DMA or not based on sdioConfig
    // if (sdioConfig.useDma()) {
    //     host.flags |= SDMMC_HOST_FLAG_DMA;
    // } else {
    //     host.flags &= ~SDMMC_HOST_FLAG_DMA;
    // }

    // Configure slot
    sdmmc_slot_config_t slot_config = 
    {
        .clk = (gpio_num_t)SD_CLK_PIN,
        .cmd = (gpio_num_t)SD_CMD_PIN,
        .d0 = (gpio_num_t)SD_D0_PIN,
        .d1 = (gpio_num_t)SD_D1_PIN,
        .d2 = (gpio_num_t)SD_D2_PIN,
        .d3 = (gpio_num_t)SD_D3_PIN,
        .cd = (gpio_num_t)SD_CD_PIN,
        .width = 4,
        .flags = 0
    };

    // Initialize the SDMMC host
    ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        Trace::Log("SDIO", "Failed to initialize the SDMMC host (%s)", esp_err_to_name(ret));
        return false;
    }

    // Initialize the slot
    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot_config);
    if (ret != ESP_OK) {
        Trace::Log("SDIO", "Failed to initialize the SDMMC slot (%s)", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return false;
    }

    // Allocate the card object
    m_card = (sdmmc_card_t*)malloc(sizeof(sdmmc_card_t));
    if (!m_card) {
        Trace::Log("SDIO", "Failed to allocate memory for sdmmc_card_t");
        sdmmc_host_deinit();
        return false;
    }

    for (;;) {
        if (sdmmc_card_init(&host, m_card) == ESP_OK) {
            break;
        }
        Trace::Log("SDIO", "Failed to initialize the SD card (%s)", esp_err_to_name(ret));
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    sdmmc_card_print_info(stdout, m_card);

    // Save the card type
    m_type = (m_card->ocr & SD_OCR_SDHC_CAP) ? 3 : 1;

    return true;
}

// void SdioCard::end() {
//     if (m_card) {
//         free(m_card);
//         m_card = nullptr;
//     }
//     sdmmc_host_deinit();
// }

uint8_t SdioCard::errorCode() const {
    // Implement as needed. For now, return 0.
    return 0;
}

uint32_t SdioCard::errorData() const {
    // Implement as needed. For now, return 0.
    return 0;
}

uint32_t SdioCard::errorLine() const {
    // Implement as needed. For now, return 0.
    return 0;
}

bool SdioCard::isBusy() {
    // Implement if necessary. For now, return false.
    return false;
}

uint32_t SdioCard::kHzSdClk() {
    // Return the SD clock frequency in kHz
    if (m_card) {
        return m_card->max_freq_khz;
    } else {
        return 0;
    }
}

bool SdioCard::readSector(uint32_t sector, uint8_t* dst) {
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        return false;
    }
    esp_err_t ret = sdmmc_read_sectors(m_card, dst, sector, 1);
    if (ret != ESP_OK) {
        Trace::Log("SDIO", "Failed to read sector %u (%s)", sector, esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool SdioCard::readSectors(uint32_t sector, uint8_t* dst, size_t ns) {
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        return false;
    }
    esp_err_t ret = sdmmc_read_sectors(m_card, dst, sector, ns);
    if (ret != ESP_OK) {
        Trace::Log("SDIO", "Failed to read sectors starting from %u (%s)", sector, esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool SdioCard::readCID(cid_t* cid) {
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        return false;
    }
    memcpy(cid, &m_card->cid, sizeof(cid_t));
    return true;
}

bool SdioCard::readCSD(csd_t* csd) {
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        return false;
    }
    memcpy(csd, &m_card->csd, sizeof(csd_t));
    return true;
}

bool SdioCard::readOCR(uint32_t* ocr) {
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        return false;
    }
    *ocr = m_card->ocr;
    return true;
}

uint32_t SdioCard::sectorCount() {
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        return 0;
    }
    return m_card->csd.capacity;
}

uint32_t SdioCard::status() {
    if (!m_card) {
        Trace::Error("SDIO", "Card not initialized");
        return 0;
    }

    sdmmc_command_t cmd = {};
    cmd.opcode = MMC_SEND_STATUS; // CMD13
    cmd.arg = MMC_ARG_RCA(m_card->rca);
    cmd.flags = SCF_CMD_AC | SCF_RSP_R1;

    esp_err_t ret = sdmmc_host_do_transaction(m_card->host.slot, &cmd);
    if (ret != ESP_OK) {
        Trace::Error("SDIO", "Failed to get card status (%s)", esp_err_to_name(ret));
        return 0;
    }

    return cmd.response[0];
}

bool SdioCard::stopTransmission(bool blocking) {
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        return false;
    }

    // Prepare the command to send CMD12
    sdmmc_command_t cmd = {};
    cmd.opcode = MMC_STOP_TRANSMISSION; // CMD12
    cmd.arg = 0;
    cmd.flags = SCF_CMD_AC | SCF_RSP_R1B; // Response with busy signal

    // Send the command
    esp_err_t ret = sdmmc_host_do_transaction(m_card->host.slot, &cmd);
    if (ret != ESP_OK) {
        Trace::Log("SDIO", "Failed to send CMD12 (%s)", esp_err_to_name(ret));
        return false;
    }

    // // If blocking, wait for the card to be ready
    // if (blocking) {
    //     // Wait until the card is no longer busy
    //     ret = sdmmc_wait_data_complete(m_card, 1000);
    //     if (ret != ESP_OK) {
    //         Trace::Log("SDIO", "Card did not transition to transfer state (%s)", esp_err_to_name(ret));
    //         return false;
    //     }
    // }

    // Reset the current state to IDLE
    m_curState = IDLE_STATE;
    return true;
}

bool SdioCard::syncDevice() { return true; }

uint8_t SdioCard::type() const {
    return m_type;
}

bool SdioCard::writeSector(uint32_t sector, const uint8_t* src) {
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        return false;
    }
    esp_err_t ret = sdmmc_write_sectors(m_card, src, sector, 1);
    if (ret != ESP_OK) {
        Trace::Log("SDIO", "Failed to write sector %u (%s)", sector, esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool SdioCard::writeSectors(uint32_t sector, const uint8_t* src, size_t ns) {
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        return false;
    }
    esp_err_t ret = sdmmc_write_sectors(m_card, src, sector, ns);
    if (ret != ESP_OK) {
        Trace::Log("SDIO", "Failed to write sectors starting from %u (%s)", sector, esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool SdioCard::readStart(uint32_t sector) {
    if (m_curState != IDLE_STATE) {
        Trace::Log("SDIO", "Card is busy");
        return false;
    }
    m_curState = READ_STATE;
    m_curSector = sector;
    return true;
}

bool SdioCard::readData(uint8_t* dst) {
    if (m_curState != READ_STATE) {
        Trace::Log("SDIO", "Not in read state");
        return false;
    }
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        m_curState = IDLE_STATE;
        return false;
    }
    esp_err_t ret = sdmmc_read_sectors(m_card, dst, m_curSector, 1);
    if (ret != ESP_OK) {
        Trace::Log("SDIO", "Failed to read sector %u (%s)", m_curSector, esp_err_to_name(ret));
        m_curState = IDLE_STATE;
        return false;
    }
    m_curSector++;
    return true;
}

bool SdioCard::readStop() {
    if (m_curState != READ_STATE) {
        Trace::Log("SDIO", "Not in read state");
        return false;
    }
    m_curState = IDLE_STATE;
    return true;
}

bool SdioCard::writeStart(uint32_t sector) {
    if (m_curState != IDLE_STATE) {
        Trace::Log("SDIO", "Card is busy");
        return false;
    }
    m_curState = WRITE_STATE;
    m_curSector = sector;
    return true;
}

bool SdioCard::writeData(const uint8_t* src) {
    if (m_curState != WRITE_STATE) {
        Trace::Log("SDIO", "Not in write state");
        return false;
    }
    if (!m_card) {
        Trace::Log("SDIO", "Card not initialized");
        m_curState = IDLE_STATE;
        return false;
    }
    esp_err_t ret = sdmmc_write_sectors(m_card, src, m_curSector, 1);
    if (ret != ESP_OK) {
        Trace::Log("SDIO", "Failed to write sector %u (%s)", m_curSector, esp_err_to_name(ret));
        m_curState = IDLE_STATE;
        return false;
    }
    m_curSector++;
    return true;
}

bool SdioCard::writeStop() {
    if (m_curState != WRITE_STATE) {
        Trace::Log("SDIO", "Not in write state");
        return false;
    }
    m_curState = IDLE_STATE;
    return true;
}

bool SdioCard::erase(uint32_t firstSector, uint32_t lastSector) {
  Trace::Log("SDIO", "SdioCard::erase() not implemented");
  return false;
}

bool SdioCard::cardCMD6(uint32_t arg, uint8_t *status) {
  Trace::Log("SDIO", "SdioCard::cardCMD6() not implemented");
  return false;
}

bool SdioCard::readSCR(scr_t *scr) {
  Trace::Log("SDIO", "SdioCard::readSCR() not implemented");
  return false;
}

// These functions are not used for SDIO mode but are needed to avoid build
// error.
void sdCsInit(SdCsPin_t pin) {}
void sdCsWrite(SdCsPin_t pin, bool level) {}

