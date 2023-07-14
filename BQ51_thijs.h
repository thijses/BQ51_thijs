
// https://www.ti.com/lit/ds/symlink/bq51222.pdf
// https://www.ti.com/lit/ds/symlink/bq51221.pdf
// https://www.ti.com/lit/ds/symlink/bq51021.pdf
/*
a little library for interfacing with the Texas Instruments Qi recievers: bq51222, bq51221 and bq51021
I only tested it with the bq51222!

The datasheet does not list clock speed limits, only a typical frequency of 100kHz
Using I2C, you can read/monitor a few things:
- V_RECT (raw voltage, before internal LDO)
- V_OUT (output voltage)
- recevied power
- operating mode (PMA or Qi, and whether 'alignment mode' is enabled)
- RXID (a unique device ID programmed at the factory) (NOTE: only readable when V_RECT > V_UVLO)
overwrite a few settings:
- output voltage (4.5~8V)
- output current limit (10~100% of the limit set by resistors for I_ILIM)
- FOD alterations (offset and ESR changes, not sure how to use those quite yet)
and potentially send proprietary packets (or something, the datasheet is pretty vague):
- custom headers
- custom payloads
- enabling/disabling alignment mode(?)



TODO:
- add example sketch filenames to library.json
- test platforms other than STM32
- check Wire.h function return values for I2C errors
- test if 'static' vars in the ESP32 functions actually are static (connect 2 sensors?)
- generalized memory map struct (also for other libraries). Could just be an enum, i just don't love #define

*/

#ifndef BQ51_thijs_h
#define BQ51_thijs_h

#include "Arduino.h" // always import Arduino.h

//#define BQ51debugPrint(x)  Serial.println(x)
//#define BQ51debugPrint(x)  log_d(x)   //using the ESP32 debug printing

#ifndef BQ51debugPrint
  #define BQ51debugPrint(x)  ;
#endif


//// BQ51 constants:
//// configuration registers: (NOTE: these registers will retain their value when V_RECT goes below V_UVLO)
#define BQ51_VO_REG              0x01 // (R/W) Wireless Power Supply Current Register 1 (bit of a misnomer, as this controls voltage, not current)
#define BQ51_IO_REG              0x02 // (R/W) Wireless Power Supply Current Register 2 (this register name makes sense)
//// output registers: (NOTE: these registers are  RESET! when V_RECT goes below V_UVLO)
#define BQ51_MAILBOX             0xE0 // (R/W) I2C Mailbox Register (involved in Qi packet transfer stuff)
#define BQ51_FOD_RAM             0xE1 // (R/W) Wireless Power Supply FOD RAM Register (involved in Foreign Object Detection)
#define BQ51_USER_HEADER_RAM     0xE2 // (R/W) Wireless Power User Header RAM Register (for custom Qi packets(?))
#define BQ51_VRECT_STATUS_RAM    0xE3 // (R) Wireless Power USER V_RECT Status RAM Register (reads back V_RECT voltage, LSB = 46mV)
#define BQ51_VOUT_STATUS_RAM     0xE4 // (R/W) Wireless Power VO_OUT Status RAM Register (reads back V_OUT voltage, LSB = 46mV)  (TODO: check Write access??)
#define BQ51_REC_PWR_STATUS_RAM  0xE8 // (R/W) Wireless Power REC PWR Byte Status RAM Register (reads back received power, LSB = 39mW)  (TODO: check Write access??)
#define BQ51_MODE_IND            0xEF // (R) Wireless Power Mode Indicator Register (indicates Qi or PMA, and Alignment-mode) (not on BQ51021)
//// Prop(rietary) Packet Payload RAM Byte Registers: (4 contiguous bytes)
#define BQ51_PACKET_PAYLOAD      0xF1 // (R/W) Wireless Power Prop Packet Payload RAM Byte 0 Register (4 bytes!)
// #define BQ51_PACKET_PAYLOAD_0    0xF1 // (R/W) Wireless Power Prop Packet Payload RAM Byte 0 Register
// #define BQ51_PACKET_PAYLOAD_1    0xF2 // (R/W) Wireless Power Prop Packet Payload RAM Byte 1 Register
// #define BQ51_PACKET_PAYLOAD_2    0xF3 // (R/W) Wireless Power Prop Packet Payload RAM Byte 2 Register
// #define BQ51_PACKET_PAYLOAD_3    0xF4 // (R/W) Wireless Power Prop Packet Payload RAM Byte 3 Register
// #define BQ51_PACKET_PAYLOAD_size   4 // size of Prop Packet Payload RAM Byte registers
//// RXID Readback Registers: (6 contiguous bytes)
#define BQ51_RXID_READBACK       0xF5 // (R/W) Wireless Power Readback Register start (unique ID for each device, programmed at factory) (not on BQ51021(?))
#define BQ51_RXID_size     6 // size of RXID (i'm pretty sure)

