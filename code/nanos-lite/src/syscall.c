#include "common.h"
#include "syscall.h"
#include "fs.h"

uintptr_t sys_write(int fd, const void *buf, size_t count)
{
  //Log("sys_write function");
  if (fd == 1 || fd == 2)
  {
    for(int i = 0; i < count; i++)
      _putc(((char *)buf)[i]);
    return count;
  }
  else
  {
    panic("Unhandled fd=%d in sys_write", fd);
    return -1;
  }
}

extern int mm_brk(uint32_t new_brk);

_RegSet* do_syscall(_RegSet *r) {
  uintptr_t a[4];
  a[0] = SYSCALL_ARG1(r);
  a[1] = SYSCALL_ARG2(r);
  a[2] = SYSCALL_ARG3(r);
  a[3] = SYSCALL_ARG4(r);

  switch (a[0]) {
    case SYS_none:
      a[0] = 1;
      break;
      
    case SYS_exit:
      _halt(a[1]);
      break;
      
    case SYS_write:
      //Log("sys_write");
      //a[0] = sys_write((int)a[1], (void *)a[2], (size_t)a[3]);
      a[0] = fs_write((int)a[1], (void *)a[2], (size_t)a[3]);
      break;
      
    case SYS_brk:
      //a[0] = 0;
      a[0] = mm_brk((uint32_t)a[1]);
      break;
      
    case SYS_open:
      a[0] = fs_open((char *)a[1], (int)a[2], (int)a[3]);
      break;
      
    case SYS_read:
      a[0] = fs_read((int)a[1], (void *)a[2], (size_t)a[3]);
      break;
      
    case SYS_close:
      a[0] = fs_close((int)a[1]);
      break;
      
    case SYS_lseek:
      a[0] = fs_lseek((int)a[1], (off_t)a[2], (int)a[3]);
      break;
      
    default: panic("Unhandled syscall ID = %d", a[0]);
  }
  SYSCALL_ARG1(r) = a[0];

  return NULL;
}
