#include "interrupts/serial.hpp"

#include <stdint.h>
#include "interrupts/keyboard.hpp"
#include "interrupts/pic.hpp"

namespace {
constexpr uint16_t kPort = 0x3F8;
uint8_t escape_state = 0;
bool previous_cr = false;
bool ready = false;
size_t input_cursor = 0;
inline void out8(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
inline uint8_t in8(uint16_t port) {
  uint8_t value;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}
void put(char ch) {
  for (unsigned i = 0; i < 100000; ++i) {
    if ((in8(kPort + 5) & 0x20) != 0) { out8(kPort, ch); return; }
  }
}
void movement(size_t count, char direction) {
  if (count == 0) { return; }
  put('\033'); put('[');
  char digits[20]; size_t n = 0;
  do { digits[n++] = '0' + count % 10; count /= 10; } while (count != 0);
  while (n != 0) { put(digits[--n]); }
  put(direction);
}
void submit(uint8_t ch) {
  KeyboardInputEvent event{kKeyboardInputCharacter, static_cast<char>(ch)};
  if (escape_state == 1) {
    escape_state = (ch == '[' || ch == 'O') ? 2 : 0;
    return;
  }
  if (escape_state == 2) {
    escape_state = 0;
    switch (ch) {
      case 'A': event.kind = kKeyboardInputArrowUp; break;
      case 'B': event.kind = kKeyboardInputArrowDown; break;
      case 'C': event.kind = kKeyboardInputArrowRight; break;
      case 'D': event.kind = kKeyboardInputArrowLeft; break;
      case 'H': event.kind = kKeyboardInputHome; break;
      case 'F': event.kind = kKeyboardInputEnd; break;
      case '3': escape_state = 3; return;
      default: return;
    }
  } else if (escape_state == 3) {
    escape_state = 0;
    if (ch != '~') { return; }
    event.kind = kKeyboardInputDelete;
  } else if (ch == 27) {
    escape_state = 1;
    return;
  } else {
    if (ch == '\n' && previous_cr) { previous_cr = false; return; }
    previous_cr = ch == '\r';
    if (ch == '\r') { event.character = '\n'; }
    if (ch == 127) { event.character = '\b'; }
    if (event.character != '\n' && event.character != '\b' &&
        (ch < 32 || ch >= 127)) { return; }
  }
  keyboard_submit_input_event(event);
}
}

bool initialize_serial_input() {
  if (!keyboard_is_ready() || in8(kPort + 5) == 0xFF) { return false; }
  out8(kPort + 1, 0);
  out8(kPort + 3, 0x03); // 8N1; retain the boot console baud rate.
  out8(kPort + 2, 0xC7); // FIFO and clear previous input.
  out8(kPort + 4, 0x0B); // OUT2 routes the interrupt to the PIC.
  escape_state = 0;
  previous_cr = false;
  out8(kPort + 1, 0x01); // Received data interrupt only.
  ready = enable_pic_irq(4);
  return ready;
}

void serial_begin_input_line() { input_cursor = 0; }
void serial_move_input_cursor(size_t cursor) {
  if (!ready) { return; }
  movement(cursor > input_cursor ? cursor - input_cursor : input_cursor - cursor,
           cursor > input_cursor ? 'C' : 'D');
  input_cursor = cursor;
}
void serial_redraw_input_line(const char* buffer, size_t length, size_t cursor) {
  if (!ready) { return; }
  movement(input_cursor, 'D');
  for (size_t i = 0; i < length; ++i) { put(buffer[i]); }
  put('\033'); put('['); put('K');
  input_cursor = length;
  serial_move_input_cursor(cursor);
}
void serial_end_input_line() {
  if (ready) { put('\r'); put('\n'); }
  input_cursor = 0;
}

void handle_serial_irq() {
  for (unsigned i = 0; i < 256; ++i) {
    if ((in8(kPort + 5) & 1) == 0) { break; }
    submit(in8(kPort));
  }
  (void)in8(kPort + 2);
}