//// bits:
//// VO_REG and IO_REG Registers:
#define BQ51_VO_REG_bits           0b00000111 // (R/w) 3 LSBits set VO_REG target from 450~800mV, VO_REG = 450+(bits*50) mV
//#define BQ51_JEITA               0b10000000 // "not used" for all BQ51x2x (but it has a name)
//#define BQ51_ITERM_bits          0b00111000 // "not used for BQ5102x" and "not used" for BQ51221 (but it has a name)
#define BQ51_IO_REG_bits           0b00000111 // (R/w) 3 LSBits set I_ILIM current, 10,20,30,40,50,60, 90, 100 % (breaks pattern for for 0b_110 and 0b_111)
//// MAILBOX Register:
#define BQ51_MAILBOX_SEND_bits     0b10000000 // (R/W?) USER_PKT_DONE can be set to 0 to send a packet with header in BQ51_USER_HEADER_RAM, and will read as 1 when packet has been sent
#define BQ51_MAILBOX_ERR_bits      0b01100000 // (R) USER_PKT_ERR bits indicate errors with packet sending: 0=no_err, 1=no_TX, 2=bad_header, 3=err_TBD
//#define BQ51_MAILBOX_FOD_M_bits  0b00010000 // (R/w) FOD Mailer "not used"
#define BQ51_MAILBOX_ALIGN_bits    0b00001000 // (R/w) ALIGN Mailer will "enable alignment aid mode where the CEP = 0" (i think only PMA has an alignment mode???)
#define BQ51_MAILBOX_FOD_S_bits    0b00000100 // (R/w) FOD Scaler, not used, MUST BE 0
//#define BQ51_MAILBOX_RSRVD_bits  0b00000011 // (R/w) last 2 bits are reserved/not-used
//// FOD_RAM Register:
#define BQ51_FOD_RAM_ESR_EN_bits   0b10000000 // (R/W) ESR_ENABLE enables I2C based ESR in received power. 1=enable, 0=disable
#define BQ51_FOD_RAM_OFF_EN_bits   0b01000000 // (R/W) OFF_ENABLE enables I2C based offset power. 1=enable, 0=disable
#define BQ51_FOD_RAM_RO_bits       0b00111000 // (R/W) RO_FODx bits for setting the offset power. 3bit value, LSB = 39mW, value is added to received power message
#define BQ51_FOD_RAM_RS_bits       0b00000111 // (R/W) RS_FODx bits for setting ESR multiplier(?). 3bit value, 0=1=5=6=ESR, 2=ESR*2, 3=ESR*3, 4=ESR*4, 7=ESR*0.5
//// Mode Indicator Register (not on BQ51021):
#define BQ51_MODE_IND_ALIGN_bits   0b01000000 // (R) ALIGN Status. 1=Alignment_mode, 0=Normal_operation
#define BQ51_MODE_IND_MODE_bits    0b00000001 // (R) Mode bit, 1=PMA, 0=WPC(Qi)

//// all non-zero default config values (according to the datasheet) 
#define BQ51_VO_REG_default        0b00000001 // 500mV VO_REG target, normally results in 5V output (depending on resistors)
#define BQ51_IO_REG_default        0b00000111 // 100% IO_REG target, so it's just determined by the resistors used
#define BQ51_MAILBOX_default       0b10000000 // writing a 0 to the first bit would trigger package transmission

static const float BQ51_VOLT_SCALAR = 0.046; // (Volt) scalar for BQ51_VRECT_STATUS_RAM and BQ51_VOUT_STATUS_RAM
static const float BQ51_WATT_SCALAR = 0.039; // (Watt) scalar for BQ51_REC_PWR_STATUS_RAM and BQ51_FOD_RAM_RO_bits

//// i could've made an enum for VO_REG, but the math is so nice and linear, it just feels like a waste

enum BQ51_ILIM_ENUM : uint8_t { // 3bit value to determine I_ILIM current
  BQ51_ILIM_10 = 0, // 10.0%
  BQ51_ILIM_20 = 1, // 20.0%
  BQ51_ILIM_30 = 2, // 30.0%
  BQ51_ILIM_40 = 3, // 40.0%
  BQ51_ILIM_50 = 4, // 50.0%
  BQ51_ILIM_60 = 5, // 60.0%
  BQ51_ILIM_90 = 6, // 90.0%
  BQ51_ILIM_100 = 7 // 100.0%
};

