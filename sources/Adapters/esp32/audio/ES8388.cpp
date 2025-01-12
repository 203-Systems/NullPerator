#include "ES8388.h"
#include "esp_log.h"

#define ES8388_ADDR 0x10
/* ES8388 register */
#define ES8388_CONTROL1 0x00
#define ES8388_CONTROL2 0x01
#define ES8388_CHIPPOWER 0x02
#define ES8388_ADCPOWER 0x03
#define ES8388_DACPOWER 0x04
#define ES8388_CHIPLOPOW1 0x05
#define ES8388_CHIPLOPOW2 0x06
#define ES8388_ANAVOLMANAG 0x07
#define ES8388_MASTERMODE 0x08
/* ADC */
#define ES8388_ADCCONTROL1 0x09
#define ES8388_ADCCONTROL2 0x0a
#define ES8388_ADCCONTROL3 0x0b
#define ES8388_ADCCONTROL4 0x0c
#define ES8388_ADCCONTROL5 0x0d
#define ES8388_ADCCONTROL6 0x0e
#define ES8388_ADCCONTROL7 0x0f
#define ES8388_ADCCONTROL8 0x10
#define ES8388_ADCCONTROL9 0x11
#define ES8388_ADCCONTROL10 0x12
#define ES8388_ADCCONTROL11 0x13
#define ES8388_ADCCONTROL12 0x14
#define ES8388_ADCCONTROL13 0x15
#define ES8388_ADCCONTROL14 0x16
/* DAC */
#define ES8388_DACCONTROL1 0x17
#define ES8388_DACCONTROL2 0x18
#define ES8388_DACCONTROL3 0x19
#define ES8388_DACCONTROL4 0x1a
#define ES8388_DACCONTROL5 0x1b
#define ES8388_DACCONTROL6 0x1c
#define ES8388_DACCONTROL7 0x1d
#define ES8388_DACCONTROL8 0x1e
#define ES8388_DACCONTROL9 0x1f
#define ES8388_DACCONTROL10 0x20
#define ES8388_DACCONTROL11 0x21
#define ES8388_DACCONTROL12 0x22
#define ES8388_DACCONTROL13 0x23
#define ES8388_DACCONTROL14 0x24
#define ES8388_DACCONTROL15 0x25
#define ES8388_DACCONTROL16 0x26
#define ES8388_DACCONTROL17 0x27
#define ES8388_DACCONTROL18 0x28
#define ES8388_DACCONTROL19 0x29
#define ES8388_DACCONTROL20 0x2a
#define ES8388_DACCONTROL21 0x2b
#define ES8388_DACCONTROL22 0x2c
#define ES8388_DACCONTROL23 0x2d
#define ES8388_DACCONTROL24 0x2e
#define ES8388_DACCONTROL25 0x2f
#define ES8388_DACCONTROL26 0x30
#define ES8388_DACCONTROL27 0x31
#define ES8388_DACCONTROL28 0x32
#define ES8388_DACCONTROL29 0x33
#define ES8388_DACCONTROL30 0x34

ES8388::ES8388() {}

ES8388::~ES8388() { 
  if (dev_handle) {
   i2c_master_bus_rm_device(dev_handle); 
  }
}

bool ES8388::write_reg(uint8_t reg_add, uint8_t data) { // Modified 
  uint8_t data_wr[2];
  // ESP_LOGI("ES8388", "Writing register 0x%02x - 0x%02x (0b%c%c%c%c%c%c%c%c)", reg_add, data, (data & 0x80) ? '1' : '0', (data & 0x40) ? '1' : '0', (data & 0x20) ? '1' : '0', (data & 0x10) ? '1' : '0', (data & 0x08) ? '1' : '0', (data & 0x04) ? '1' : '0', (data & 0x02) ? '1' : '0', (data & 0x01) ? '1' : '0');
  data_wr[0] = reg_add;
  data_wr[1] = data;
  ESP_ERROR_CHECK(i2c_master_transmit(this->dev_handle, data_wr, 2, 1000));
  return true;
}

