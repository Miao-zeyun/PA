#include "common.h"

#define NAME(key) \
  [_KEY_##key] = #key,

static const char *keyname[256] __attribute__((used)) = {
  [_KEY_NONE] = "NONE",
  _KEYS(NAME)
};

int current_game = 0;

size_t events_read(void *buf, size_t len) {
  int key = _read_key();
  if(key == _KEY_NONE)
    sprintf(buf, "t %d\n", _uptime());
  else if ((uint32_t)key & 0x8000)
    sprintf(buf, "ku %s\n", keyname[key & ~0x8000]);
  else
  {
    sprintf(buf, "kd %s\n", keyname[key]);
    if (key == _KEY_F12)
    {
      current_game = (current_game == 0 ? 2 : 0);
      Log("switch current game to %d", current_game);
    }
  }
  return strlen(buf);
}

static char dispinfo[128] __attribute__((used));

void dispinfo_read(void *buf, off_t offset, size_t len) {
  memcpy(buf, dispinfo + offset, len);
}

void fb_write(const void *buf, off_t offset, size_t len) {
  int size = sizeof(uint32_t);
  offset /= size;
  len /= size;
  int x = offset % _screen.width;
  int y = offset / _screen.width;
  //_draw_rect(buf, x, y, len, 1);
  //return;
  if (len + x <= _screen.width)
  {
    _draw_rect(buf, x, y, len, 1);
    return;
  }
  else if (len + x <= 2 * _screen.width)
  {
    int first_len = _screen.width - x;
    _draw_rect(buf, x, y, first_len, 1);
    _draw_rect(buf + first_len * size, 0, y + 1, len - first_len, 1);
    return;
  }
  else
  {
    int first_len = _screen.width - x;
    int other_len = (len - first_len) / _screen.width;
    int last_len = len - first_len - other_len * _screen.width;
    _draw_rect(buf, x, y, first_len, 1);
    _draw_rect(buf + first_len * size, 0, y + 1, _screen.width, other_len);
    _draw_rect(buf + (first_len + other_len * _screen.width) * size, 0, y + other_len, last_len, 1);
    return;
  }
}

void init_device() {
  _ioe_init();

  // TODO: print the string to array `dispinfo` with the format
  // described in the Navy-apps convention
  //Log("WIDTH:%d,HEIGHT:%d.", _screen.width, _screen.height);
  sprintf(dispinfo, "WIDTH:%d\nHEIGHT:%d\n", _screen.width, _screen.height);
}
