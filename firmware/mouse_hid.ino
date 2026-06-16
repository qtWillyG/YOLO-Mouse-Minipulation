// mouse_hid.ino - USB HID mouse for RP2040 / RP2350 (Raspberry Pi Pico / Pico 2)
//
// Reads simple line commands over USB serial and performs HID mouse actions.
// Works with the Arduino-Pico core (Earle Philhower) for both RP2040 and RP2350.
//
//   Tools -> USB Stack -> "Adafruit TinyUSB"
//   Board -> "Raspberry Pi Pico"  (RP2040)  or  "Raspberry Pi Pico 2" (RP2350)
//
// Serial protocol (newline-terminated, 115200 baud):
//   M,<dx>,<dy>   relative move  (e.g.  M,12,-4 )
//   L             left click
//   R             right click
//   D             left button down (hold)
//   U             left button up
//
// HID relative moves are limited to -127..127 per report, so large deltas are
// split into multiple reports automatically.

#include <Adafruit_TinyUSB.h>

// HID report descriptor: a single relative mouse.
uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_MOUSE()
};

Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report),
                          HID_ITF_PROTOCOL_MOUSE, 2, false);

static char    lineBuf[32];
static uint8_t lineLen = 0;

void setup() {
  usb_hid.begin();
  Serial.begin(115200);
  while (!TinyUSBDevice.mounted()) delay(1);
}

// Send a relative move, chunking into <=127 px steps per HID report.
void moveRelative(int dx, int dy) {
  while (dx != 0 || dy != 0) {
    int sx = dx >  127 ?  127 : (dx < -127 ? -127 : dx);
    int sy = dy >  127 ?  127 : (dy < -127 ? -127 : dy);
    usb_hid.mouseMove(0, (int8_t)sx, (int8_t)sy);
    dx -= sx;
    dy -= sy;
    delayMicroseconds(300);
  }
}

void handleLine(char* s) {
  if (s[0] == 'M') {
    // M,dx,dy
    char* p = strchr(s, ',');
    if (!p) return;
    int dx = atoi(p + 1);
    char* q = strchr(p + 1, ',');
    if (!q) return;
    int dy = atoi(q + 1);
    moveRelative(dx, dy);
  } else if (s[0] == 'L') {
    usb_hid.mouseButtonPress(0, MOUSE_BUTTON_LEFT);
    delay(15);
    usb_hid.mouseButtonRelease(0);
  } else if (s[0] == 'R') {
    usb_hid.mouseButtonPress(0, MOUSE_BUTTON_RIGHT);
    delay(15);
    usb_hid.mouseButtonRelease(0);
  } else if (s[0] == 'D') {
    usb_hid.mouseButtonPress(0, MOUSE_BUTTON_LEFT);
  } else if (s[0] == 'U') {
    usb_hid.mouseButtonRelease(0);
  }
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        if (usb_hid.ready()) handleLine(lineBuf);
        lineLen = 0;
      }
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
}
