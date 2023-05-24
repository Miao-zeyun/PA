#include "cpu/rtl.h"

/* Condition Code */

void rtl_setcc(rtlreg_t* dest, uint8_t subcode) {
  bool invert = subcode & 0x1;
  enum {
    CC_O, CC_NO, CC_B,  CC_NB,
    CC_E, CC_NE, CC_BE, CC_NBE,
    CC_S, CC_NS, CC_P,  CC_NP,
    CC_L, CC_NL, CC_LE, CC_NLE
  };

  // TODO: Query EFLAGS to determine whether the condition code is satisfied.
  // dest <- ( cc is satisfied ? 1 : 0)
  switch (subcode & 0xe) {
    case CC_O:
      *dest=(eflags_OF == 1);
      break;
    case CC_B:
      *dest=(eflags_CF == 1);
      break;
    case CC_E:
      *dest=(eflags_ZF == 1);
      break;
    case CC_BE:
      *dest=((eflags_CF == 1) || (eflags_ZF == 1));
      break;
    case CC_S:
      *dest=(eflags_SF == 1);
      break;
    case CC_L:
      *dest=(eflags_SF != eflags_OF);
      break;
    case CC_LE:
      *dest=(eflags_ZF == 1 || (eflags_SF != eflags_OF));
      break;
    case CC_P: panic("nemu does not have PF");
    default: panic("error in rtl_setcc");
    
  }

  if (invert) {
    rtl_xori(dest, dest, 0x1);
  }
}