enum BQ51_MAILBOX_ERR_ENUM : uint8_t { // 2bit value to indicate packet transfer success
  BQ51_MAILBOX_ERR_good       = 0, // No error in sending packet
  BQ51_MAILBOX_ERR_no_TX      = 1, // Error: no transmitter present
  BQ51_MAILBOX_ERR_bad_header = 2, // Illegal header found: packet will not be sent
  BQ51_MAILBOX_ERR_err_TBD    = 3  // Error: not defined yet

};

enum BQ51_RS_FOD_ENUM : uint8_t { // 3bit value to determine ESR multiplier(?) for Foreign Object Detection
  BQ51_RS_FOD_1x = 0, // ESR*1, NOTE: same as BQ51_RS_FOD_1x_alt_x
  BQ51_RS_FOD_1x_alt_1 = 1, // ESR*1, NOTE: same as BQ51_RS_FOD_1x
  BQ51_RS_FOD_2x = 2, // ESR*2
  BQ51_RS_FOD_3x = 3, // ESR*3
  BQ51_RS_FOD_4x = 4, // ESR*4
  BQ51_RS_FOD_1x_alt_2 = 5, // ESR*1, NOTE: same as BQ51_RS_FOD_1x
  BQ51_RS_FOD_1x_alt_3 = 6, // ESR*1, NOTE: same as BQ51_RS_FOD_1x
  BQ51_RS_FOD_05x = 7 // ESR*0.5
};


#include "_BQ51_thijs_base.h" // this file holds all the nitty-gritty low-level stuff (I2C implementations (platform optimizations))
/**
 * An I2C interfacing library for the BQ51 Qi receiver from Texas Instruments
 * 
 * features the option to use the Wire.h library, or optimized code for the following platforms:
 * atmega328p (direct register manipulation)
 * ESP32 (below HAL layer, but not lowest level)
 * MSP430 (through Energia(?) middle layer)
 * STM32 (through twi->HAL layers)
 */
class BQ51_thijs : public _BQ51_thijs_base
{
  public:
  using _BQ51_thijs_base::_BQ51_thijs_base;
  /*
  This class only contains the higher level functions.
   for the base functions, please refer to _BQ51_thijs_base.h
  here is a brief list of all the lower-level functions:
  - init()
  - requestReadBytes()
  - onlyReadBytes()
  - writeBytes()
  */
  //// the following functions are abstract enough that they'll work for either architecture

  //private:
  public:
  /**
   * (private) set only some bits in a register
   * @param newVal I2C Mailbox Register (involved in Qi packet transfer stuff)
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE _setBits(uint8_t registerToWrite, uint8_t newValue, uint8_t mask) {
    uint8_t temp;
    requestReadBytes(registerToWrite, &temp, 1); // read the register
    // if(registerToWrite == BQ51_MAILBOX) { temp |= BQ51_MAILBOX_SEND_bits; } // the 'send' bit will trigger a header transmission if a 0 is written to it. This prevents that
    if(registerToWrite == BQ51_MAILBOX) { temp &= (~BQ51_MAILBOX_FOD_S_bits); } // this bit must be kept 0, according to datasheet
    temp &= ~mask; //erase old value
    temp |= (newValue & mask); //insert new value
    return(writeBytes(registerToWrite, &temp, 1)); // write the register
  }

  public:

  /**
   * (just a macro) check whether an BQ51_ERR_RETURN_TYPE (which may be one of several different types) is fine or not 
   * @param err (bool or esp_err_t or i2c_status_e, see on defines at top)
   * @return whether the error is fine
   */
  bool _errGood(BQ51_ERR_RETURN_TYPE err) {
    #if defined(BQ51_return_esp_err_t)
      return(err == ESP_OK);
    #elif defined(BQ51_return_i2c_status_e)
      return(err == I2C_OK);
    #else
      return(err);
    #endif
  }

  ////////////////////////////////////////// set functions: ////////////////////////////////////////////////////
  
