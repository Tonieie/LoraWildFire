#include <Arduino.h>
#include <LoRa.h>
#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <IOXhop_FirebaseESP32.h>
#include <time.h>

//----------Pin define----------//
#define SCK 14
#define MISO 12
#define MOSI 13
#define SS 27
#define RST 26
#define DIO0 25

#define DHTPIN 15
#define SW_pin 19

//---------Global Variables----------//
union FloatToByte
{
  float asFloat;
  uint8_t asByte[4];
};
FloatToByte temp[3], humid[3];

DHT dht(DHTPIN, DHT11);

boolean flag_batt[3] = {false, false, false};
boolean flag_switch[3] = {false, false, false};
boolean flag_smoke[3] = {false, false, false};
boolean flag_ack[3] = {false, false, false};

#define sw_bit 0
#define smoke_bit 1
#define batt_bit 2

//----------Firebase & WiFi----------//
#define WIFI_SSID "NKH1449_2.4G"
#define WIFI_PASSWORD "0851404547"

#define FIREBASE_HOST "lorawildfire-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "FPtMYEMGMHvsWgNSfsVG9ybfW8FMtRtW2dIywlOn"

//----------CPU0 Handle----------//
TaskHandle_t cpu0_taskhandle = NULL;
TaskHandle_t req_taskhandle = NULL;

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

void sentToNd(char node_num, uint8_t LED_Byte) // r e q node led
{
  uint8_t checksum = 0;
  uint8_t payload[] = {'r', 'e', 'q', node_num, LED_Byte};
  LoRa_txMode();
  LoRa.beginPacket();
  for (int i = 0; i < sizeof(payload); i++)
    LoRa.write(payload[i]);
  LoRa.endPacket(true);
}

void onReceive(int packetSize)
{
  static uint8_t buffer[24];
  uint8_t index = 0;
  uint8_t checksum = 0;
  while (LoRa.available())
  {
    buffer[index] = (uint8_t)LoRa.read();
    if (index == 0 && buffer[0] != 'n')
      index = 0;

    if (buffer[index - 11] == 'n' && (buffer[index - 10] == '1' || buffer[index - 10] == '2'))
    {
      uint8_t node_num = buffer[index - 10] - '0';

      for (int i = 11; i >= 0; i--)
        checksum += buffer[index - i];

      if (!checksum)
      {
        temp[node_num].asByte[0] = buffer[index - 9];
        temp[node_num].asByte[1] = buffer[index - 8];
        temp[node_num].asByte[2] = buffer[index - 7];
        temp[node_num].asByte[3] = buffer[index - 6];

        humid[node_num].asByte[0] = buffer[index - 5];
        humid[node_num].asByte[1] = buffer[index - 4];
        humid[node_num].asByte[2] = buffer[index - 3];
        humid[node_num].asByte[3] = buffer[index - 2];

        flag_batt[node_num] = (buffer[index - 1] >> batt_bit) & 0x01;
        flag_smoke[node_num] = (buffer[index - 1] >> smoke_bit) & 0x01;
        flag_switch[node_num] = (buffer[index - 1] >> sw_bit) & 0x01;
        flag_ack[node_num] = true;
        index = 0;
      }
    }

    index >= 23 ? index = 0 : index++;
  }
}

void onTxDone()
{
  // Serial.println("TxDone");
  LoRa_rxMode();
}

//----------Sensor Reading----------//
void readBatt()
{
  digitalWrite(18, LOW);
  float v_batt = (float)analogRead(4);
  v_batt = v_batt / 4095.0f * 2 * 3.3f;

  if (v_batt <= 3.7f)
    flag_batt[0] = true;
  else
    flag_batt[0] = false;
}

void readDHT()
{
  temp[0].asFloat = dht.readTemperature();
  humid[0].asFloat = dht.readHumidity();
}

void readCritical()
{
  if (digitalRead(SW_pin) == LOW)
  {
    flag_switch[0] = true;
  }
  else
    flag_switch[0] = false;

  //**********Read Smoke**********//
}

//----------CPU0 Tasks----------//
void cpu0_task(void *pvParam)
{

  while (1)
  {
    // Firebase.setFloat("node1/temp", temp.asFloat);
    // Firebase.setFloat("node1/humid", humid.asFloat);
    vTaskDelay(1000);
  }
}

//----------CPU1 Tasks----------//
void req_task(void *pvParam)
{
  struct tm current_time;
  while (1)
  {
    Serial.println("in task");
    if (!getLocalTime(&current_time))
    {
      Serial.println("Failed to obtain time");
    }

    if (current_time.tm_min == 34)
    {
      // if (current_time.tm_hour % 4 == 0)
      // {
      //   sentToNd('1', 0);
      //   sentToNd('2', 0);
      // }
      // else
      if (current_time.tm_hour == 21)
      {
        flag_ack[0] = true;
      }

      vTaskDelay(pdMS_TO_TICKS(60000)); //pause task for 55 mins
    }
    if (flag_ack[0] == true)
    {
      if(flag_ack[1] == false && flag_ack[2] == false)
        sentToNd('1', 0xFF);
      else if(flag_ack[1] == true && flag_ack[2] == false)
        sentToNd('2', 0xFF);
      else if(flag_ack[1] == true && flag_ack[2] == true){
        flag_ack[0] = false;
        flag_ack[1] = false;
        flag_ack[2] = false;
        Serial.println("ieei send");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup()
{

  Serial.begin(57600);
  // while(!Serial);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  configTime(7 * 60 * 60, 0, "pool.ntp.org");
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);

  //setup LoRa module (sx1276) with frequency 923.2 MHz
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(923.2E6))
  {
    Serial.println("Starting LoRa failed!");
    while (1)
      ;
  }

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
  LoRa_rxMode();

  // xTaskCreatePinnedToCore(cpu0_task, "cpu0_task", 10000, NULL, 0, &cpu0_taskhandle, 0);
  xTaskCreatePinnedToCore(req_task, "req_task", 3000, NULL, 1, &req_taskhandle, 1);
  pinMode(SW_pin, INPUT);
  Serial.println("in");
}

void loop()
{
  // put your main code here, to run repeatedly:
}