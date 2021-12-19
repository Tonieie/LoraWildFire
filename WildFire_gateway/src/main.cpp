#include <Arduino.h>
#include <LoRa.h>
#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>

#include <BlynkSimpleEsp32.h>
#include <TridentTD_LineNotify.h>

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

#define MQPIN 34
#define DHTPIN 15
#define SW_pin 19
#define BattPin 18
#define LEDPin 23
#define BattadcPin 35

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
boolean flag_danger[3] = {false, false, false};
boolean flag_net = false;
boolean blynk_req = false;

uint8_t util_byte[3] = {0};
uint8_t last_util_byte[3] = {0};
uint8_t blynk_req_led = 0;

#define sw_bit 0
#define smoke_bit 1
#define batt_bit 2

//----------Firebase & WiFi----------//

#define MAXIMUM_TRY 5

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

FirebaseJsonArray data_arr[3];
uint8_t firebase_index[3];

struct tm current_time;

//----------Task Handle----------//
TaskHandle_t sentInternet_taskhandle = NULL;
TaskHandle_t dht_taskhandle = NULL;

TaskHandle_t blynk_taskhandle = NULL;
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
        util_byte[node_num] = buffer[index - 1];
        if (util_byte[node_num] != last_util_byte[node_num])
        {
          last_util_byte[node_num] = util_byte[node_num];
          flag_danger[node_num] = true;
        }
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
  vTaskDelay(pdMS_TO_TICKS(1000));

  float v_batt = 0;
  for (uint8_t read_cnt = 0; read_cnt < 100; read_cnt++)
  {
    v_batt += (float)analogRead(BattadcPin);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  v_batt /= 100;
  v_batt = v_batt / 4095.0f * 2 * 3.3f + 0.2f;

  digitalWrite(18, HIGH);
  Serial.println("batt : " + String(v_batt));
  if (v_batt <= 3.7f)
    flag_batt[0] = true;
  else
    flag_batt[0] = false;
}

