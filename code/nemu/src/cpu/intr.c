#include "cpu/exec.h"
#include "memory/mmu.h"

void raise_intr(uint8_t NO, vaddr_t ret_addr) {
  /* TODO: Trigger an interrupt/exception with ``NO''.
   * That is, use ``NO'' to index the IDT.
   */
  rtl_push(&cpu.eflags_init);
  cpu.eflags.IF = 0;
  
  t0 = cpu.cs;
  rtl_push(&t0);
  rtl_push(&ret_addr);
  
  vaddr_t addr;
  addr = cpu.idtr.base + 8 * NO;
  uint32_t offset_l = vaddr_read(addr, 2);
  uint32_t offset_h = vaddr_read(addr + 6, 2);
  decoding.jmp_eip = (offset_h << 16) + offset_l;
  decoding.is_jmp = 1;
}

void dev_raise_intr() {
  cpu.INTR = true;
}