bool ES8388::read_reg(uint8_t reg_add, uint8_t& data) { 
    ESP_ERROR_CHECK(i2c_master_transmit_receive(
        this->dev_handle,            // I2C device handle
        &reg_add,                       // Pointer to register address
        1,                // Size of register address
        &data,                          // Buffer to store received data
        1,                               // Number of bytes to read
        1000                            // Timeout in milliseconds
    ));
    // ESP_LOGI("ES8388", "Reading register 0x%02x - 0x%02x (0b%c%c%c%c%c%c%c%c)", reg_add, data, (data & 0x80) ? '1' : '0', (data & 0x40) ? '1' : '0', (data & 0x20) ? '1' : '0', (data & 0x10) ? '1' : '0', (data & 0x08) ? '1' : '0', (data & 0x04) ? '1' : '0', (data & 0x02) ? '1' : '0', (data & 0x01) ? '1' : '0');
    return true;
}


uint8_t* ES8388::readAllReg() {
  static uint8_t reg[53];
  for (uint8_t i = 0; i < 53; i++) {
    read_reg(i, reg[i]);
  }
  return reg;
}

bool ES8388::init(i2c_master_bus_handle_t i2c_handle, uint32_t _speed) {
  i2c_device_config_t dev_cfg = {
  .device_address = ES8388_ADDR,
  .scl_speed_hz = _speed,
  };
  dev_cfg.flags.disable_ack_check = 0;

  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_handle, &dev_cfg, &dev_handle));
  
  bool res = true;
  uint8_t reg;

  // Init based on https://github.com/marcel-licence/ML_SynthTools/blob/9915b7c71a64126234c1eb836edd2da1f67d7873/src/es8388.h#L399

    assert(write_reg(ES8388_CONTROL1, 1 << 7)); // do reset!
assert(write_reg(ES8388_CONTROL1, 0x06));
assert(write_reg(ES8388_CONTROL2, 0x50));

assert(read_reg(ES8388_CONTROL1, reg));
assert(0x06 == reg);
assert(read_reg(ES8388_CONTROL2, reg));
assert(0x50 == reg);

assert(write_reg(ES8388_CHIPPOWER, 0xFF)); // reset and stop es8388
assert(write_reg(0x00, 0x80)); // reset control port register to default
assert(write_reg(0x00, 0x06)); // restore default value

assert(write_reg(0x0F, 0x34)); // ADC Mute
assert(read_reg(0x0F, reg));
// assert(0x34 == reg);

assert(write_reg(0x19, 0x36)); // DAC Mute
assert(read_reg(0x19, reg));
// assert(0x36 == reg);

assert(write_reg(0x02, 0xF3)); // Power down DEM and STM
assert(read_reg(0x02, reg));
assert(0xF3 == reg);

assert(write_reg(0x08, 0x00)); // Set Chip to Slave Mode
assert(read_reg(0x08, reg));
assert(0x00 == reg);

assert(write_reg(0x02, 0x3F)); // Power down DEM and STM
assert(read_reg(0x02, reg));
assert(0x3F == reg);

assert(write_reg(0x2B, 0x80)); // Set same LRCK
assert(read_reg(0x2B, reg));
assert(0x80 == reg);

assert(write_reg(0x00, 0x05)); // Set Chip to Play&Record Mode
assert(read_reg(0x00, reg));
assert(0x05 == reg);

assert(write_reg(0x01, 0x40)); // Power Up Analog and Ibias
assert(read_reg(0x01, reg));
assert(0x40 == reg);

assert(write_reg(0x03, 0x3F)); // ADC also on but no bias
assert(read_reg(0x03, reg));
assert(0x3F == reg);

assert(write_reg(0x03, 0x00)); // Power down ADC features
assert(read_reg(0x03, reg));
assert(0x00 == reg);

assert(write_reg(0x04, 0x3C)); // Power up DAC / Analog Output for Record
assert(read_reg(0x04, reg));
assert(0x3C == reg);

assert(write_reg(0x0A, 0x80)); // Select Analog input channel for ADC
assert(read_reg(0x0A, reg));
assert(0x80 == reg);

