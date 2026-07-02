#ifndef __PAD_H
#define __PAD_H

// PAD handling

#define KEY_LEFT     1
#define KEY_DOWN     2
#define KEY_RIGHT    3
#define KEY_UP       4
#define KEY_START    5
#define KEY_R3       6
#define KEY_L3       7
#define KEY_SELECT   8
#define KEY_SQUARE   9
#define KEY_CROSS    10
#define KEY_CIRCLE   11
#define KEY_TRIANGLE 12
#define KEY_R1       13
#define KEY_L1       14
#define KEY_R2       15
#define KEY_L2       16

#define PAD_CONTROLLER_COUNT 4

#define PAD_CONTROLLER_SOURCE_NONE 0
#define PAD_CONTROLLER_SOURCE_PS2  1
#define PAD_CONTROLLER_SOURCE_DS34 2
#define PAD_CONTROLLER_SOURCE_XBOX 3

typedef struct pad_controller_state
{
    int connected;
    int source;
    unsigned int buttons;
} pad_controller_state_t;

int startPads();
int readPads();
void unloadPads();

const pad_controller_state_t *padGetControllerState(int player);
int padGetControllerCount(void);

int getKey(int num);

int getKeyOn(int num);
int getKeyOff(int num);
int getKeyPressed(int num);

/** Sets the repetition delay for the specified button
 * @param button id (KEY_XXX values)
 * @param btndelay the delay in miliseconds per repeat (clamped by framerate!) */
void setButtonDelay(int button, int btndelay);

/** Gets the repetition delay for the specified button */
int getButtonDelay(int button);


/** Store's the button delay into specified integer array (has to have 16 items) */
void padStoreSettings(int *buffer);

/** Restore's the button delay from specified integer array (has to have 16 items) */
void padRestoreSettings(int *buffer);

#endif