boolean readSmoke()
{
  static int last_val = analogRead(MQPIN);
  int current_val = 0;
  for (uint8_t read_cnt = 0; read_cnt < 100; read_cnt++)
  {
    current_val += analogRead(MQPIN);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  current_val /= 100;
  if (abs(current_val - last_val) >= 150)
  {
    last_val = current_val;
    return true;
  }
  else
  {
    last_val = current_val;
    return false;
  }
}

void readCritical()
{

  static boolean last_switch = !digitalRead(SW_pin);
  static boolean last_smoke = readSmoke();

  boolean current_switch = !digitalRead(SW_pin); //true = pressed
  if (current_switch != last_switch)
  {
    Serial.println("switch toggled");
    last_switch = current_switch;
    flag_switch[0] = current_switch;
    flag_danger[0] = true;
  }

  boolean current_smoke = readSmoke();
  if (current_smoke != last_smoke)
  {
    Serial.println("smoke toggled");
    last_smoke = current_smoke;
    flag_smoke[0] = current_smoke;
    flag_danger[0] = true;
  }
}

//----------CPU0 Tasks----------//
void blynk_task(void *pvParam)
{
  //Blynk init
  Blynk.begin(Blynk_TOKEN, WIFI_SSID, WIFI_PASSWORD, IPAddress(43, 229, 135, 169), 8080);
  while (1)
  {
    Blynk.run();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

BLYNK_WRITE(V8)
{
  if(param.asInt())
  {
    blynk_req = true;
    blynk_req_led = 0xFF;
  }
}

BLYNK_WRITE(V9)
{
 if(param.asInt())
  {
    blynk_req = true;
    blynk_req_led = 0;
  }
}

BLYNK_WRITE(V10)
{
 if(param.asInt())
  {
    ESP.restart();
  }
}


//----------CPU1 Tasks----------//

void reqNode(uint8_t led_req)
{
  uint8_t try_count = 0;
  flag_ack[1] = false;
  flag_ack[2] = false;
  while (flag_ack[1] == false && try_count < MAXIMUM_TRY)
  {
    try_count++;
    sentToNd('1', led_req);
    Serial.println("try node1 : " + String(try_count));
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
  try_count = 0;
  while (flag_ack[2] == false && try_count < MAXIMUM_TRY)
  {
    try_count++;
    sentToNd('2', led_req);
    Serial.println("try node2 : " + String(try_count));
    vTaskDelay(pdMS_TO_TICKS(10000));
  }

  flag_net = true;
}

void sentLineNode(uint8_t node, String node_name)
{

  if (flag_batt[node])
    LINE.notify("Battery " + node_name + " Low");

  if (flag_smoke[node])
    LINE.notify("Smoke detected at " + node_name);

  if (flag_switch[node])
    LINE.notify("SOS from " + node_name);
}
void sentLine(uint8_t node)
{
  static String node_name[3] = {"Gateway", "Node 1", "Node 2"};

  if (node == 3)
  {
    sentLineNode(0, node_name[0]);
    sentLineNode(1, node_name[1]);
    sentLineNode(2, node_name[2]);
  }
  else if (node >= 0 && node <= 2)
  {
    sentLineNode(node, node_name[node]);
  }

  Serial.println("sent to Line " + String(node));
}

void sentBlynk(uint8_t node)
{
  if (node == 3)
  {
    Blynk.virtualWrite(2, temp[0].asFloat);
    Blynk.virtualWrite(3, humid[0].asFloat);
    Blynk.virtualWrite(4, temp[1].asFloat);
    Blynk.virtualWrite(5, humid[1].asFloat);
    Blynk.virtualWrite(6, temp[2].asFloat);
    Blynk.virtualWrite(7, humid[2].asFloat);

    Blynk.virtualWrite(11, flag_ack[0]*255);
    Blynk.virtualWrite(12, flag_ack[1]*255);
    Blynk.virtualWrite(13, flag_ack[2]*255);
  }
  else if (node >= 0 && node <= 2)
  {
    Blynk.virtualWrite(2 + (node * 2), temp[node].asFloat);
    Blynk.virtualWrite(3 + (node * 2), humid[node].asFloat);
    Blynk.virtualWrite(11+node, flag_ack[node]);
  }

  Serial.println("sent to blynk " + String(node));
}

void sentFirebaseNode(uint8_t node, String timeBuff2)
{
  data_arr[node].set("[" + String(firebase_index[node]) + "]/temp", temp[node].asFloat);
  data_arr[node].set("[" + String(firebase_index[node]) + "]/humid", humid[node].asFloat);
  data_arr[node].set("[" + String(firebase_index[node]) + "]/batt", flag_batt[node]);
  data_arr[node].set("[" + String(firebase_index[node]) + "]/smoke", flag_smoke[node]);
  data_arr[node].set("[" + String(firebase_index[node]) + "]/switch", flag_switch[node]);
  data_arr[node].set("[" + String(firebase_index[node]) + "]/timestamp", timeBuff2);
  data_arr[node].set("[" + String(firebase_index[node]) + "]/isOK", flag_ack[node]);

  firebase_index[node] >= 250 ? firebase_index[node] = 0 : firebase_index[node]++;
}

void setFirebase(uint8_t node)
{
  static String node_name[3] = {"/gateway", "/node1", "/node2"};
  if (node == 3)
  {
    Firebase.setArray(fbdo, "/gateway", data_arr[0]);
    Firebase.setArray(fbdo, "/node1", data_arr[1]);
    Firebase.setArray(fbdo, "/node2", data_arr[2]);
  }
  else if (node >= 0 && node <= 2)
  {
    Firebase.setArray(fbdo, node_name[node], data_arr[node]);
  }
}

void sentFirebase(uint8_t node)
{
  struct tm time_now;
  getLocalTime(&time_now);
  char timeBuff[50]; //50 chars should be enough
  strftime(timeBuff, sizeof(timeBuff), "%A %B %d %Y %H:%M:%S", &time_now);
  String timeBuff2(timeBuff);

  if (node == 3)
  {
    sentFirebaseNode(0, timeBuff2);
    sentFirebaseNode(1, timeBuff2);
    sentFirebaseNode(2, timeBuff2);
  }
  else if (node >= 0 && node <= 2)
  {
    sentFirebaseNode(node, timeBuff2);
  }
  setFirebase(node);

  Serial.println("sent to firebase " + String(node));
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
  delay(3000);

  //Init Line Token
  LINE.setToken(LINE_TOKEN);

  //Init Firebase
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  delay(5000);

  Firebase.getArray(fbdo, "/gateway");
  data_arr[0] = fbdo.jsonArray();
  Firebase.getArray(fbdo, "/node1");
  data_arr[1] = fbdo.jsonArray();
  Firebase.getArray(fbdo, "/node2");
  data_arr[2] = fbdo.jsonArray();
  firebase_index[0] = data_arr[0].size();
  firebase_index[1] = data_arr[1].size();
  firebase_index[2] = data_arr[2].size();
  delay(5000);
  if (firebase_index[0] == 0 || firebase_index[1] == 0 || firebase_index[2] == 0)
  {
    Firebase.getArray(fbdo, "/gateway");
    data_arr[0] = fbdo.jsonArray();
    Firebase.getArray(fbdo, "/node1");
    data_arr[1] = fbdo.jsonArray();
    Firebase.getArray(fbdo, "/node2");
    data_arr[2] = fbdo.jsonArray();
    firebase_index[0] = data_arr[0].size();
    firebase_index[1] = data_arr[1].size();
    firebase_index[2] = data_arr[2].size();
    delay(5000);
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

  dht.begin();
  pinMode(LEDPin, OUTPUT);
  pinMode(BattPin, OUTPUT);
  pinMode(SW_pin, INPUT);

  configTime(7 * 60 * 60, 0, "pool.ntp.org"); //setup for NTP request

  xTaskCreatePinnedToCore(blynk_task, "blynk_task", 10000, NULL, 1, &blynk_taskhandle, 0);


  digitalWrite(LEDPin, HIGH);
  delay(2000);
  digitalWrite(LEDPin, LOW);
  delay(1000);
}

void loop()
{
  static uint32_t last_time = millis();
  static boolean first_time = true;
  if (millis() - last_time >= 1000)
  {

    last_time = millis();
    if (!getLocalTime(&current_time))
    {
      Serial.println("Failed to obtain time");
    }

    if (current_time.tm_min == 0 && current_time.tm_sec <= 10)
    {
      if (current_time.tm_hour == 18 || current_time.tm_hour == 20 || current_time.tm_hour == 0 || current_time.tm_hour == 4)
      {
        digitalWrite(LEDPin,HIGH);
        reqNode(0xFF);
      }
      else if (current_time.tm_hour == 6 || current_time.tm_hour == 8 || current_time.tm_hour == 12 || current_time.tm_hour == 16)
      {
        digitalWrite(LEDPin,LOW);
        reqNode(0);
      }
    }

    if(first_time)
    {
      if(current_time.tm_hour >= 18 || current_time.tm_hour < 6)
      {
        digitalWrite(LEDPin,HIGH);
        reqNode(0xFF);
      }
      else
      {
        digitalWrite(LEDPin,LOW);
        reqNode(0);
      }
      first_time = false;
    }

    if(blynk_req)
    {
      static uint32_t last_req = 0;
      if(millis() - last_req >= 15000)
      {
        last_req = millis();
        blynk_req = false;
        digitalWrite(LEDPin,blynk_req_led>>7);
        Serial.println("Request from blynk");
        reqNode(blynk_req_led);
      }
    }

    readCritical();
    for (uint node = 0; node < 3; node++)
    {
      if (flag_danger[0])
      {
        readBatt();
        flag_ack[0] = true;
        temp[0].asFloat = dht.readTemperature();
        humid[0].asFloat = dht.readHumidity();
        vTaskDelay(pdMS_TO_TICKS(2000));
        sentLine(0);
        sentBlynk(0);
        sentFirebase(0);
      }
      else if (flag_danger[node])
      {
        sentLine(node);
        sentBlynk(node);
        sentFirebase(node);
      }
      flag_danger[node] = false;
    }

    if (flag_net)
    {
      readCritical();
      readBatt();
      flag_ack[0] = true;
      temp[0].asFloat = dht.readTemperature();
      humid[0].asFloat = dht.readHumidity();
      vTaskDelay(pdMS_TO_TICKS(2000));

      sentLine(3);
      sentBlynk(3);
      sentFirebase(3);

      flag_net = false;
      flag_ack[0] = false;
      flag_ack[1] = false;
      flag_ack[2] = false;
    }
  }
}