assert(write_reg(0x09, 0x00)); // Select PGA Gain for ADC analog input
assert(read_reg(0x09, reg));
assert(0x00 == reg);

assert(write_reg(0x0C, 0x0C)); // ADC settings
assert(read_reg(0x0C, reg));
assert(0x0C == reg);

assert(write_reg(0x0D, 0x02)); // ADC FsMode and FsRatio
assert(read_reg(0x0D, reg));
assert(0x02 == reg);

assert(write_reg(0x10, 192)); // Set ADC Digital Volume LADCVOL
assert(read_reg(0x10, reg));
assert(192 == reg);

assert(write_reg(0x11, 192)); // Set ADC Digital Volume RADCVOL
assert(read_reg(0x11, reg));
assert(192 == reg);

assert(write_reg(0x0F, 0x30)); // UnMute ADC
assert(read_reg(0x0F, reg));
// assert(0x30 == reg);

assert(write_reg(0x12, 0x16)); 
assert(read_reg(0x12, reg));
assert(0x16 == reg);

assert(write_reg(0x17, 0x18)); // DAC settings
assert(read_reg(0x17, reg));
assert(0x18 == reg);

assert(write_reg(0x18, 0x02)); // DAC FsMode and FsRatio
assert(read_reg(0x18, reg));
assert(0x02 == reg);

assert(write_reg(0x1A, 0x00)); 
assert(read_reg(0x1A, reg));
assert(0x00 == reg);

assert(write_reg(0x1B, 0x02)); 
assert(read_reg(0x1B, reg));
assert(0x02 == reg);

assert(write_reg(0x19, 0x32)); // UnMute DAC
assert(read_reg(0x19, reg));
// assert(0x32 == reg);

assert(write_reg(0x26, 0x09)); // Mixer setup
assert(read_reg(0x26, reg));
assert(0x09 == reg);

assert(write_reg(0x27, 0xD0)); // Mixer control
assert(read_reg(0x27, reg));
assert(0xD0 == reg);

assert(write_reg(0x28, 0x38)); 
assert(read_reg(0x28, reg));
// assert(0x38 == reg);

assert(write_reg(0x29, 0x38)); 
assert(read_reg(0x29, reg));
// assert(0x38 == reg);

assert(write_reg(0x2A, 0xD0)); 
assert(read_reg(0x2A, reg));
assert(0xD0 == reg);

assert(write_reg(0x2E, 129)); // Set Lout/Rout Volume
assert(read_reg(0x2E, reg));
// assert(129 == reg);

assert(write_reg(0x2F, 129)); 
assert(read_reg(0x2F, reg));
// assert(129 == reg);

assert(write_reg(0x30, 192)); 
assert(read_reg(0x30, reg));
// assert(192 == reg);

assert(write_reg(0x31, 192)); 
assert(read_reg(0x31, reg));
// assert(192 == reg);

assert(write_reg(0x02, 0x00)); // Power up DEM and STM
assert(read_reg(0x02, reg));
assert(0x00 == reg);

assert(write_reg(0x0A, (1 << 6) + (1 << 4))); // Set input channel
assert(read_reg(0x0A, reg));
assert(((1 << 6) + (1 << 4)) == reg);

assert(write_reg(0x26, 1 + (1 << 3))); // Set Mixer Input Channel
assert(read_reg(0x26, reg));
assert((1 + (1 << 3)) == reg);

assert(write_reg(0x09, 8 + (8 << 4))); // Set PGA Gain
assert(read_reg(0x09, reg));
assert((8 + (8 << 4)) == reg);

assert(read_reg(0x27, reg));
reg &= 0xC0;
reg &= ~0x40;
assert(write_reg(0x27, (7 << 3) + reg));

