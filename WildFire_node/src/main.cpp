#include <Arduino.h>
#include "DHT.h"
//----------CPU0 Handle----------//

TaskHandle_t readDHT_handle = NULL;
TaskHandle_t readSmoke_handle = NULL;
TaskHandle_t readSwitch_handle = NULL;
TaskHandle_t readBatt_handle = NULL;
TaskHandle_t sentToGw_handle = NULL;

//----------CPU1 Handle----------//

TaskHandle_t receiveFromGw_handle = NULL;
TaskHandle_t ctrlLed_handle = NULL;

union FloatToByte
{
  float asFloat;
  uint8_t asByte[4];
};

FloatToByte temp,humid;
uint8_t util_byte = 0;

#define sw_bit 0
#define smoke_bit 1
#define led_bit 2


DHT dht(15,DHT11);

void readDHT(void *pvParam)
{
  temp.asFloat = dht.readTemperature();
}

void setup()
{
  
  Serial.begin(9600);
  dht.begin();

  xTaskCreatePinnedToCore(readDHT, "readDHT", 2000, NULL, 1, &readDHT_handle, 0);

}

void loop()
{
}