  /**
   * set the VO_REG target (Supply Current Register 1) bits directly
   * @param newVal 3 LSBits set VO_REG target from 450~800mV, VO_REG = 450+(bits*50) mV
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setVO_REG(uint8_t newVal) { newVal &= BQ51_VO_REG_bits; return(writeBytes(BQ51_VO_REG, &newVal, 1)); }
  /**
   * set the IO_REG target (Supply Current Register 2) bits directly
   * @param newVal 3 LSBits set I_ILIM current, 10,20,30,40,50,60, 90, 100 % ,see BQ51_ILIM_ENUM
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setIO_REG(BQ51_ILIM_ENUM newVal) { uint8_t temp=static_cast<uint8_t>(newVal)&BQ51_IO_REG_bits; return(writeBytes(BQ51_IO_REG, &temp, 1)); }

  /**
   * set the (whole) MAILBOX register (Note: writing 0 to first bit triggers custom header transmission, and 6th bit must be 0)
   * @param newVal I2C Mailbox Register (involved in Qi packet transfer stuff)
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setMAILBOX(uint8_t newVal) { return(writeBytes(BQ51_MAILBOX, &newVal, 1)); }
  /**
   * set USER_PKT_DONE to 0, which will send a packet with header in BQ51_USER_HEADER_RAM, and will read as 1 when packet has been sent
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setMAILBOX_SEND() { return(_setBits(BQ51_MAILBOX, 0, BQ51_MAILBOX_SEND_bits)); }
  /**
   * set the ALIGN Mailer bit in the MAILBOX register
   * @param newVal ALIGN Mailer will "enable alignment aid mode where the CEP = 0" (i think only PMA has an alignment mode???)
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setMAILBOX_ALIGN(bool newVal) { return(_setBits(BQ51_MAILBOX, newVal ? BQ51_MAILBOX_ALIGN_bits : 0, BQ51_MAILBOX_ALIGN_bits)); }

  /**
   * set the (whole) FOD RAM register
   * @param newVal Wireless Power Supply FOD RAM Register (involved in Foreign Object Detection)
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setFOD_RAM(uint8_t newVal) { return(writeBytes(BQ51_FOD_RAM, &newVal, 1)); }
  /**
   * set the ESR_ENABLE bit in the FOD RAM register
   * @param newVal ESR_ENABLE enables I2C based ESR in received power. 1=enable, 0=disable
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setFOD_ESR_EN(bool newVal) { return(_setBits(BQ51_FOD_RAM, newVal ? BQ51_FOD_RAM_ESR_EN_bits : 0, BQ51_FOD_RAM_ESR_EN_bits)); }
  /**
   * set the OFF_ENABLE bit in the FOD RAM register
   * @param newVal OFF_ENABLE enables I2C based offset power. 1=enable, 0=disable
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setFOD_OFF_EN(bool newVal) { return(_setBits(BQ51_FOD_RAM, newVal ? BQ51_FOD_RAM_OFF_EN_bits : 0, BQ51_FOD_RAM_OFF_EN_bits)); }
  /**
   * set the RO_FODx bits (3) in the FOD RAM register
   * @param newVal RO_FODx bits for setting the offset power. 3bit value, LSB = 39mW, value is added to received power message
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setFOD_RO(uint8_t newVal) { return(_setBits(BQ51_FOD_RAM, newVal << 3, BQ51_FOD_RAM_RO_bits)); }
  /**
   * set the RS_FODx bits (3) in the FOD RAM register (see BQ51_RS_FOD_ENUM for options)
   * @param newVal RS_FODx bits for setting ESR multiplier(?). 3bit value, 0=1=5=6=ESR, 2=ESR*2, 3=ESR*3, 4=ESR*4, 7=ESR*0.5
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setFOD_RS(BQ51_RS_FOD_ENUM newVal) { return(_setBits(BQ51_FOD_RAM, static_cast<uint8_t>(newVal), BQ51_FOD_RAM_RS_bits)); }

  /**
   * set the User Header RAM register
   * @param newVal Wireless Power User Header RAM Register (for custom Qi packets(?))
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setUSER_HEADER(uint8_t newVal) { return(writeBytes(BQ51_USER_HEADER_RAM, &newVal, 1)); }

  /**
   * set the Prop Packet Payload RAM Byte registers (all 4)
   * @param writeBuff 4-byte buffer for the Wireless Power Prop Packet Payload RAM Byte registers
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE setPACKET_PAYLOAD(uint8_t writeBuff[]) { return(writeBytes(BQ51_PACKET_PAYLOAD, writeBuff, 4)); }
  // /**
  //  * set the Prop Packet Payload RAM Byte registers (all 4) as a uint32_t
  //  * @param writeBuff 4-byte buffer for the Wireless Power Prop Packet Payload RAM Byte registers
  //  * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
  //  */
  // BQ51_ERR_RETURN_TYPE setPACKET_PAYLOAD_uint32_t(uint32_t newVal) { return(writeBytes(BQ51_PACKET_PAYLOAD, (uint8_t*)&newVal, 4)); }
  
  // /**
  //  * set the VO_OUT Status RAM register (not sure if this should actually have write-access)
  //  * @param newVal Wireless Power VO_OUT Status RAM Register (reads back V_OUT voltage, LSB = 46mV)  (TODO: check Write access??)
  //  * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
  //  */
  // BQ51_ERR_RETURN_TYPE setVOUT_STATUS_RAM(uint8_t newVal) { return(writeBytes(BQ51_VOUT_STATUS_RAM, &newVal, 1)); }
  // /**
  //  * set the REC PWR Byte Status RAM register (not sure if this should actually have write-access)
  //  * @param newVal Wireless Power REC PWR Byte Status RAM Register (reads back received power, LSB = 39mW)  (TODO: check Write access??)
  //  * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
  //  */
  // BQ51_ERR_RETURN_TYPE setREC_PWR_STATUS_RAM(uint8_t newVal) { return(writeBytes(BQ51_REC_PWR_STATUS_RAM, &newVal, 1)); }

