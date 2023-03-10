/*

this is a test of the Qi receiver: bq51222
The library should also work for the bq51221 and bq51021, but I haven't tested those

*/

#include <Arduino.h>


// #define BQ51_useWireLib  // force the use of Wire.h (instead of platform-optimized code (if available))

#define BQ51debugPrint(x)  Serial.println(x)    //you can undefine these printing functions no problem, they are only for printing I2C errors
//#define BQ51debugPrint(x)  log_d(x)  // ESP32 style logging

#include <BQ51_thijs.h>

BQ51_thijs BQ51;


#ifdef ARDUINO_ARCH_ESP32  // on the ESP32, almost any pin can become an I2C pin
  const uint8_t BQ51_SDApin = 26; // 'defualt' is 21 (but this is just some random value Arduino decided.)
  const uint8_t BQ51_SCLpin = 27; // 'defualt' is 22 
#endif
#ifdef ARDUINO_ARCH_STM32   // on the STM32, each I2C peripheral has several pin options
  const uint8_t BQ51_SDApin = SDA; // default pin, on the STM32WB55 (nucleo_wb55rg_p) that's pin PB9
  const uint8_t BQ51_SCLpin = SCL; // default pin, on the STM32WB55 (nucleo_wb55rg_p) that's pin PB8
  /* Here is a handy little table of I2C pins on the STM32WB55 (nucleo_wb55rg_p):
      I2C1: SDA: PA10, PB7, PB9
            SCL: PA9, PB6, PB8
      I2C3: SDA: PB4, PB11, PB14, PC1
            SCL: PA7 PB10, PB13, PC0      */
#endif


void setup() 
{


  Serial.setRx(PA10);  Serial.setTx(PA9); // PCB R01


  Serial.begin(115200);  delay(50); Serial.println();
  #ifdef BQ51_useWireLib // the slow (but pretty universally compatible) way
    BQ51.init(100000); // NOTE: it's up to the user to find a frequency that works well.
  #elif defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__) // TODO: test 328p processor defines! (also, this code may be functional on other AVR hw as well?)
    pinMode(SDA, INPUT_PULLUP); //A4    NOT SURE IF THIS INITIALIZATION IS BEST (external pullups are strongly recommended anyways)
    pinMode(SCL, INPUT_PULLUP); //A5
    BQ51.init(100000); // your average atmega328p should do 800kHz, but the BQ51 is 1kHz~400kHz limited (unless! you use some specific I2C commands to unlock the full 2.8Mhz range)
  #elif defined(ARDUINO_ARCH_ESP32)
//    pinMode(BQ51_SDApin, INPUT_PULLUP); //not needed, as twoWireSetup() does the pullup stuff for you
//    pinMode(BQ51_SCLpin, INPUT_PULLUP);
    esp_err_t initErr = BQ51.init(100000, BQ51_SDApin, BQ51_SCLpin, 0); //on the ESP32 (almost) any pins can be I2C pins
    if(initErr != ESP_OK) { Serial.print("I2C init fail. error:"); Serial.println(esp_err_to_name(initErr)); Serial.println("while(1){}..."); while(1) {} }
    //note: on the ESP32 the actual I2C frequency is lower than the set frequency (by like 20~40% depending on pullup resistors, 1.5kOhm gets you about 800kHz)
  #elif defined(__MSP430FR2355__) //TBD: determine other MSP430 compatibility: || defined(ENERGIA_ARCH_MSP430) || defined(__MSP430__)
    // not sure if MSP430 needs pinMode setting for I2C, but it seems to work without just fine.
    BQ51.init(100000); // TODO: test what the limit of this poor microcontroller are ;)
    delay(50);
  #elif defined(ARDUINO_ARCH_STM32)
    // not sure if STM32 needs pinMode setting for I2C
    BQ51.init(100000, SDA, SCL, false); // TODO: test what the limits of this poor microcontroller are ;)
  #else
    #error("should never happen, BQ51_useWireLib should have automatically been selected if your platform isn't one of the optimized ones")
  #endif
  
  if(!BQ51.connectionCheck()) { Serial.println("BQ51 connection check failed!"); while(1);}
  float V_RECT_test = BQ51.poweredCheck();
  if(V_RECT_test < 0.0) { Serial.println("BQ51 not powered by wireless TX!"); }
  Serial.print("V_RECT (poweredCheck()): "); Serial.println(V_RECT_test);

  Serial.print("getVO_REG: 0b"); Serial.print(BQ51.getVO_REG(), BIN); Serial.print("  default: 0b"); Serial.println(BQ51_VO_REG_default, BIN);
  Serial.print("getVO_REG_volt: "); Serial.print(BQ51.getVO_REG_volt()); Serial.println("V  default: 0.50V");
  Serial.print("getIO_REG: 0b"); Serial.print(BQ51.getIO_REG(), BIN); Serial.print("  default: 0b"); Serial.println(BQ51_IO_REG_default, BIN);
  Serial.print("getIO_REG_percent: "); Serial.print(BQ51.getIO_REG_percent()); Serial.println("%  default: 100%");
  Serial.println(); // spacer
  Serial.print("getMODE_IND: 0b"); Serial.println(BQ51.getMODE_IND(), BIN);
  Serial.print("getMODE_IND_ALIGN: "); Serial.println(BQ51.getMODE_IND_ALIGN());
  Serial.print("getMODE: "); Serial.println(BQ51.getMODE());
  Serial.println();
  uint8_t readBuff[max(BQ51_RXID_size, 4)];
  BQ51.getPACKET_PAYLOAD(readBuff);
  Serial.print("getPACKET_PAYLOAD: "); for(uint8_t i=0; i<4; i++) { Serial.print("0x"); Serial.print(readBuff[i], HEX); Serial.write('\t'); } Serial.println();
  BQ51.getRXID(readBuff);
  Serial.print("getRXID: "); for(uint8_t i=0; i<BQ51_RXID_size; i++) { Serial.print("0x"); Serial.print(readBuff[i], HEX); Serial.write('\t'); } Serial.println();
  Serial.println();

  BQ51.setVO_REG(0); // set VOUT target to 4.5V (VO_REG to 450mV)
  Serial.print("updated getVO_REG_volt: "); Serial.println(BQ51.getVO_REG_volt());
  delay(250); // give the LDO some time to reach the new target voltage
  // Serial.print("getVRECT: "); Serial.println(BQ51.getVRECT());
  Serial.print("getVRECT_volt: "); Serial.println(BQ51.getVRECT_volt());
  // Serial.print("getVOUT: "); Serial.println(BQ51.getVOUT());
  Serial.print("getVOUT_volt: "); Serial.println(BQ51.getVOUT_volt());
  Serial.println();

  delay(500); // give the BQ51 time to get a new conversion done, now that Extended Mode is disabled
}

void loop()
{
  while(Serial.available()) {
    uint8_t recv = Serial.read();
    if((recv >= '0') && (recv <= '7')) {
      recv -= '0'; // ASCII to numeric
      // Serial.print("setting to: "); Serial.print(recv); Serial.print(" == "); Serial.println(4.5 + (0.5*recv));
      BQ51.setVO_REG(recv);
      Serial.print("confirm: "); Serial.println(BQ51.getVO_REG_volt()*10.0);
    } else { Serial.println("invalid input!"); }
  }
  Serial.print("VRECT[v], VOUT[v], REC_PWR[W]:\t");
  Serial.print(BQ51.getVRECT_volt()); Serial.print('\t');
  Serial.print(BQ51.getVOUT_volt()); Serial.print('\t');
  Serial.println(BQ51.getREC_PWR_watt());
  delay(500);
}