assert(read_reg(0x2A, reg));
reg &= 0xC0;
reg &= ~0x40;
assert(write_reg(0x2A, (7 << 3) + reg));



  // /* INITIALIZATION (BASED ON ES8388 USER GUIDE EXAMPLE) */
  // // Set Chip to Slave
  // res &= write_reg(ES8388_MASTERMODE, 0x00);
  // Power down DEM and STM
  // res &= write_reg(ES8388_CHIPPOWER, 0xFF);
  // Set same LRCK	Set same LRCK
  // res &= write_reg(ES8388_DACCONTROL21, 0x80);
  // Set Chip to Play&Record Mode
  // res &= write_reg(ES8388_CONTROL1, 0x05);
  // Power Up Analog and Ibias
  // res &= write_reg(ES8388_CONTROL2, 0x40);

  // Power Up ADC& enable Lin/Rin
  // res &= write_reg(ES8388_ADCPOWER, 0x00);

  // Power Up DAC& enable Lout1/Rout1
  // res &= write_reg(ES8388_DACPOWER, 0x30);

  /* ADC setting */
  // Micbias for Record;

  // // Enable Lin1/Rin1 (0x00 0x00) for Lin2/Rin2 (0x50 0x80)
  // res &= write_reg(ES8388_ADCCONTROL2, 0x50);
  // res &= write_reg(ES8388_ADCCONTROL3, 0x80);
  // // PGA gain (0x88 - 24db) (0x77 - 21db)
  // res &= write_reg(ES8388_ADCCONTROL1, 0x77);
  // // SFI setting (i2s mode/16 bit)
  // res &= write_reg(ES8388_ADCCONTROL4, 0x0C);
  // // ADC MCLK/LCRK ratio (256)
  // res &= write_reg(ES8388_ADCCONTROL5, 0x02);
  // // set ADC digital volume
  // res &= write_reg(ES8388_ADCCONTROL8, 0x00);
  // res &= write_reg(ES8388_ADCCONTROL9, 0x00);
  // // recommended ALC setting for VOICE refer to ES8388 MANUAL
  // res &= write_reg(ES8388_ADCCONTROL10, 0xEA);
  // res &= write_reg(ES8388_ADCCONTROL11, 0xC0);
  // res &= write_reg(ES8388_ADCCONTROL12, 0x12);
  // res &= write_reg(ES8388_ADCCONTROL13, 0x06);
  // res &= write_reg(ES8388_ADCCONTROL14, 0xC3);

  /* DAC setting */

  // SFI setting (i2s mode/16 bit)
  // res &= write_reg(ES8388_DACCONTROL1, 0x18);
  // DAC MCLK/LCRK ratio (256)
  // res &= write_reg(ES8388_DACCONTROL2, 0x02);
  // // unmute codec
  // res &= write_reg(ES8388_DACCONTROL3, 0x00);
  // set DAC digital volume
  // res &= write_reg(ES8388_DACCONTROL4, 0x00);
  // res &= write_reg(ES8388_DACCONTROL5, 0x00);
  // Setup Mixer
  // (reg[16] 1B mic Amp, 0x09 direct;[reg 17-20] 0x90 DAC, 0x50 Mic Amp)
  // res &= write_reg(ES8388_DACCONTROL16, 0x09);
  // res &= write_reg(ES8388_DACCONTROL17, 0x50);
  // res &= write_reg(ES8388_DACCONTROL18, 0x38);  //??
  // res &= write_reg(ES8388_DACCONTROL19, 0x38);  //??
  // res &= write_reg(ES8388_DACCONTROL20, 0x50);
  // set Lout/Rout Volume -45db
  // res &= write_reg(ES8388_DACCONTROL24, 0x1E);
  // res &= write_reg(ES8388_DACCONTROL25, 0x1E);
  // res &= write_reg(ES8388_DACCONTROL26, 0x1E);
  // res &= write_reg(ES8388_DACCONTROL27, 0x1E);

  /* Power up DEM and STM */
  // res &= write_reg(ES8388_CHIPPOWER, 0x00);
  /* set up MCLK) */

  // From ESP ADF

  // bool res = true;
  // //es8388_init
  // res &= write_reg(ES8388_DACCONTROL3, 0x04);  // 0x04 mute/0x00 unmute&ramp;DAC unmute and  disabled digital volume control soft ramp
  // /* Chip Control and Power Management */
  // res &= write_reg(ES8388_CONTROL2, 0x50);
  // res &= write_reg(ES8388_CHIPPOWER, 0x00); //normal all and power up all

  // // Disable the internal DLL to improve 8K sample rate
  // res &= write_reg(0x35, 0xA0);
  // res &= write_reg(0x37, 0xD0);
  // res &= write_reg(0x39, 0xD0);

  // res &= write_reg(ES8388_MASTERMODE, 0); //CODEC IN I2S SLAVE MODE

  // /* dac */
  // res &= write_reg(ES8388_DACPOWER, 0xC0);  //disable DAC and disable Lout/Rout/1/2
  // res &= write_reg(ES8388_CONTROL1, 0x12);  //Enfr=0,Play&Record Mode,(0x17-both of mic&paly)
  // res &= write_reg(ES8388_DACCONTROL1, 0x18);//1a 0x18:16bit iis , 0x00:24
  // res &= write_reg(ES8388_DACCONTROL2, 0x02);  //DACFsMode,SINGLE SPEED; DACFsRatio,256
  // res &= write_reg(ES8388_DACCONTROL16, 0x00); // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2
  // res &= write_reg(ES8388_DACCONTROL17, 0x90); // only left DAC to left mixer enable 0db
  // res &= write_reg(ES8388_DACCONTROL20, 0x90); // only right DAC to right mixer enable 0db
  // res &= write_reg(ES8388_DACCONTROL21, 0x80); // set internal ADC and DAC use the same LRCK clock, ADC LRCK as internal LRCK
  // res &= write_reg(ES8388_DACCONTROL23, 0x00); // vroi=0

  // res &= write_reg(ES8388_DACCONTROL24, 0x1E); // Set L1 R1 L2 R2 volume. 0x00: -30dB, 0x1E: 0dB, 0x21: 3dB
  // res &= write_reg(ES8388_DACCONTROL25, 0x1E);
  // res &= write_reg(ES8388_DACCONTROL26, 0);
  // res &= write_reg(ES8388_DACCONTROL27, 0);

  // // es8388_set_voice_mute
  // uint8_t reg = 0;
  // res &= read_reg(ES8388_DACCONTROL3, reg);
  // reg = reg & 0xFB;
  // res &= write_reg(ES8388_DACCONTROL3, reg | (1 << 2));


  // // es8388_config_i2s -> es8388_config_fmt
  // res &= read_reg(ES8388_DACCONTROL1, reg);
  // reg = reg & 0xf9;
  // res &= write_reg(ES8388_DACCONTROL1, reg | (0 << 1));

  // // es8388_config_i2s -> es8388_set_bits_per_sample
  // res &= read_reg(ES8388_DACCONTROL1, reg);
  // reg = reg & 0xc7;
  // res &= write_reg(ES8388_DACCONTROL1, reg | (0x03 << 3));


  // // es8388_ctrl_state -> es8388_start
  // uint8_t prev_data = 0, data = 0;
  // res &= read_reg(ES8388_DACCONTROL21, prev_data);

  // res &= write_reg(ES8388_DACCONTROL21, 0x80);   //enable dac


  // res &= read_reg(ES8388_DACCONTROL21, data);
  // if (prev_data != data) {
  //       res &= write_reg(ES8388_CHIPPOWER, 0xF0);   //start state machine
  //       res &= write_reg(ES8388_CHIPPOWER, 0x00);   //start state machine
  //   }

  // res &= write_reg(ES8388_DACPOWER, 0x3c);   //power up dac and line out

  // // es8388_set_voice_volume
  // reg = 15;
  // res &= write_reg(ES8388_DACCONTROL5, reg);
  // res &= write_reg(ES8388_DACCONTROL4, reg);

  return true;
}