  ////////////////////////////////////////// get functions: ////////////////////////////////////////////////////

  /**
   * retrieve VO_REG (Supply Current Register 1) bits
   * @param readBuff byte reference to put the result in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getVO_REG(uint8_t& readBuff) { return(requestReadBytes(BQ51_VO_REG, &readBuff, 1)); }
  /**
   * retrieve VO_REG (Supply Current Register 1) bits
   * @return 3 LSBits set VO_REG target from 450~800mV, VO_REG = 450+(bits*50) mV
   */
  uint8_t getVO_REG() { uint8_t retVal=0; getVO_REG(retVal); return(retVal); } // just a macro
  /**
   * retrieve VO_REG (Supply Current Register 1) in milliVolts (float)
   * @return VO_REG target in milliVolts, from 450~800mV, VO_REG = 450+(bits*50) mV
   */
  float getVO_REG_volt() { return(0.450 + (getVO_REG()*0.050)); } // just a macro
  /**
   * retrieve IO_REG (Supply Current Register 2) bits
   * @param readBuff enum (byte) reference to put the result in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getIO_REG(BQ51_ILIM_ENUM& readBuff) { return(requestReadBytes(BQ51_IO_REG, (uint8_t*)&readBuff, 1)); }
  /**
   * retrieve IO_REG (Supply Current Register 2) bits
   * @return 3 LSBits set I_ILIM current, 10,20,30,40,50,60, 90, 100 % (breaks pattern for for 0b_110 and 0b_111)
   */
  BQ51_ILIM_ENUM getIO_REG() { BQ51_ILIM_ENUM retVal; getIO_REG(retVal); return(retVal); } // just a macro
  /**
   * retrieve VO_REG (Supply Current Register 1) in milliVolts (float)
   * @return IO_REG current limit in percent, 10,20,30,40,50,60, 90 or 100 %
   */
  uint8_t getIO_REG_percent() { uint8_t IO_REG_bits=getIO_REG(); return((IO_REG_bits==7) ? 100 : ((IO_REG_bits==6) ? 90 : (10*(IO_REG_bits+1)))); } // just a macro

  /**
   * retrieve the (whole) MAILBOX register
   * @param readBuff byte reference to put the result in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getMAILBOX(uint8_t& readBuff) { return(requestReadBytes(BQ51_MAILBOX, &readBuff, 1)); }
  /**
   * retrieve (whole) MAILBOX register
   * @return I2C Mailbox Register (involved in Qi packet transfer stuff)
   */
  uint8_t getMAILBOX() { uint8_t retVal=0; getMAILBOX(retVal); return(retVal); } // just a macro
  /**
   * retrieve USER_PKT_DONE, which will read as 1 when packet has been sent
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  bool getMAILBOX_SEND() { return((getMAILBOX() & BQ51_MAILBOX_SEND_bits) != 0); }
  /**
   * retrieve the USER_PKT_ERR bits from the MAILBOX register (see BQ51_MAILBOX_ERR_ENUM)
   * @return USER_PKT_ERR bits indicate errors with packet sending: 0=no_err, 1=no_TX, 2=bad_header, 3=err_TBD
   */
  BQ51_MAILBOX_ERR_ENUM getMAILBOX_ERR() { return(static_cast<BQ51_MAILBOX_ERR_ENUM>((getMAILBOX() & BQ51_MAILBOX_ERR_bits)>>5)); }
  /**
   * retrieve the ALIGN Mailer bit from the MAILBOX register
   * @return ALIGN Mailer will "enable alignment aid mode where the CEP = 0" (i think only PMA has an alignment mode???)
   */
  bool getMAILBOX_ALIGN() { return((getMAILBOX() & BQ51_MAILBOX_ALIGN_bits) != 0); }

