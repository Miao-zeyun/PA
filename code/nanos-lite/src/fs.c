#include "fs.h"

extern void ramdisk_read(void *buf, off_t offset, size_t len);
extern void ramdisk_write(const void *buf, off_t offset, size_t len);
extern void fb_write(const void *buf, off_t offset, size_t len);
extern void dispinfo_read(void *buf, off_t offset, size_t len);
extern size_t events_read(void *buf, size_t len);

typedef struct {
  char *name;
  size_t size;
  off_t disk_offset;
  off_t open_offset;
} Finfo;

enum {FD_STDIN, FD_STDOUT, FD_STDERR, FD_FB, FD_EVENTS, FD_DISPINFO, FD_NORMAL};

/* This is the information about all files in disk. */
static Finfo file_table[] __attribute__((used)) = {
  {"stdin (note that this is not the actual stdin)", 0, 0},
  {"stdout (note that this is not the actual stdout)", 0, 0},
  {"stderr (note that this is not the actual stderr)", 0, 0},
  [FD_FB] = {"/dev/fb", 0, 0},
  [FD_EVENTS] = {"/dev/events", 0, 0},
  [FD_DISPINFO] = {"/proc/dispinfo", 128, 0},
#include "files.h"
};

#define NR_FILES (sizeof(file_table) / sizeof(file_table[0]))

void init_fs() {
  // TODO: initialize the size of /dev/fb
  file_table[3].size = _screen.width * _screen.height * sizeof(uint32_t);
  file_table[3].open_offset = 0;
}

int fs_open(const char *pathname, int flags, int mode)
{
  //Log("fs_open pathname:%s",pathname);
  for(int i = 0; i < NR_FILES; i++)
  {
    if (strcmp(pathname, file_table[i].name) == 0)
    {
      file_table[i].open_offset = 0;
      return i;
    }
  }
  panic("illegal pathname(failure in fs_open)");
  return -1;
}

ssize_t fs_read(int fd, void *buf, size_t len)
{
  //Log("fs_read fd:%d,buf:%d,len:%d,size = %d", fd, buf, len, file_table[fd].size);
  switch(fd)
  {
    case FD_STDIN:
    case FD_STDOUT:
    case FD_STDERR:
      return -1;
    case FD_EVENTS:
    {
      len = events_read((void *)buf, len);
      break;
    }
    case FD_DISPINFO:
    {
      if(file_table[fd].open_offset + len >= fs_filesz(fd))
        len = fs_filesz(fd) - file_table[fd].open_offset;
      dispinfo_read((void *)buf, file_table[fd].open_offset, len);
      file_table[fd].open_offset += len;
      break;
    }
    default:
    {
      off_t offset = file_table[fd].disk_offset + file_table[fd].open_offset;
      if(file_table[fd].open_offset + len >= fs_filesz(fd))
        len = fs_filesz(fd) - file_table[fd].open_offset;
      ramdisk_read((void *)buf, offset, len);
      file_table[fd].open_offset += len;
      break;
    }
  }
  return len;
}

ssize_t fs_write(int fd, const void *buf, size_t len)
{
  //Log("fs_write fd:%d,buf:%d,len:%d", fd, buf, len);
  switch(fd)
  {
    case FD_STDIN:
      return -1;
    case FD_STDOUT:
    case FD_STDERR:
    {
      for(int i = 0; i < len; i++)
        _putc(((char *)buf)[i]);
      break;
    }
    case FD_FB:
    {
      if(fs_filesz(fd) <= len + file_table[fd].open_offset)
        len = fs_filesz(fd) - file_table[fd].open_offset;
      fb_write(buf, file_table[fd].open_offset, len);
      file_table[fd].open_offset += len;
      break;
    }
    default:
    {
      off_t offset = file_table[fd].disk_offset + file_table[fd].open_offset;
      if(file_table[fd].open_offset + len >= fs_filesz(fd))
        len = fs_filesz(fd) - file_table[fd].open_offset;
      ramdisk_write(buf, offset, len);
      file_table[fd].open_offset += len;
      break;
    }
  }
  return len;
}

off_t fs_lseek(int fd , off_t offset, int whence)
{
  //Log("fs_lseek fd:%d,offset:%d,whence:%d", fd, offset, whence);
  switch (whence)
  {
    case SEEK_SET:
    {
      if (offset >=0 && offset <= fs_filesz(fd))
      {
        file_table[fd].open_offset = offset;
        return file_table[fd].open_offset;
      }
      else
        return -1;
    }
    case SEEK_CUR:
    {
      if (file_table[fd].open_offset + offset <= fs_filesz(fd))
      {
        file_table[fd].open_offset += offset;
        return file_table[fd].open_offset;
      }
      else
        return -1;
    }
    case SEEK_END:
    {
      file_table[fd].open_offset = fs_filesz(fd) + offset;
      return file_table[fd].open_offset;
    }
    default:
     return -1;
  }
}

int fs_close(int fd)
{
  //Log("fs_close fd:%d",fd);
  return 0;
}

size_t fs_filesz(int fd)
{
  return file_table[fd].size;
}
