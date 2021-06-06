#include <Arduino.h>
#include <LoRa.h>
#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <BlynkSimpleEsp32.h>

#include <FirebaseESP32.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

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
boolean flag_net = false;
boolean flag_danger = false;
boolean flag_isOK = false;

#define sw_bit 0
#define smoke_bit 1
#define batt_bit 2

//----------Firebase & WiFi----------//
#define WIFI_SSID "tonieie"
#define WIFI_PASSWORD "78787862x"

#define API_KEY "AIzaSyDvs4yCvhcPnYBcGcL8svJMnOBWrts9vmg"
#define DATABASE_URL "lorawildfire-default-rtdb.asia-southeast1.firebasedatabase.app"
#define USER_EMAIL "s6201011620127@email.kmutnb.ac.th"
#define USER_PASSWORD "78787862x"

#define Blynk_TOKEN "6IcpR9OcnSMVpMRdY0VOIQ1CGN-eyHRH"

#define MAXIMUM_TRY 5

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

FirebaseJsonArray data_arr[3];
uint8_t firebase_index;

struct tm current_time;

//----------Task Handle----------//
TaskHandle_t req_taskhandle = NULL;
TaskHandle_t sentInternet_taskhandle = NULL;
TaskHandle_t sentToFirebase_taskhandle = NULL;
TaskHandle_t sentToLine_taskhandle = NULL;