  /**
   * retrieve the (whole) FOD RAM register
   * @param readBuff byte reference to put the result in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getFOD_RAM(uint8_t& readBuff) { return(requestReadBytes(BQ51_FOD_RAM, &readBuff, 1)); }
  /**
   * retrieve the (whole) FOD RAM register
   * @return Wireless Power Supply FOD RAM Register (involved in Foreign Object Detection)
   */
  uint8_t getFOD_RAM() { uint8_t retVal=0; getFOD_RAM(retVal); return(retVal); } // just a macro
  /**
   * retrieve the getFOD_ESR_EN bit from the FOD RAM register
   * @return ESR_ENABLE enables I2C based ESR in received power. 1=enable, 0=disable
   */
  bool getFOD_ESR_EN() { return((getFOD_RAM() & BQ51_FOD_RAM_ESR_EN_bits) != 0); }
  /**
   * retrieve the getFOD_OFF_EN bit from the FOD RAM register
   * @return OFF_ENABLE enables I2C based offset power. 1=enable, 0=disable
   */
  bool getFOD_OFF_EN() { return((getFOD_RAM() & BQ51_FOD_RAM_OFF_EN_bits) != 0); }
  /**
   * retrieve the getFOD_RO bits from the FOD RAM register (see BQ51_RS_FOD_ENUM)
   * @return RO_FODx bits for setting the offset power. 3bit value, LSB = 39mW, value is added to received power message
   */
  uint8_t getFOD_RO() { return((getFOD_RAM() & BQ51_FOD_RAM_RO_bits)>>3); }
  /**
   * retrieve the getFOD_RO in milliWatts from the FOD RAM register (see BQ51_RS_FOD_ENUM)
   * @return RO_FODx in milliWatts for setting the offset power. 3bit value, LSB = 39mW, value is added to received power message
   */
  float getFOD_RO_mW() { return(getFOD_RO() * BQ51_WATT_SCALAR); }
  /**
   * retrieve the RS_FODx bits from the FOD RAM register (see BQ51_RS_FOD_ENUM)
   * @return RS_FODx bits for setting ESR multiplier(?). 3bit value, 0=1=5=6=ESR, 2=ESR*2, 3=ESR*3, 4=ESR*4, 7=ESR*0.5
   */
  BQ51_RS_FOD_ENUM getFOD_RS() { return(static_cast<BQ51_RS_FOD_ENUM>(getFOD_RAM() & BQ51_FOD_RAM_RS_bits)); }
  /**
   * retrieve the RS_FODx as a float (multiplier) from the FOD RAM register
   * @return ESR multiplier(?) calculated from: RS_FODx bits for setting ESR multiplier(?). 3bit value, 0=1=5=6=ESR, 2=ESR*2, 3=ESR*3, 4=ESR*4, 7=ESR*0.5
   */
  float getFOD_RS_mult() { uint8_t FOD_RS_bits=getFOD_RS(); return((FOD_RS_bits==0) ? 1 : ((FOD_RS_bits<5) ? FOD_RS_bits : ((FOD_RS_bits==7) ? 0.5 : 1))); }

  /**
   * retrieve the User Header RAM register
   * @param readBuff byte reference to put the result in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getUSER_HEADER(uint8_t& readBuff) { return(requestReadBytes(BQ51_USER_HEADER_RAM, &readBuff, 1)); }
  /**
   * retrieve the User Header RAM register
   * @return Wireless Power User Header RAM Register (for custom Qi packets(?))
   */
  uint8_t getUSER_HEADER() { uint8_t retVal=0; getUSER_HEADER(retVal); return(retVal); } // just a macro
  
  /**
   * retrieve the USER V_RECT Status RAM register
   * @param readBuff byte reference to put the result in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getVRECT(uint8_t& readBuff) { return(requestReadBytes(BQ51_VRECT_STATUS_RAM, &readBuff, 1)); }
  /**
   * retrieve the USER V_RECT Status RAM register
   * @return Wireless Power USER V_RECT Status RAM Register (reads back V_RECT voltage, LSB = 46mV)
   */
  uint8_t getVRECT() { uint8_t retVal=0; getVRECT(retVal); return(retVal); } // just a macro
  /**
   * retrieve the USER V_RECT Status RAM register in volts (float)
   * @return Wireless Power USER V_RECT Status RAM Register (reads back V_RECT voltage, LSB = 46mV)
   */
  float getVRECT_volt() { return(getVRECT() * BQ51_VOLT_SCALAR); } // just a macro
  /**
   * retrieve the VO_OUT Status RAM register
   * @param readBuff byte reference to put the result in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getVOUT(uint8_t& readBuff) { return(requestReadBytes(BQ51_VOUT_STATUS_RAM, &readBuff, 1)); }
  /**
   * retrieve the VO_OUT Status RAM register
   * @return Wireless Power VO_OUT Status RAM Register (reads back V_OUT voltage, LSB = 46mV)
   */
  uint8_t getVOUT() { uint8_t retVal=0; getVOUT(retVal); return(retVal); } // just a macro
  /**
   * retrieve the VO_OUT Status RAM register in volts (float)
   * @return Wireless Power VO_OUT Status RAM Register (reads back V_OUT voltage, LSB = 46mV)
   */
  float getVOUT_volt() { return(getVOUT() * BQ51_VOLT_SCALAR); } // just a macro
  /**
   * retrieve the REC PWR Byte Status RAM register
   * @param readBuff byte reference to put the result in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getREC_PWR(uint8_t& readBuff) { return(requestReadBytes(BQ51_REC_PWR_STATUS_RAM, &readBuff, 1)); }
  /**
   * retrieve the REC PWR Byte Status RAM register
   * @return Wireless Power REC PWR Byte Status RAM Register (reads back received power, LSB = 39mW)
   */
  uint8_t getREC_PWR() { uint8_t retVal=0; getREC_PWR(retVal); return(retVal); } // just a macro
  /**
   * retrieve the REC PWR Byte Status RAM register in watts (float)
   * @return Wireless Power REC PWR Byte Status RAM Register (reads back received power, LSB = 39mW)
   */
  float getREC_PWR_watt() { return(getREC_PWR() * BQ51_WATT_SCALAR); } // just a macro
  
