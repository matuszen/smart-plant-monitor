#include <FreeRTOS.h>
#include <task.h>
#include <pico/platform/panic.h>
#include <cstdio>

extern "C"
{
  void vApplicationMallocFailedHook(void)
  {
    panic("FreeRTOS: Malloc Failed! No memory available.");
  }

  void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName)
  {
    (void)xTask;
    panic("FreeRTOS: Stack Overflow in task: %s", pcTaskName);
  }

  void vApplicationIdleHook(void)
  {
  }

  void vApplicationTickHook(void)
  {
  }
}
