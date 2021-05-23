#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <LoRa.h>

#define SCK 14
#define MISO 12
#define MOSI 13
#define SS 27
#define RST 26
#define DIO0 25

//----------CPU0 Handle----------//
TaskHandle_t readDHT_handle = NULL;
TaskHandle_t readSmoke_handle = NULL;
TaskHandle_t readSwitch_handle = NULL;
TaskHandle_t readBatt_handle = NULL;
TaskHandle_t sentToGw_handle = NULL;

//----------CPU1 Handle----------//
TaskHandle_t receiveFromGw_handle = NULL;
TaskHandle_t ctrlLed_handle = NULL;

//----------Params----------//
union FloatToByte
{
  float asFloat;
  uint8_t asByte[4];
};

FloatToByte temp, humid;
uint8_t util_byte = 0;

#define sw_bit 0
#define smoke_bit 1
#define led_bit 2

//----------CPU0 Task----------//
DHT dht(15, DHT11);

void readDHT(void *pvParam)
{
  while (1)
  {
    temp.asFloat = dht.readTemperature();
    humid.asFloat = dht.readHumidity();

    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

void LoRa_rxMode()
{
  LoRa.enableInvertIQ(); // active invert I and Q signals
  LoRa.receive();        // set receive mode
}

void LoRa_txMode()
{
  LoRa.idle();            // set standby mode
  LoRa.disableInvertIQ(); // normal mode
}

void sentToGw(void *pvParam)
{
  while (1)
  {
    LoRa_txMode();
    LoRa.beginPacket();
    LoRa.print("no");
    LoRa.write(temp.asByte,4);
    LoRa.write(humid.asByte,4);
    LoRa.endPacket(true);

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void onReceive(int packetSize)
{
  String message = "";

  while (LoRa.available())
  {
    message += (char)LoRa.read();
  }

  Serial.print("Node Receive: ");
  Serial.println(message);
}

void onTxDone()
{
  Serial.println("TxDone");
  LoRa_rxMode();
}

void setup()
{

  Serial.begin(9600);

  //setup LoRa module (sx1276) with frequency 923.2 MHz
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(923.2E6))
  {
    Serial.println("Starting LoRa failed!");
    while (1)
      ;
  }

  dht.begin();

  xTaskCreatePinnedToCore(readDHT, "readDHT", 2000, NULL, 1, &readDHT_handle, 0);
  xTaskCreatePinnedToCore(sentToGw, "sentToGw", 2000, NULL, 1, &sentToGw_handle, 0);

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
  LoRa_rxMode();
}

void loop()
{
}
