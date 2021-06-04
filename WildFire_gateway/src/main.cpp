#include <Arduino.h>
#include <LoRa.h>
#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <IOXhop_FirebaseESP32.h>
#include <TridentTD_LineNotify.h>
#include <BlynkSimpleEsp32.h>


//----------Pin define----------//
#define BLYNK_PRINT Serial
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

uint8_t util_byte = 0;
DHT dht(DHTPIN, DHT11);

boolean flag_batt[3] = {false, false, false};
boolean flag_switch[3] = {false, false, false};
boolean flag_smoke[3] = {false, false, false};

int lastS1 = 0;
int currentS1 = 0;


#define sw_bit 0
#define smoke_bit 1
#define batt_bit 2
//----------Line & Blynk Token----------//
#define LINE_TOKEN  "qXTiqmoPiOWNiCVWpt2RCQ0fXNnVa1XjAm7Dhi7jQAc"   
#define Blynk_TOKEN "6IcpR9OcnSMVpMRdY0VOIQ1CGN-eyHRH"
BlynkTimer timer;

//----------Firebase & WiFi----------//
#define WIFI_SSID "NKH1449_2.4G"
#define WIFI_PASSWORD "0851404547"

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

void sentToNd(char node_num, uint8_t LED_Byte) // r e q node led
{
  uint8_t checksum = 0;
  uint8_t payload[] = {'r', 'e', 'q', node_num, LED_Byte};
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
  while (1)
  {
    Blynk.run(); 
    timer.run(); //แก้ให้ด้วย
    Line_Notify(); //นี่ก็ด้วย
 

    // vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
//------ Blynk ------//
void blynk()
{
  Blynk.virtualWrite(V2, temp[0].asFloat );
  Blynk.virtualWrite(V3, humid[0].asFloat);
  Blynk.virtualWrite(V4, temp[1].asFloat );
  Blynk.virtualWrite(V5, humid[1].asFloat);
  Blynk.virtualWrite(V6, temp[2].asFloat );
  Blynk.virtualWrite(V7, humid[2].asFloat);
}

void Line_Notify()
{
  if (flag_batt[0] = true)
  {
    LINE.notify("BatteryGw LOW!!");
  }
   if (flag_batt[1] = true)
  {
    LINE.notify("BatteryN1 LOW!!");
  }
   if (flag_batt[2] = true)
  {
    LINE.notify("BatteryN2 LOW!!");
  }
   if (flag_switch[0] = true)
  {
    LINE.notify("Gw SOS!!");
  }
  if (flag_switch[1] = true)
  {
    LINE.notify("N1 SOS!!");
  }
  if (flag_switch[2] = true)
  {
    LINE.notify("N2 SOS!!");
  }
  if (flag_smoke[0] = true)
  {
    LINE.notify("Gw Fire!!");
  }
  if (flag_smoke[1] = true)
  {
    LINE.notify("N1 Fire!!");
  }
  if (flag_smoke[2] = true)
  {
    LINE.notify("N2 Fire!!");
  }
}

  

void setup()
{

  Serial.begin(9600);
  //setup Blynk
  Blynk.begin(Blynk_TOKEN, WIFI_SSID , WIFI_PASSWORD , IPAddress(43,229,135,169), 8080);
  timer.setInterval(1000L, blynk);
  //setup Line notify
  Serial.println(LINE.getVersion());
  LINE.setToken(LINE_TOKEN);
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
  
}