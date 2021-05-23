#include <Arduino.h>
#include <LoRa.h>



TaskHandle_t receiveNode_handle = NULL;

// void receiveNode(void *pvParam)
// {
  
//   int packetSize = LoRa.parsePacket();
//   if (packetSize) {
//     // received a packet
//     Serial.print("Received packet '");

//     // read packet
//     while (LoRa.available()) {
//       Serial.print((char)LoRa.read());
//     }

//     // print RSSI of packet
//     Serial.print("' with RSSI ");
//     Serial.println(LoRa.packetRssi());
//   }
//   vTaskDelete(NULL);
// }

void LoRa_rxMode(){
  LoRa.disableInvertIQ();               // normal mode
  LoRa.receive();                       // set receive mode
}

void LoRa_txMode(){
  LoRa.idle();                          // set standby mode
  LoRa.enableInvertIQ();                // active invert I and Q signals
}

void onReceive(int packetSize) {
  Serial.print("Gateway Receive: ");
  while (LoRa.available()) {
    uint8_t buff = (uint8_t) LoRa.read();
    Serial.write(buff);
  }
  Serial.println();
 
}

void onTxDone() {
  Serial.println("TxDone");
  LoRa_rxMode();
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  SPI.begin(14,12,13,27);
  LoRa.setPins(27,26,25);
  if (!LoRa.begin(923.2E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }

  // xTaskCreatePinnedToCore(receiveNode, "receiveNode", 10000, NULL, 0, &receiveNode_handle, 0);

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
  LoRa_rxMode();

}

void loop() {
  // put your main code here, to run repeatedly:
}