// Select output sink
// OUT1 -> Select Line OUTL/R1
// OUT2 -> Select Line OUTL/R2
// OUTALL -> Enable ALL
bool ES8388::outputSelect(outsel_t _sel) {
  bool res = true;
  if (_sel == OUTALL)
    res &= write_reg(ES8388_DACPOWER, 0x3C);
  else if (_sel == OUT1)
    res &= write_reg(ES8388_DACPOWER, 0x30);
  else if (_sel == OUT2)
    res &= write_reg(ES8388_DACPOWER, 0x0C);
  _outSel = _sel;
  return res;
}

// Select input source
// IN1     -> Select Line IN L/R 1
// IN2     -> Select Line IN L/R 2
// IN1DIFF -> differential IN L/R 1
// IN2DIFF -> differential IN L/R 2
bool ES8388::inputSelect(insel_t sel) {
  bool res = true;
  if (sel == IN1)
    res &= write_reg(ES8388_ADCCONTROL2, 0x00);
  else if (sel == IN2)
    res &= write_reg(ES8388_ADCCONTROL2, 0x50);
  else if (sel == IN1DIFF) {
    res &= write_reg(ES8388_ADCCONTROL2, 0xF0);
    res &= write_reg(ES8388_ADCCONTROL3, 0x00);
  } else if (sel == IN2DIFF) {
    res &= write_reg(ES8388_ADCCONTROL2, 0xF0);
    res &= write_reg(ES8388_ADCCONTROL3, 0x80);
  }
  _inSel = sel;
  return res;
}

