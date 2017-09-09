/**
 * Function definitions for the main project file.
 *
 */

#ifndef Demo_INCLUDED
#define Demo_INCLUDED

struct coord {
	uint8_t x;
	uint8_t y;
};

void uartReceive();
void sendLine(struct coord coord_1, struct coord coord_2);
void checkJoystick();

#endif
