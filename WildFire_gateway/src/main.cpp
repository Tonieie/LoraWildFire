#include <Arduino.h>
#include <LoRa.h>

#define SCK 14
#define MISO 12
#define MOSI 13
#define SS 27
#define RST 26
#define DIO0 25

TaskHandle_t receiveNode_handle = NULL;

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
  Serial.print("Gateway Receive: ");
  while (LoRa.available())
  {
    uint8_t buff = (uint8_t)LoRa.read();
    Serial.write(buff);
  }
  Serial.println();
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

  // xTaskCreatePinnedToCore(receiveNode, "receiveNode", 10000, NULL, 0, &receiveNode_handle, 0);

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
  LoRa_rxMode();
}

void loop()
{
  // put your main code here, to run repeatedly:
}