// mute Output
bool ES8388::DACmute(bool mute) {
  uint8_t _reg;
  read_reg(ES8388_ADCCONTROL1, _reg);
  bool res = true;
  if (mute)
    res &= write_reg(ES8388_DACCONTROL3, _reg | 0x04);
  else
    res &= write_reg(ES8388_DACCONTROL3, _reg & ~(0x04));
  return res;
}

// set output volume max is 33
bool ES8388::setOutputVolume(uint8_t vol) {
  if (vol > 33)
    vol = 33;
  bool res = true;
  if (_outSel == OUTALL || _outSel == OUT1) {
    res &= write_reg(ES8388_DACCONTROL24, vol); // LOUT1VOL
    res &= write_reg(ES8388_DACCONTROL25, vol); // ROUT1VOL
  } if (_outSel == OUTALL || _outSel == OUT2) {
    res &= write_reg(ES8388_DACCONTROL26, vol); // LOUT2VOL
    res &= write_reg(ES8388_DACCONTROL27, vol); // ROUT2VOL
  }
  return res;
}

uint8_t ES8388::getOutputVolume() {
  static uint8_t _reg;
  if(_outSel == OUT1)
    read_reg(ES8388_DACCONTROL24, _reg);
  else if(_outSel == OUT2)
    read_reg(ES8388_DACCONTROL26, _reg);
  return _reg;
}

// set input gain max is 8 +24db
bool ES8388::setInputGain(uint8_t gain) {
  if (gain > 8) gain = 8;
  bool res = true;
  gain = (gain << 4) | gain;
  res &= write_reg(ES8388_ADCCONTROL1, gain);
  return res;
}

uint8_t ES8388::getInputGain() {
  static uint8_t _reg;
  read_reg(ES8388_ADCCONTROL1, _reg);
  _reg = _reg & 0x0F;
  return _reg;
}

