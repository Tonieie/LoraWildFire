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
TaskHandle_t sentToGw_handle = NULL;

//----------CPU1 Handle----------//
TaskHandle_t receiveFromGw_handle = NULL;
TaskHandle_t ctrlLed_handle = NULL;

//----------Params----------//
#define DHTPIN 15
DHT dht(DHTPIN, DHT11);

union FloatToByte
{
  float asFloat;
  uint8_t asByte[4];
};

FloatToByte temp, humid;
uint8_t util_byte = 0;

#define node_number 1
#define sw_bit 0
#define smoke_bit 1
#define batt_bit 2
#define led_bit 3

#define SW_pin 19

boolean sent_flag = false;

//----------Gateway - Node functions----------//
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

void onReceive(int packetSize)
{

  static uint8_t buffer[8];
  uint8_t index = 0;
  while (LoRa.available())
  {
    buffer[index] = (uint8_t)LoRa.read();

    if (buffer[index - 3] == 'r' && buffer[index - 2] == 'e' && buffer[index - 1] == 'q' && buffer[index] == ('0' + node_number))
    {
      sent_flag = true;
    }
  }
}

void onTxDone()
{
  LoRa_rxMode();
}

//----------Sensor Reading----------//
void readBatt()
{
  digitalWrite(18, LOW);
  float v_batt = (float)analogRead(4);
  v_batt = v_batt / 4095.0f * 2 * 3.3f;

  if (v_batt <= 3.7f)
    setBit(&util_byte, batt_bit);
  else
    clearBit(&util_byte, batt_bit);
}

void readDHT()
{
  temp.asFloat = dht.readTemperature();
  humid.asFloat = dht.readHumidity();
}

void readCritical()
{
  if (digitalRead(SW_pin) == LOW)
  {
    setBit(&util_byte, sw_bit);
    sent_flag = true;
  }
  else
    clearBit(&util_byte, sw_bit);

  //**********Read Smoke**********//



}

uint8_t setBit(uint8_t *data, uint8_t bit)
{
  return *data |= (1 << bit);
}

uint8_t clearBit(uint8_t *data, uint8_t bit)
{
  return *data &= ~(1 << bit);
}

//----------CPU0 Task----------//
void sentToGw(void *pvParam)
{
  while (1)                                                                                                   
  {

    readCritical();

    if (sent_flag == true)
    {
      readDHT();
      readBatt();
      uint8_t checksum = 0;
      uint8_t payload[] = {'n', '0' + node_number,
                           temp.asByte[0], temp.asByte[1], temp.asByte[2], temp.asByte[3],
                           humid.asByte[0], humid.asByte[1], humid.asByte[2], humid.asByte[3],
                           util_byte};
      LoRa_txMode();
      LoRa.beginPacket();
      for (int i = 0; i < sizeof(payload); i++)
      {
        LoRa.write(payload[i]);
        checksum += payload[i];
      }
      checksum = ~(checksum) + 1;
      LoRa.write(checksum);
      LoRa.endPacket(true);

      sent_flag = false;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
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

  xTaskCreatePinnedToCore(sentToGw, "sentToGw", 2000, NULL, 1, &sentToGw_handle, 0);

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
  LoRa_rxMode();
}

void loop()
{
}
