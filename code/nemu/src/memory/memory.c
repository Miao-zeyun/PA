#include "nemu.h"
#include "device/mmio.h"
#include "memory/mmu.h"

#define PMEM_SIZE (128 * 1024 * 1024)

#define pmem_rw(addr, type) *(type *)({\
    Assert(addr < PMEM_SIZE, "physical address(0x%08x) is out of bound", addr); \
    guest_to_host(addr); \
    })

uint8_t pmem[PMEM_SIZE];

/* Memory accessing interfaces */

paddr_t page_translate(vaddr_t addr, bool write);

uint32_t paddr_read(paddr_t addr, int len) {
  int port = is_mmio(addr);
  if(port != -1)
    return mmio_read(addr, len, port);
  else
    return pmem_rw(addr, uint32_t) & (~0u >> ((4 - len) << 3));
}

void paddr_write(paddr_t addr, int len, uint32_t data) {
  int port = is_mmio(addr);
  if(port != -1)
    mmio_write(addr, len, data, port);
  else
    memcpy(guest_to_host(addr), &data, len);
}

uint32_t vaddr_read(vaddr_t addr, int len) {
  if ((addr & 0xfffff000) != ((addr + len - 1) & 0xfffff000))
  {
    //panic("data cross page boundary(in vaddr_read())");
    int first_pg_len = 0x1000 - (int)((uint32_t)addr & 0xfff);
    int second_pg_len = len - first_pg_len;
    uint32_t data = 0;
    
    paddr_t first_paddr = page_translate(addr, false);
    data = paddr_read(first_paddr, first_pg_len);
    
    vaddr_t second_vaddr = addr + first_pg_len;
    paddr_t second_paddr = page_translate(second_vaddr, false);
    data = data | (paddr_read(second_paddr, second_pg_len) << (first_pg_len << 3));
    
    return data;
  }
  else
  {
    //printf("vaddr = 0x%08x(vaddr_read()) ", addr);
    paddr_t paddr = page_translate(addr, false);
    //printf("paddr = 0x%08x(vaddr_read())\n", paddr);
    return paddr_read(paddr, len);
  }
  //return paddr_read(addr, len);
}

void vaddr_write(vaddr_t addr, int len, uint32_t data) {
  if ((addr & 0xfffff000) != ((addr + len - 1) & 0xfffff000))
  {
    //panic("data cross page boundary(in vaddr_write())");
    int first_pg_len = 0x1000 - (int)(addr & 0xfff);
    int second_pg_len = len - first_pg_len;
    
    uint32_t first_pg_data = (data << (second_pg_len << 3)) >> (second_pg_len << 3);
    paddr_t first_paddr = page_translate(addr, true);
    paddr_write(first_paddr, first_pg_len, first_pg_data);
    
    uint32_t second_pg_data = data >> (first_pg_len << 3);
    vaddr_t second_vaddr = addr + first_pg_len;
    paddr_t second_paddr = page_translate(second_vaddr, true);
    paddr_write(second_paddr, second_pg_len, second_pg_data);
  }
  else
  {
    //printf("vaddr = 0x%08x(vaddr_write()) ", addr);
    paddr_t paddr = page_translate(addr, true);
    //printf("paddr = 0x%08x(vaddr_write())\n", paddr);
    paddr_write(paddr, len, data);
  }
  //paddr_write(addr, len, data);
}

paddr_t page_translate(vaddr_t addr, bool write)
{
  if (cpu.cr0.protect_enable && cpu.cr0.paging)
  {
    PDE pde;
    PTE pte;
    
    PDE *pg_dic_base = (PDE*)((uint32_t)cpu.cr3.val & 0xfffff000);
    uint32_t pg_dic_index = ((uint32_t)addr >> 22) & 0x3ff;
    pde.val = paddr_read((paddr_t) &pg_dic_base[pg_dic_index], 4);
    Assert(pde.present, "pde.present = 0");
    pde.accessed = 1;
    
    PTE *pg_tab_base = (PTE*)((uint32_t)pde.val & 0xfffff000);
    uint32_t pg_tab_index = ((uint32_t)addr >> 12) & 0x3ff;
    pte.val = paddr_read((paddr_t) &pg_tab_base[pg_tab_index], 4);
    Assert(pte.present, "pte.present = 0");
    pte.accessed = 1;
    
    if(write)
      pte.dirty = 1;
    paddr_t paddr = ((uint32_t)pte.val & 0xfffff000) | ((uint32_t)addr & 0xfff);
    return paddr;
  }
  else
    return addr;
}
