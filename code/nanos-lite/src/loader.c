#include "common.h"
#include "memory.h"
#include "fs.h"

#define DEFAULT_ENTRY ((void *)0x8048000)

extern void ramdisk_read(void *buf, off_t offset, size_t len);
extern size_t get_ramdisk_size();

extern void _map(_Protect *p, void *va, void *pa);
//extern void* new_page(void);

uintptr_t loader(_Protect *as, const char *filename) {
  //size_t rsize = get_ramdisk_size();
  //ramdisk_read(DEFAULT_ENTRY, 0, rsize);
  
  int fd = fs_open(filename, 0, 0);
  Log("loader fd:%d",fd);
  size_t rsize = fs_filesz(fd);
  //fs_read(fd, DEFAULT_ENTRY, rsize);
  //fs_close(fd);
  
  int page_num = rsize / PGSIZE;
  int last_bytes = rsize % PGSIZE;
  void *page;
  int i;
  for(i = 0; i < page_num; i++)
  {
    page = new_page();
    _map(as, DEFAULT_ENTRY + i * PGSIZE, page);
    fs_read(fd, page, PGSIZE);
  }
  if(last_bytes != 0)
  {
    page = new_page();
    _map(as, DEFAULT_ENTRY + i * PGSIZE, page);
    fs_read(fd, page, last_bytes);
  }
  fs_close(fd);
  
  return (uintptr_t)DEFAULT_ENTRY;
}
