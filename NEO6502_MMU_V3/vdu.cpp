// 
// 
// 
#include <Adafruit_dvhstx.h>

#include "vdu.h"

#define DVHSTX_PINOUT_NEO6502_MMU { 14, 18, 16, 12 }

DVHSTXText display(DVHSTX_PINOUT_NEO6502_MMU);
//DVHSTXText display(DVHSTX_PINOUT_NEO6502_MMU, DVHSTX_RESOLUTION_640x360);

const static TextColor colors[] = {
    TextColor::TEXT_BLACK, TextColor::TEXT_RED,    TextColor::TEXT_GREEN,
    TextColor::TEXT_BLUE,  TextColor::TEXT_YELLOW, TextColor::TEXT_MAGENTA,
    TextColor::TEXT_CYAN,  TextColor::TEXT_WHITE,
};

const static TextColor background_colors[] = {
    TextColor::BG_BLACK, TextColor::BG_RED,    TextColor::BG_GREEN,
    TextColor::BG_BLUE,  TextColor::BG_YELLOW, TextColor::BG_MAGENTA,
    TextColor::BG_CYAN,  TextColor::BG_WHITE,
};

const static TextColor intensity[] = { TextColor::ATTR_NORMAL_INTEN,
                                      TextColor::ATTR_LOW_INTEN,
                                      TextColor::ATTR_V_LOW_INTEN };


/// <summary>
/// 
/// </summary>
void resetVDU() {
  display.setColor(TextColor::TEXT_YELLOW, TextColor::BG_BLACK);
  display.clear();
  display.showCursor();

  display.print("NEO6502_MMU@rp2350b: Hello world ");
}

/// <summary>
/// 
/// </summary>
void initVDU() {
  if (!display.begin()) {
    Serial.println("*E: VDU: not enough RAM available");
  }
  else
    resetVDU();

  display.println("\n\nAttribute test\n");
  display.print("   ");
  for (int d : background_colors) {
    display.printf(" %d ri vli ", (int)d >> 3);
  }
  display.write('\n');
  for (TextColor c : colors) {
    display.printf(" %d ", (int)c);
    for (TextColor d : background_colors) {
      display.setColor(c, d);
      display.write('*');
      display.write('*');
      display.write('*');
      display.setColor(c, d, TextColor::ATTR_LOW_INTEN);
      display.write('*');
      display.write('*');
      display.write('*');
      display.setColor(c, d, TextColor::ATTR_V_LOW_INTEN);
      display.write('*');
      display.write('*');
      display.write('*');
      display.setColor(TextColor::TEXT_BLACK, TextColor::BG_WHITE);
      display.write(' ');
    }
    display.write('\n');
  }
  display.write('\n');

  Serial.println("*I: VDU init OK");
}

/// <summary>
/// 
/// </summary>
void testVDU() {

}