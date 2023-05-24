#include "FLOAT.h"
#include <stdint.h>
#include <assert.h>

FLOAT F_mul_F(FLOAT a, FLOAT b) {
  //assert(0);
  int64_t temp_result = (int64_t)a * (int64_t)b;
  FLOAT result = (FLOAT)(temp_result >> 16);
  return result;
}

FLOAT F_div_F(FLOAT a, FLOAT b) {
  //assert(0);
  assert(b != 0);
  
  if(a & 0x80000000)
    a = -a;
  if(b & 0x80000000)
    b = -b;
  
  FLOAT result = a / b;
  int temp = a % b;
  
  for (int i = 0; i < 16; i++)
  {
    result <<= 1;
    temp <<= 1;
    if (temp >= b)
    {
      temp -= b;
      result++;
    }
  }
  
  if((a ^ b) & 0x80000000)
    return -result;
  else
    return result;
}

typedef union
{
  struct
  {
    uint32_t fraction : 23;
    uint32_t exponent : 8;
    uint32_t sign     : 1;
  };
  uint32_t value;
} Float;

FLOAT f2F(float a) {
  /* You should figure out how to convert `a' into FLOAT without
   * introducing x87 floating point instructions. Else you can
   * not run this code in NEMU before implementing x87 floating
   * point instructions, which is contrary to our expectation.
   *
   * Hint: The bit representation of `a' is already on the
   * stack. How do you retrieve it to another variable without
   * performing arithmetic operations on it directly?
   */

  //assert(0);

  void *voidp_temp = (void *)&a;
  uint32_t *intp_temp = (uint32_t *)voidp_temp;
  Float f;
  f.value = *intp_temp;
  
  FLOAT result = f.fraction | 0x800000;
  
  int exp = (int)f.exponent - 127;
  if(exp >= 7 && exp < 15)
    result <<= (exp - 7);
  else if(exp < 7 && exp > -17)
    result >>= (7 - exp);
  else
    assert(0);
  
  if(f.sign)
    return -result;
  else
    return result;
}

FLOAT Fabs(FLOAT a) {
  //assert(0);
  if(a & 0x80000000)
    return -a;
  else
    return a;
}

/* Functions below are already implemented */

FLOAT Fsqrt(FLOAT x) {
  FLOAT dt, t = int2F(2);

  do {
    dt = F_div_int((F_div_F(x, t) - t), 2);
    t += dt;
  } while(Fabs(dt) > f2F(1e-4));

  return t;
}

FLOAT Fpow(FLOAT x, FLOAT y) {
  /* we only compute x^0.333 */
  FLOAT t2, dt, t = int2F(2);

  do {
    t2 = F_mul_F(t, t);
    dt = (F_div_F(x, t2) - t) / 3;
    t += dt;
  } while(Fabs(dt) > f2F(1e-4));

  return t;
}
