#include "cpu/exec.h"

make_EHelper(test) {
  rtl_and(&t0, &id_dest->val, &id_src->val);
  
  rtl_set_CF(&tzero);
  rtl_set_OF(&tzero);
  rtl_update_ZFSF(&t0, id_dest->width);

  print_asm_template2(test);
}

make_EHelper(and) {
  rtl_and(&t0, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t0);
  
  rtl_set_CF(&tzero);
  rtl_set_OF(&tzero);
  rtl_update_ZFSF(&t0, id_dest->width);

  print_asm_template2(and);
}

make_EHelper(xor) {
  rtl_xor(&t0, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t0);
  
  rtl_set_CF(&tzero);
  rtl_set_OF(&tzero);
  rtl_update_ZFSF(&t0, id_dest->width);

  print_asm_template2(xor);
}

make_EHelper(or) {
  rtl_or(&t0, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t0);
  
  rtl_set_CF(&tzero);
  rtl_set_OF(&tzero);
  rtl_update_ZFSF(&t0, id_dest->width);

  print_asm_template2(or);
}

make_EHelper(sar) {
  rtl_sar(&t0, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t0);
  
  rtl_update_ZFSF(&t0, id_dest->width);
  
  // unnecessary to update CF and OF in NEMU

  print_asm_template2(sar);
}

make_EHelper(shl) {
  rtl_shl(&t0, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t0);
  
  rtl_update_ZFSF(&t0, id_dest->width);
  
  // unnecessary to update CF and OF in NEMU

  print_asm_template2(shl);
}

make_EHelper(shr) {
  rtl_shr(&t0, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t0);
  
  rtl_update_ZFSF(&t0, id_dest->width);
  
  // unnecessary to update CF and OF in NEMU

  print_asm_template2(shr);
}

make_EHelper(setcc) {
  uint8_t subcode = decoding.opcode & 0xf;
  rtl_setcc(&t2, subcode);
  operand_write(id_dest, &t2);

  print_asm("set%s %s", get_cc_name(subcode), id_dest->str);
}

make_EHelper(not) {
  rtl_not(&id_dest->val);
  operand_write(id_dest, &id_dest->val);

  print_asm_template1(not);
}

make_EHelper(rol) {
  t0=id_dest->width*8-id_src->val;
  rtl_shr(&t1,&id_dest->val,&t0);
  rtl_shl(&t2,&id_dest->val,&id_src->val);
  rtl_or(&t3,&t1,&t2);
  operand_write(id_dest,&t3);
  
  print_asm_template2(rol);
}

make_EHelper(shrd)
{
  rtl_shr(&t0, &id_dest->val, &id_src->val);
  if(decoding.is_operand_size_16)
    rtl_addi(&t1, &tzero, 16);
  else
    rtl_addi(&t1, &tzero, 32);
  rtl_sub(&t1, &t1, &id_src->val);
  rtl_shl(&t2, &id_src2->val, &t1);
  rtl_or(&t0, &t0, &t2);
  operand_write(id_dest, &t0);
  
  rtl_update_ZFSF(&t0, id_dest->width);
  
  print_asm_template3(shrd);
}

