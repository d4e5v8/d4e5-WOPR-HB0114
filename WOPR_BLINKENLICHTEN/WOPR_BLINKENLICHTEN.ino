// Random effect for WOPR LED Matrix in HackerBox 0114
// https://hackerboxes.com/products/hackerbox-0114-wopr

#include <MD_MAX72xx.h>
#include <SPI.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES	12
#define CLK_PIN   4
#define DATA_PIN  6
#define CS_PIN    7

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES); 

void setup()
{
  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, MAX_INTENSITY/8);
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
  mx.clear();
}

void loop(void)
{
  update_leds();
  delay(200);
}

void update_leds(void)
{
  for (int col=0; col<96; col++)
  {
    if (random(100)<30)  // randomly update each column 30% of the time
    {
      for (int row=0; row<8; row++)
      {
        if (random(100)<65)  // randomly light each bit 65% of the time
          mx.setPoint(row, col, true);
        else
          mx.setPoint(row, col, false);  
      }
    }
  }
}



