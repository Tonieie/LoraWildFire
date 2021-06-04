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
#define LEDPin 23

#define DHTPIN 15
#define SW_pin 19
#define LED 23
/***********************MQ2************************************/
#define MQ_PIN (2)                        //define which analog input channel you are going to use
#define RL_VALUE (5)                      //define the load resistance on the board, in kilo ohms
#define RO_CLEAN_AIR_FACTOR (9.83)        //RO_CLEAR_AIR_FACTOR=(Sensor resistance in clean air)/RO, 
                                          //which is derived from the chart in datasheet
#define CALIBARAION_SAMPLE_TIMES (50)     //define how many samples you are going to take in the calibration phase
#define CALIBRATION_SAMPLE_INTERVAL (500) //define the time interal(in milisecond) between each samples in the
#define READ_SAMPLE_INTERVAL (50)         //define how many samples you are going to take in normal operation
#define READ_SAMPLE_TIMES (5)             //define the time interal(in milisecond) between each samples in
#define GAS_CO (1)
#define GAS_SMOKE (2)
float COCurve[3] = {2.3, 0.72, -0.34};    //two points are taken from the curve.
                                          //with these two points, a line is formed which is "approximately equivalent"
                                          //to the original curve.
                                          //data format:{ x, y, slope}; point1: (lg200, 0.72), point2: (lg10000,  0.15)
float SmokeCurve[3] = {2.3, 0.53, -0.44}; //two points are taken from the curve.
                                          //with these two points, a line is formed which is "approximately equivalent"
                                          //to the original curve.
                                          //data format:{ x, y, slope}; point1: (lg200, 0.53), point2: (lg10000,  -0.22)
float Ro = 10;                            //Ro is initialized to 10 kilo ohms
/////////////////////////////////////////////////////

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

#define node_number 1
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
        digitalWrite(LED, HIGH);
      else
        digitalWrite(LED, LOW);

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
float MQResistanceCalculation(int raw_adc)
{
  return (((float)RL_VALUE * (4095 - raw_adc) / raw_adc));
}
float MQCalibration(int mq_pin)
{
  int i;
  float val = 0;

  for (i = 0; i < CALIBARAION_SAMPLE_TIMES; i++)
  { //take multiple samples
    val += MQResistanceCalculation(analogRead(mq_pin));
    delay(CALIBRATION_SAMPLE_INTERVAL);
  }
  val = val / CALIBARAION_SAMPLE_TIMES; //calculate the average value

  val = val / RO_CLEAN_AIR_FACTOR; //divided by RO_CLEAN_AIR_FACTOR yields the Ro
  return val;
}
float MQRead(int mq_pin)
{
  int i;
  float rs = 0;
  for (i = 0; i < READ_SAMPLE_TIMES; i++)
  {
    rs += MQResistanceCalculation(analogRead(mq_pin));
    delay(READ_SAMPLE_INTERVAL);
  }
  rs = rs / READ_SAMPLE_TIMES;
  return rs;
}
int MQGetPercentage(float rs_ro_ratio, float *pcurve)
{
  return (pow(10, (((log(rs_ro_ratio) - pcurve[1]) / pcurve[2]) + pcurve[0])));
}
int MQGetGasPercentage(float rs_ro_ratio, int gas_id)
{
    if (gas_id == GAS_CO)
    {
        return MQGetPercentage(rs_ro_ratio, COCurve);
    }
    else if (gas_id == GAS_SMOKE)
    {
        return MQGetPercentage(rs_ro_ratio, SmokeCurve);
    }
    return 0;
}
boolean readMQ()
{
  long COppm, SMOKEppm = 0;
  //Serial.print("CO:");
  COppm = MQGetGasPercentage(MQRead(MQ_PIN) / Ro, GAS_CO);
  //Serial.print("SMOKE:");
  SMOKEppm = MQGetGasPercentage(MQRead(MQ_PIN) / Ro, GAS_SMOKE);
  if (SMOKEppm > 400) 
  {
    return true;
  }
  else
  {
    return false;
  }
}

    //----------CPU0 Task----------//
void sentToGw(void *pvParam)
{
  while (1)
  ;
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
  pinMode(LEDPin, OUTPUT);

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
  Serial.print("Calibrating...\n");
  Ro = MQCalibration(MQ_PIN); //MQ2 calibration
  pinMode(LED, OUTPUT);

  xTaskCreatePinnedToCore(sentToGw, "sentToGw", 2000, NULL, 1, &sentToGw_handle, 0);

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
  LoRa_rxMode();
}

void loop()
{
  // readMQ();
}
