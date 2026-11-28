#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "user/user.h"

static void
clear_screen(void)
{
  printf("\033[H\033[2J\033[3J");
}

static int
read_text(const char *path, char *buf, int max)
{
  int fd = open(path, O_RDONLY);
  int n;

  if (fd < 0)
    return -1;
  n = read(fd, buf, max - 1);
  close(fd);
  if (n < 0)
    return -1;
  buf[n] = '\0';
  return n;
}

static int
is_digits(const char *s)
{
  if (*s == '\0')
    return 0;
  while (*s) {
    if (*s < '0' || *s > '9')
      return 0;
    s++;
  }
  return 1;
}

static int
starts_with(const char *s, const char *prefix)
{
  while (*prefix) {
    if (*s != *prefix)
      return 0;
    s++;
    prefix++;
  }
  return 1;
}

static void
parse_field(const char *buf, const char *key, char *out, int max)
{
  const char *p = buf;
  int klen = strlen(key);

  out[0] = '\0';
  while (*p) {
    if ((p == buf || *(p - 1) == '\n') && starts_with(p, key)) {
      p += klen;
      while (*p == ' ' || *p == '\t')
        p++;
      int i = 0;
      while (*p && *p != '\n' && i < max - 1)
        out[i++] = *p++;
      out[i] = '\0';
      return;
    }
    p++;
  }
}

static void
print_col(const char *text, int width)
{
  int len = strlen(text);

  printf("%s", text);
  while (len++ < width)
    printf(" ");
}

static void
trim_line(char *text)
{
  int i = 0;

  while (text[i] && text[i] != '\n' && text[i] != '\r')
    i++;
  text[i] = '\0';
}

static void
print_process(const char *pid)
{
  char path[64];
  char buf[256];
  char name[32], ppid[16], state[16], size[16];
  int pos = 0, n;

  const char *prefix = "/proc/";
  const char *suffix = "/status";
  for (int i = 0; prefix[i]; i++)
    path[pos++] = prefix[i];
  for (int i = 0; pid[i]; i++)
    path[pos++] = pid[i];
  for (int i = 0; suffix[i]; i++)
    path[pos++] = suffix[i];
  path[pos] = '\0';

  n = read_text(path, buf, sizeof(buf));
  if (n < 0)
    return;

  parse_field(buf, "PPid:", ppid, sizeof(ppid));
  parse_field(buf, "State:", state, sizeof(state));
  parse_field(buf, "Size:", size, sizeof(size));
  parse_field(buf, "Name:", name, sizeof(name));

  print_col(pid, 8);
  print_col(ppid, 8);
  print_col(state, 12);
  print_col(size, 12);
  printf("%s\n", name);
}

int
main(int argc, char *argv[])
{
  int refreshes = 0;
  int interval = 10;
  int round = 0;

  if (argc > 1)
    refreshes = atoi(argv[1]);
  if (argc > 2)
    interval = atoi(argv[2]);
  if (interval < 1)
    interval = 1;

  printf("\033[?1049h\033[?25l");

  for (;;) {
    char uptime[128], meminfo[128];
    char memfree[32];
    int fd;
    struct dirent de;
    char name[DIRSIZ + 1];

    clear_screen();
    printf("XV6 TOP  uptime: ");
    if (read_text("/proc/uptime", uptime, sizeof(uptime)) >= 0) {
      trim_line(uptime);
      printf("%s ticks", uptime);
    } else {
      printf("unknown");
    }

    printf("  interval: %d ticks\n", interval);

    if (read_text("/proc/meminfo", meminfo, sizeof(meminfo)) >= 0) {
      parse_field(meminfo, "MemFree:", memfree, sizeof(memfree));
      trim_line(memfree);
      {
        int i = strlen(memfree);
        while (i > 0 && memfree[i - 1] == ' ')
          i--;
        if (i >= 2 && memfree[i - 1] == 'B') {
          i--;
          while (i > 0 && memfree[i - 1] == ' ')
            i--;
          memfree[i] = '\0';
        }
      }
      printf("MemFree: %s bytes\n", memfree);
    } else {
      printf("MemFree: unknown\n");
    }

    printf("\n");
    print_col("PID", 8);
    print_col("PPID", 8);
    print_col("STATE", 12);
    print_col("SIZE", 12);
    printf("NAME\n");

    fd = open("/proc", O_RDONLY);
    if (fd < 0) {
      printf("top: cannot open /proc\n");
    } else {
      while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0)
          continue;
        memmove(name, de.name, DIRSIZ);
        name[DIRSIZ] = '\0';
        if (is_digits(name))
          print_process(name);
      }
      close(fd);
    }

    printf("\nPress Ctrl-C to exit\033[K");
    round++;
    if (refreshes > 0 && round >= refreshes)
      break;
    if (pause(interval) < 0)
      break;
  }

  printf("\033[0m\033[?25h\033[?1049l");
  exit(0);
}
