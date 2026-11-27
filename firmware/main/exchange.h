/*
 * The exchange over Bluetooth, on the watch: the one time the pet does
 * more than listen. While the owner has the exchange screen open the
 * watch waits for another Baandwidth Farms to call (it never calls
 * anything itself), asks for a six-digit code shown on its own screen,
 * and then runs pet_link over the encrypted link. Nothing about the
 * pairing is kept once the link ends.
 */
#pragma once

#include <stdbool.h>

/* Sets up the service and the pairing rules. Call once, before the Bluetooth host is started. */
void exchange_init(void);

/*
 * The "exchange" of pet_hal_t. what: 0 = the screen was closed, stop;
 * 1 = wait for a call to bring the farms level; 2 = wait, and give the pet
 * to the caller. Returns false if it cannot start.
 */
bool exchange_ask(int what);

/* True while the exchange has the radio: the listening scan must wait. */
bool exchange_busy(void);
