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

#define MQPIN 2
#define DHTPIN 15
#define SW_pin 19
#define BattPin 18
#define LEDPin 23
#define BattadcPin 4

//----------CPU0 Handle----------//
TaskHandle_t sentToGw_handle = NULL;

//----------CPU1 Handle----------//
TaskHandle_t receiveFromGw_handle = NULL;
TaskHandle_t ctrlLed_handle = NULL;

//----------Params----------//
DHT dht(DHTPIN, DHT11);

union FloatToByte
{
  float asFloat;
  uint8_t asByte[4];
};

FloatToByte temp, humid;
uint8_t util_byte = 0;

#define node_number 2
#define sw_bit 0
#define smoke_bit 1
#define batt_bit 2
#define led_bit 3

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

  static uint8_t buffer[10];
  uint8_t index = 0;
  while (LoRa.available())
  {
    buffer[index] = (uint8_t)LoRa.read();
    if (buffer[index - 4] == 'r' && buffer[index - 3] == 'e' && buffer[index - 2] == 'q' && buffer[index - 1] == ('0' + node_number))
    {
      if (index == 0 && buffer[0] != 'r')
        index = 0;

      if (buffer[index] == 0xFF)
        digitalWrite(LEDPin, HIGH);
      else
        digitalWrite(LEDPin, LOW);

      sent_flag = true;

      index = 0;
    }

    index >= 9 ? index = 0 : index++;
  }
}

void onTxDone()
{
  LoRa_rxMode();
}

//----------Sensor Reading----------//
void setBit(uint8_t *data, uint8_t bit)
{
  *data |= (1 << bit);
}

void clearBit(uint8_t *data, uint8_t bit)
{
  *data &= ~(1 << bit);
}

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
  digitalWrite(18, HIGH);

  v_batt /= 100;
  v_batt = v_batt / 4095.0f * 2 * 3.3f + 0.2f;

  Serial.println("batt : " + String(v_batt));
  if (v_batt <= 3.7f)
    setBit(&util_byte, batt_bit);
  else
    clearBit(&util_byte, batt_bit);
}

void readDHT()
{
  temp.asFloat = dht.readTemperature();
  humid.asFloat = dht.readHumidity();
  Serial.println("temp : " + String(temp.asFloat) + "/t humid : " + String(humid.asFloat));
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
  if (abs(current_val - last_val) >= 50)
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
    current_switch == true ? setBit(&util_byte, sw_bit) : clearBit(&util_byte, sw_bit);
    sent_flag = true;
  }

  boolean current_smoke = readSmoke();
  if (current_smoke != last_smoke)
  {
    Serial.println("smoke toggled");
    last_smoke = current_smoke;
    current_smoke == true ? setBit(&util_byte, smoke_bit) : clearBit(&util_byte, smoke_bit);
    sent_flag = true;
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
  pinMode(LEDPin, OUTPUT);
  pinMode(BattPin, OUTPUT);
  pinMode(SW_pin, INPUT);

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
  LoRa_rxMode();
  Serial.println("node ready");
}

void loop()
{
  static uint32_t lasttime,last_sent = millis();
  if (millis() - lasttime >= 1000)  //run every 1 second
  {
    lasttime = millis();
    
    readCritical();
    if (sent_flag == true && (millis() - last_sent >= 15000))
    {
      readCritical();
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
      Serial.println("sent");

      last_sent = millis();
    }
  }
}
