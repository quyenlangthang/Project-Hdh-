#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "user/user.h"

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
print_process(const char *pid)
{
  char buf[256];
  char name[32], ppid[16], state[16], size[16];
  int pos = 0, fd, n;

  char status_path[64];
  pos = 0;
  const char *prefix = "/proc/";
  const char *suffix = "/status";
  for (int i = 0; prefix[i]; i++)
    status_path[pos++] = prefix[i];
  for (int i = 0; pid[i]; i++)
    status_path[pos++] = pid[i];
  for (int i = 0; suffix[i]; i++)
    status_path[pos++] = suffix[i];
  status_path[pos] = '\0';

  fd = open(status_path, O_RDONLY);
  if (fd < 0)
    return;
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return;
  buf[n] = '\0';

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
main(void)
{
  int fd;
  struct dirent de;
  char name[DIRSIZ + 1];

  fd = open("/proc", O_RDONLY);
  if (fd < 0) {
    fprintf(2, "ps: cannot open /proc\n");
    exit(1);
  }

  print_col("PID", 8);
  print_col("PPID", 8);
  print_col("STATE", 12);
  print_col("SIZE", 12);
  printf("NAME\n");

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = '\0';
    if (is_digits(name))
      print_process(name);
  }

  close(fd);
  exit(0);
}