  /**
   * retrieve the (whole) Mode Indicator register (not on BQ51021)
   * @param readBuff byte reference to put the result in
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getMODE_IND(uint8_t& readBuff) {
    if(isBQ51021) { BQ51debugPrint("BQ51021 doesn't have a Mode Indicator register!"); /* return(error); */ }
    return(requestReadBytes(BQ51_MODE_IND, &readBuff, 1));
  }
  /**
   * retrieve the (whole) Mode Indicator register (not on BQ51021)
   * @return Wireless Power Mode Indicator Register (indicates Qi or PMA, and Alignment-mode)
   */
  uint8_t getMODE_IND() {
    if(isBQ51021) { BQ51debugPrint("BQ51021 doesn't have a Mode Indicator register!"); return(0); }
    uint8_t retVal=0; getMODE_IND(retVal); return(retVal);
  } // just a macro
  /**
   * retrieve the ALIGN Status bit from the Mode Indicator register (not on BQ51021)
   * @return ALIGN Status. 1=Alignment_mode, 0=Normal_operation
   */
  bool getMODE_IND_ALIGN() {
    if(isBQ51021) { BQ51debugPrint("BQ51021 doesn't have a Mode Indicator register!"); return(0); }
    return((getMODE_IND() & BQ51_MODE_IND_ALIGN_bits) != 0);
  }
  /**
   * retrieve the Mode bit from the Mode Indicator register (not on BQ51021)
   * @return Mode bit, 1=PMA, 0=WPC(Qi)
   */
  bool getMODE() {
    if(isBQ51021) { BQ51debugPrint("BQ51021 doesn't have a Mode Indicator register!"); return(0); } // NOTE: returns WPC(Qi), which is correct
    return((getMODE_IND() & BQ51_MODE_IND_MODE_bits) != 0);
  }
  
  /**
   * retrieve all Prop Packet Payload RAM Byte registers (all 4)
   * @param readBuff 4-byte buffer for the Wireless Power Prop Packet Payload RAM Byte registers
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getPACKET_PAYLOAD(uint8_t readBuff[]) { return(requestReadBytes(BQ51_PACKET_PAYLOAD, readBuff, 4)); }
  // /**
  //  * set the Prop Packet Payload RAM Byte registers (all 4) as a uint32_t
  //  * @param writeBuff 4-byte buffer for the Wireless Power Prop Packet Payload RAM Byte registers
  //  * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
  //  */
  // uint32_t getPACKET_PAYLOAD_uint32_t(uint32_t newVal) { uint32_t retVal; return(writeBytes(BQ51_PACKET_PAYLOAD, (uint8_t*)&retVal, 4)); }

  /**
   * retrieve the Wireless Power Readback Register (unique ID for each device, programmed at factory) (not on BQ51021(?))
   * @param readBuff 6-byte (BQ51_RXID_size) buffer for the Wireless Power Readback Register (unique ID for each device, programmed at factory)
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote/read successfully
   */
  BQ51_ERR_RETURN_TYPE getRXID(uint8_t readBuff[]) {
    if(isBQ51021) { BQ51debugPrint("BQ51021 doesn't have RXID registers(?)"); } // it's not listed in the datasheet, but TI is not always 1000% diligent
    return(requestReadBytes(BQ51_RXID_READBACK, readBuff, BQ51_RXID_size));
  }

  /**
   * Only checks for I2C errors. Use poweredCheck to see if V_RECT > V_UVLO (enabling most of the registers)
   * @return true if reading was successful and resolution bits were as expected
   */
  bool connectionCheck() {
    uint8_t readBuff[2];
    BQ51_ERR_RETURN_TYPE err = requestReadBytes(BQ51_VO_REG, readBuff, 2); // attempt to read some stuff, to see if the device responds at all
    if(!_errGood(err)) { return(false); }
    return(((readBuff[0] & (~BQ51_VO_REG_bits))==0) && ((readBuff[1] & (~BQ51_IO_REG_bits))==0)); // only some bits in these registers are actually used. The reserved bits should read 0
  }

  /**
   * Attempts to check whether RXID is all 1's, to see if those registers are active. This also checks whether V_RECT > V_UVLO (which should mean a TX is providing power)
   * @return V_RECT as a float, because why not (but use getV_RECT() for a much faster version)
   */
  float poweredCheck() {
    uint8_t readBuff[BQ51_RXID_size]; // sized for the RXID, but also used to hold V_RECT byte
    BQ51_ERR_RETURN_TYPE err = requestReadBytes(BQ51_RXID_READBACK, readBuff, BQ51_RXID_size);
    if(!_errGood(err)) { return(-2.0); } // if the I2C interaction failed, the device may not even be connected.
    bool allOnes = true; for(uint8_t i=0; i<BQ51_RXID_size; i++) { allOnes &= (readBuff[i] == 0xFF); } // if any of the bytes is not all 1's, set the bool to false
    if(allOnes) { return(-1.0); } // if the RXID read as all 1's, then those registers are likely reset/unpowered, because V_RECT < V_UVLO
    err = requestReadBytes(BQ51_VRECT_STATUS_RAM, readBuff, 1);
    if(!_errGood(err)) { return(-2.0); } // there is no good reason for the I2C interaction to fail on the second read, but might as well check
    const static uint8_t UVLO = 2.9 / BQ51_VOLT_SCALAR; // the UnderVoltage LockOut is max 2.9V, according to the TI BQ51222 datasheet
    if(readBuff[0] < UVLO) { return(-1.0); } // if the rectifier voltage read is below UVLO, it should not be on
    return(readBuff[0] * BQ51_VOLT_SCALAR);
  }

  // /**
  //  * print out all the configuration values (just for debugging)
  //  * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
  //  */
  // BQ51_ERR_RETURN_TYPE printConfig() { // prints contents of CONF register (somewhat efficiently)
  //   uint8_t readBuff[6]; // instead of calling all the functions above, attempt to bundle reads
  //   BQ51_ERR_RETURN_TYPE readSuccess = requestReadBytes(BQ51_CONF, readBuff, 2);
  //   if(_errGood(readSuccess))
  //    {
  //     //Serial.println("printing config: ");
  //   } else {
  //     Serial.println("BQ51 failed to do printConfig thingy!");
  //   }
  //   return(readSuccess);
  // }

  /**
   * write the defualt value to VO_REG (resetting the output voltage target to the one set by the resistors)
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE resetVO_REG() { static uint8_t VO_def=BQ51_VO_REG_default; return(writeBytes(BQ51_VO_REG, &VO_def, 1)); } // write the default value (according to datasheet) to VO_REG register
  /**
   * write the defualt value to IO_REG (resetting the I_ILIM current limit to 100% of the value set by the resistors)
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE resetIO_REG() { static uint8_t IO_def=BQ51_IO_REG_default; return(writeBytes(BQ51_IO_REG, &IO_def, 1)); } // write the default value (according to datasheet) to IO_REG register
  /**
   * write the defualt value to MAILBOX register
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE resetMAILBOX() { static uint8_t MB_def=BQ51_MAILBOX_default; return(writeBytes(BQ51_MAILBOX, &MB_def, 1)); } // write the default value (according to datasheet) to MAILBOX register
  /**
   * write default values to all (write-access) registers, resetting the output voltage, FOD adjustments and custom proprietary packets/headers
   * @return (bool or esp_err_t or i2c_status_e, see on defines at top) whether it wrote successfully
   */
  BQ51_ERR_RETURN_TYPE resetAllRegisters() {
    static uint8_t defaults[3][4] = {{BQ51_VO_REG_default,BQ51_IO_REG_default,(0),(0)},{BQ51_MAILBOX_default,0,0,(0)},{0,0,0,0}};
    return(writeBytes(BQ51_VO_REG, defaults[0], 2)); // reset BQ51_VO_REG and BQ51_IO_REG
    return(writeBytes(BQ51_MAILBOX, defaults[1], 3)); // reset BQ51_MAILBOX, BQ51_FOD_RAM and BQ51_USER_HEADER_RAM
    return(writeBytes(BQ51_PACKET_PAYLOAD, defaults[2], 4)); // reset all 4 bytes of BQ51_PACKET_PAYLOAD
  }
};

#endif  // BQ51_thijs_h