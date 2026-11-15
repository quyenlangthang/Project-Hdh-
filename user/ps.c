#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int
is_digits(const char *s)
{
  if (s[0] == '\0')
    return 0;
  for (int i = 0; s[i] != '\0'; i++) {
    if (s[i] < '0' || s[i] > '9')
      return 0;
  }
  return 1;
}

int
starts_with(const char *s, const char *prefix)
{
  while (*prefix != '\0') {
    if (*s != *prefix)
      return 0;
    s++;
    prefix++;
  }
  return 1;
}

void
parse_field(const char *buf, const char *key, char *out, int max_len)
{
  int klen = strlen(key);
  const char *p = buf;

  out[0] = '\0';
  while (*p != '\0') {
    if ((p == buf || *(p - 1) == '\n') && starts_with(p, key)) {
      p += klen;
      while (*p == ' ' || *p == '\t') {
        p++;
      }
      int i = 0;
      while (*p != '\0' && *p != '\n' && i < max_len - 1) {
        out[i++] = *p++;
      }
      out[i] = '\0';
      return;
    }
    p++;
  }
}

void
print_col(const char *s, int width)
{
  int len = strlen(s);
  printf("%s", s);
  for (int i = len; i < width; i++) {
    printf(" ");
  }
}

void
print_proc_info(const char *pid_str)
{
  char path[64];
  char buf[256];
  char pid[16], ppid[16], state[16], size[16], name[32];

  int pos = 0;
  const char *prefix = "/proc/";
  const char *suffix = "/status";
  for (int i = 0; prefix[i] != '\0'; i++)
    path[pos++] = prefix[i];
  for (int i = 0; pid_str[i] != '\0'; i++)
    path[pos++] = pid_str[i];
  for (int i = 0; suffix[i] != '\0'; i++)
    path[pos++] = suffix[i];
  path[pos] = '\0';

  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return;

  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return;
  buf[n] = '\0';

  parse_field(buf, "Pid:", pid, sizeof(pid));
  parse_field(buf, "PPid:", ppid, sizeof(ppid));
  parse_field(buf, "State:", state, sizeof(state));
  parse_field(buf, "Size:", size, sizeof(size));
  parse_field(buf, "Name:", name, sizeof(name));

  print_col(pid, 6);
  print_col(ppid, 6);
  print_col(state, 11);
  print_col(size, 10);
  printf("%s\n", name);
}

int
main(int argc, char *argv[])
{
  int fd;
  struct dirent de;
  char name[DIRSIZ + 1];

  fd = open("/proc", O_RDONLY);
  if (fd < 0) {
    fprintf(2, "ps: cannot open /proc\n");
    exit(1);
  }

  print_col("PID", 6);
  print_col("PPID", 6);
  print_col("STATE", 11);
  print_col("SIZE", 10);
  printf("NAME\n");

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;

    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = '\0';

    if (is_digits(name)) {
      print_proc_info(name);
    }
  }

  close(fd);
  exit(0);
}