// Recommended ALC setting from User Guide
// DISABLE -> Disable ALC
// GENERIC -> Generic Mode
// VOICE   -> Voice Mode
// MUSIC   -> Music Mode
bool ES8388::setALCmode(alcmodesel_t alc) {
  bool res = true;

  // generic ALC setting
  uint8_t ALCSEL = 0b11;       // stereo
  uint8_t ALCLVL = 0b0011;     //-12db
  uint8_t MAXGAIN = 0b111;     //+35.5db
  uint8_t MINGAIN = 0b000;     //-12db
  uint8_t ALCHLD = 0b0000;     // 0ms
  uint8_t ALCDCY = 0b0101;     // 13.1ms/step
  uint8_t ALCATK = 0b0111;     // 13.3ms/step
  uint8_t ALCMODE = 0b0;       // ALC
  uint8_t ALCZC = 0b0;         // ZC off
  uint8_t TIME_OUT = 0b0;      // disable
  uint8_t NGAT = 0b1;          // enable
  uint8_t NGTH = 0b10001;      //-51db
  uint8_t NGG = 0b00;          // hold gain
  uint8_t WIN_SIZE = 0b00110;  // default

  if (alc == DISABLE)
    ALCSEL = 0b00;
  else if (alc == MUSIC) {
    ALCDCY = 0b1010;  // 420ms/step
    ALCATK = 0b0110;  // 6.66ms/step
    NGTH = 0b01011;   // -60db
  } else if (alc == VOICE) {
    ALCLVL = 0b1100;  // -4.5db
    MAXGAIN = 0b101;  // +23.5db
    MINGAIN = 0b010;  // 0db
    ALCDCY = 0b0001;  // 820us/step
    ALCATK = 0b0010;  // 416us/step
    NGTH = 0b11000;   // -40.5db
    NGG = 0b01;       // mute ADC
    res &= write_reg(ES8388_ADCCONTROL1, 0x77);
  }
  res &= write_reg(ES8388_ADCCONTROL10, ALCSEL << 6 | MAXGAIN << 3 | MINGAIN);
  res &= write_reg(ES8388_ADCCONTROL11, ALCLVL << 4 | ALCHLD);
  res &= write_reg(ES8388_ADCCONTROL12, ALCDCY << 4 | ALCATK);
  res &= write_reg(ES8388_ADCCONTROL13,
                   ALCMODE << 7 | ALCZC << 6 | TIME_OUT << 5 | WIN_SIZE);
  res &= write_reg(ES8388_ADCCONTROL14, NGTH << 3 | NGG << 2 | NGAT);

  return res;
}

// MIXIN1 – direct IN1 (default)
// MIXIN2 – direct IN2
// MIXRES – reserved es8388
// MIXADC – ADC/ALC input (after mic amplifier)
bool ES8388::mixerSourceSelect(mixsel_t LMIXSEL, mixsel_t RMIXSEL) {
  bool res = true;
  uint8_t _reg;
  _reg = (LMIXSEL << 3) | RMIXSEL;
  res &= write_reg(ES8388_DACCONTROL16, _reg);
  return res;
}

// LD/RD = DAC(i2s), false disable, true enable
// LI2LO/RI2RO from mixerSourceSelect(), false disable, true enable
// LOVOL = gain, 0 -> 6db, 1 -> 3db, 2 -> 0db, higher will attenuate
bool ES8388::mixerSourceControl(bool LD2LO, bool LI2LO, uint8_t LI2LOVOL,
                                bool RD2RO, bool RI2RO, uint8_t RI2LOVOL) {
  bool res = true;
  uint8_t _regL, _regR;
  if (LI2LOVOL > 7) LI2LOVOL = 7;
  if (RI2LOVOL > 7) RI2LOVOL = 7;
  _regL = (LD2LO << 7) | (LI2LO << 6) | (LI2LOVOL << 3);
  _regR = (RD2RO << 7) | (RI2RO << 6) | (RI2LOVOL << 3);
  res &= write_reg(ES8388_DACCONTROL17, _regL);
  res &= write_reg(ES8388_DACCONTROL20, _regR);
  return res;
}

// Mixer source control
// DACOUT -> Select Sink From DAC
// SRCSEL -> Select Sink From SourceSelect()
// MIXALL -> Sink DACOUT + SRCSEL
bool ES8388::mixerSourceControl(mixercontrol_t mix) {
  bool res = true;
  if (mix == DACOUT)
    mixerSourceControl(true, false, 2, true, false, 2);
  else if (mix == SRCSELOUT)
    mixerSourceControl(false, true, 2, false, true, 2);
  else if (mix == MIXALL)
    mixerSourceControl(true, true, 2, true, true, 2);
  return res;
}

// true -> analog out = analog in
// false -> analog out = DAC(i2s)
bool ES8388::analogBypass(bool bypass) {
  bool res = true;
  if (bypass) {
    if (_inSel == IN1)
      mixerSourceSelect(MIXIN1, MIXIN1);
    else if (_inSel == IN2)
      mixerSourceSelect(MIXIN2, MIXIN2);
    mixerSourceControl(false, true, 2, false, true, 2);
  } else {
    mixerSourceControl(true, false, 2, true, false, 2);
  }
  return res;
}
