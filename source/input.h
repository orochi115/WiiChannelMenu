#ifndef INPUT_H
#define INPUT_H

typedef enum {
	INPUT_NONE,
	INPUT_UP,
	INPUT_DOWN,
	INPUT_LEFT,
	INPUT_RIGHT,
	INPUT_ACCEPT,
	INPUT_HOME,
} InputAction;

void input_init(void);
// Polls all Wii Remotes (with Classic Controller). Call once per frame.
InputAction input_poll(void);
void input_shutdown(void);

#endif
