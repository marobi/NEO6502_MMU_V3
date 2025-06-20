// 
// 
// 

#include <PicoDVI.h>

#include "vdu.h"

// Pico DVI Sock ('pico_sock_cfg').
DVIGFX8 display(DVI_RES_320x240p60, true, pico_sock_cfg);


/// <summary>
/// 
/// </summary>
void resetVDU() {
  display.setColor(255, 0xFF00);       // Last palette entry = white
  display.swap(false, true);           // Duplicate same palette into front & back buffers
  // Clear back framebuffer
  display.fillScreen(0);               // black
  display.setFont();                   // Use default font
  display.setCursor(0, 0);             // Initial cursor position
  display.setTextSize(1);              // Default size

  display.println("NEO6502_MMU@rp2350b: Hello world");
  display.swap(true, false);
}

/// <summary>
/// 
/// </summary>
void initVDU() {
  if (!display.begin()) {
    Serial.println("ERROR: not enough RAM available");
    for (;;);
  }

  resetVDU();
}