TaskHandle_t blynk_taskhandle = NULL;
TaskHandle_t sentToBlynk_taskhandle = NULL;
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
        if (flag_smoke[node_num] || flag_switch[node_num])
          flag_danger = true;
        flag_ack[node_num] = true;
        index = 0;
      }
    }

    index >= 23 ? index = 0 : index++;
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
void blynk_task(void *pvParam)
{
  //Blynk init
  Blynk.begin(Blynk_TOKEN, WIFI_SSID, WIFI_PASSWORD, IPAddress(43, 229, 135, 169), 8080);
  while (1)
  {
    Blynk.run();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void sentToBlynk_task(void *pvParam)
{
  Blynk.virtualWrite(V2, temp[0].asFloat);
  Blynk.virtualWrite(V3, humid[0].asFloat);
  Blynk.virtualWrite(V4, temp[1].asFloat);
  Blynk.virtualWrite(V5, humid[1].asFloat);
  Blynk.virtualWrite(V6, temp[2].asFloat);
  Blynk.virtualWrite(V7, humid[2].asFloat);

  Serial.println("sent to blynk");

  vTaskSuspend(sentToBlynk_taskhandle);
}
//----------CPU1 Tasks----------//
void req_task(void *pvParam)
{
  while (1)
  {

    uint8_t try_count = 0;
    while (flag_ack[1] == false && try_count < MAXIMUM_TRY)
    {
      try_count++;
      sentToNd('1', 0xFF);
      Serial.println("try node1 : " + String(try_count));
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
    try_count = 0;
    while (flag_ack[2] == false && try_count < MAXIMUM_TRY)
    {
      try_count++;
      sentToNd('2', 0xFF);
      Serial.println("try node2 : " + String(try_count));
      vTaskDelay(pdMS_TO_TICKS(10000));
    }

    flag_net = true;
    vTaskSuspend(req_taskhandle);
  }
}

void sentToFirebase_task(void *pvParam)
{
  while (1)
  {
  
  static boolean firsttime = true;
  if (firsttime)
  {
    //Firebase Init
    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.database_url = DATABASE_URL;
    config.token_status_callback = tokenStatusCallback;
    Firebase.begin(&config, &auth);
    vTaskDelay(pdMS_TO_TICKS(10000));

    Firebase.getArray(fbdo, "/gateway");
    data_arr[0] = fbdo.jsonArray();
    Firebase.getArray(fbdo, "/node1");
    data_arr[1] = fbdo.jsonArray();
    Firebase.getArray(fbdo, "/node2");
    data_arr[2] = fbdo.jsonArray();
    firebase_index = data_arr[0].size();

    firsttime = false;
  }

  struct tm time_now;
  getLocalTime(&time_now);
  char timeBuff[50]; //50 chars should be enough
  strftime(timeBuff, sizeof(timeBuff), "%A %B %d %Y %H:%M:%S", &time_now);
  String timeBuff2(timeBuff);

  for (uint8_t i = 0; i < 3; i++)
  {
    data_arr[i].set("[" + String(firebase_index) + "]/temp", temp[i].asFloat);
    data_arr[i].set("[" + String(firebase_index) + "]/humid", humid[i].asFloat);
    data_arr[i].set("[" + String(firebase_index) + "]/batt", flag_batt[i]);
    data_arr[i].set("[" + String(firebase_index) + "]/smoke", flag_smoke[i]);
    data_arr[i].set("[" + String(firebase_index) + "]/switch", flag_switch[i]);
    data_arr[i].set("[" + String(firebase_index) + "]/timestamp", timeBuff2);
    data_arr[i].set("[" + String(firebase_index) + "]/isOK", flag_ack[i]);
  }

  Firebase.setArray(fbdo, "/gateway", data_arr[0]);
  Firebase.setArray(fbdo, "/node1", data_arr[1]);
  Firebase.setArray(fbdo, "/node2", data_arr[2]);
  firebase_index >= 250 ? firebase_index = 0 : firebase_index++;

  Serial.println("sent to firebase");
  vTaskSuspend(sentToFirebase_taskhandle)
  }
}

void sentToLine_task(void *pvParam)
{
  while (1)
  {
    /* code */
  
  
  for (uint8_t i = 0; i < 3; i++)
  {
    if(flag_batt[i])
      
  }

  vTaskSuspend(sentToLine_taskhandle);
  }
}

void sentInternet_task(void *pvParam)
{
  while (1)
  {

    if (!getLocalTime(&current_time))
    {
      Serial.println("Failed to obtain time");
    }

    if (current_time.tm_hour == 13 && current_time.tm_min == 6 && current_time.tm_sec <= 10)
    {
      vTaskResume(req_taskhandle);
    }

    if (flag_danger || flag_net)
    {
      flag_ack[0] = true;
      vTaskResume(sentToBlynk_taskhandle);
      Serial.println("sent to blynk");
      sentToFirebase();

      flag_net = false;
      flag_danger = false;
      flag_ack[0] = false;
      flag_ack[1] = false;
      flag_ack[2] = false;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup()
{

  Serial.begin(115200);

  //Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }

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

  configTime(7 * 60 * 60, 0, "pool.ntp.org"); //setup for NTP request

  xTaskCreatePinnedToCore(blynk_task, "blynk_task", 10000, NULL, 1, &blynk_taskhandle, 0);
  xTaskCreatePinnedToCore(sentToBlynk_task, "sentToBlynk_task", 3000, NULL, 1, &sentToBlynk_taskhandle, 0);
  vTaskSuspend(sentToBlynk_taskhandle);

  xTaskCreatePinnedToCore(sentInternet_task, "sentInternet_task", 10000, NULL, 1, &sentInternet_taskhandle, 1);
  xTaskCreatePinnedToCore(req_task, "req_task", 3000, NULL, 1, &req_taskhandle, 1);
  vTaskSuspend(req_taskhandle);
  xTaskCreatePinnedToCore(sentToFirebase_task, "sentToFirebase_task", 10000, NULL, 1, &sentToFirebase_taskhandle, 1);
  vTaskSuspend(sentToFirebase_taskhandle);
  xTaskCreatePinnedToCore(sentToLine_task, "sentToLine_task", 3000, NULL, 1, &sentToLine_taskhandle, 1);
  vTaskSuspend(sentToLine_taskhandle);
  
  // xTaskCreatePinnedToCore(net_task, "net_task", 10000, NULL, 1, &net_taskhandle, 0);

  pinMode(SW_pin, INPUT);
}

void loop()
{
}