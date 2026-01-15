#ifndef _NODEINPUT_H_
#define _NODEINPUT_H_
#include <stdlib.h>
#include <stdint.h>

#ifndef BIT
#define BIT(n) (1 << (n))
#endif

typedef enum KEYPAD_BITS {
  KEY_LEFT = BIT(0),   //!< Keypad LEFT button.
  KEY_DOWN = BIT(1),   //!< Keypad DOWN button.
  KEY_RIGHT = BIT(2),  //!< Keypad RIGHT button.
  KEY_UP = BIT(3),     //!< Keypad UP button.
  KEY_ALT = BIT(4),    //!< Keypad ALT button.
  KEY_EDIT = BIT(5),   //!< Keypad EDIT button.
  KEY_ENTER = BIT(6),  //!< Keypad ENTER button.
  KEY_NAV = BIT(7),    //!< Keypad NAV button.
  KEY_PLAY = BIT(8),   //!< Keypad PLAY/START button.
  KEY_SELECT = BIT(9), //!< Keypad SELECT button.
  KEY_UNUSED = 0,      //!< Unused/skip position
} KEYPAD_BITS;

uint16_t scanKeys();

#endif
