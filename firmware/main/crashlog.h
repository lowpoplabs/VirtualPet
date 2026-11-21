/*
 * Why the chip restarted, kept across restarts so a crash on battery (no
 * serial port) can be read afterwards.
 */
#pragma once

#include <stddef.h>

/*
 * Call once, early. Prints this start's reason, the recent ones and the
 * last crash, and fills `line` with a short version for the card log.
 */
void crashlog_boot(char *line, size_t len);
