#ifndef OS64_SERIAL_HPP
#define OS64_SERIAL_HPP
#include <stddef.h>

bool initialize_serial_input();
void handle_serial_irq();
void serial_begin_input_line();
void serial_move_input_cursor(size_t cursor);
void serial_redraw_input_line(const char* buffer, size_t length, size_t cursor);
void serial_end_input_line();

#endif
