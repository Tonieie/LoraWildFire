#include <Arduino.h>
#include <LoRa.h>
#include <WiFi.h>
#include <IOXhop_FirebaseESP32.h>

//----------Pin define----------//
#define SCK 14
#define MISO 12
#define MOSI 13
#define SS 27
#define RST 26
#define DIO0 25

//----------Firebase & WiFi----------//
#define WIFI_SSID "tonieie"
#define WIFI_PASSWORD "78787862x"

#define FIREBASE_HOST "lorawildfire-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "FPtMYEMGMHvsWgNSfsVG9ybfW8FMtRtW2dIywlOn"

//----------CPU0 Handle----------//
TaskHandle_t firebase_taskhandle = NULL;

//----------LoRa Tasks----------//
void LoRa_rxMode()
{
  LoRa.disableInvertIQ(); // normal mode
  LoRa.receive();         // set receive mode
}

void LoRa_txMode()
{
  LoRa.idle();           // set standby mode
  LoRa.enableInvertIQ(); // active invert I and Q signals
}

void onReceive(int packetSize)
{
  static uint8_t buffer[20];
  uint8_t index = 0;
  while (LoRa.available())
  {
    buffer[index] = (uint8_t)LoRa.read();
    if (index == 0 && buffer[0] != 'n')
      index = 0;
    if (buffer[index - 9] == 'n' && buffer[index - 8] == 'o')
    {
      temp.asByte[0] = buffer[index - 7];
      temp.asByte[1] = buffer[index - 6];
      temp.asByte[2] = buffer[index - 5];
      temp.asByte[3] = buffer[index - 4];

      humid.asByte[0] = buffer[index - 3];
      humid.asByte[1] = buffer[index - 2];
      humid.asByte[2] = buffer[index - 1];
      humid.asByte[3] = buffer[index];

      Serial.print("temp : ");
      Serial.print(temp.asFloat);
      Serial.print(" humid : ");
      Serial.println(humid.asFloat);
      index = 0;
    }

    index >= 20 ? index = 0 : index++;
  }
}

void onTxDone()
{
  Serial.println("TxDone");
  LoRa_rxMode();
}

//----------CPU0 Tasks----------//
void firebase_task(void *pvParam)
{
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }

  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  for (;;)
  {
    Firebase.setFloat("node1/temp", temp.asFloat);
    Firebase.setFloat("node1/humid", humid.asFloat);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

//----------Global Variable----------//
union FloatToByte
{
  float asFloat;
  uint8_t asByte[4];
};
FloatToByte temp, humid;

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

  xTaskCreatePinnedToCore(firebase_task, "firebase_task", 10000, NULL, 0, &firebase_taskhandle, 0);

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
  LoRa_rxMode();
}

void loop()
{
  // put your main code here, to run repeatedly:
}