#include <Arduino.h>

TaskHandle_t handle_1 = NULL;

void task1(void *pvparameter)
{
  while (1)
  {
    Serial.print("setup() running on core ");
    Serial.println(xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void setup()
{
  Serial.begin(9600);
  xTaskCreatePinnedToCore(task1, "task1", 5000, NULL, 1, &handle_1, 0);
}

void loop()
{
}
