#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

void
assert_test(int condition, char *test_name)
{
  if (condition) {
    printf("[PASS] %s\n", test_name);
  } else {
    printf("[FAIL] %s\n", test_name);
  }
}

// Kiểm thử đọc nội dung file bất kỳ trong /proc
void
test_read_file(char *path, char *test_name)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    assert_test(0, test_name);
    return;
  }

  char buf[256];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);

  if (n > 0) {
    buf[n] = '\0';
    assert_test(1, test_name);
  } else {
    assert_test(0, test_name);
  }
}

// Kiểm thử trường hợp đọc với buffer dung lượng nhỏ (Chunked read)
void
test_small_buffer(void)
{
  int fd = open("/proc/uptime", O_RDONLY);
  if (fd < 0) {
    assert_test(0, "Doc voi buffer nho (Mơ file /proc/uptime)");
    return;
  }

  char buf[4];
  int total_read = 0;
  int n;

  while ((n = read(fd, buf, 3)) > 0) {
    total_read += n;
  }
  close(fd);

  assert_test(total_read > 0, "Doc voi buffer nho (Read chunking)");
}

int
main(int argc, char *argv[])
{
  printf("=======================================\n");
  printf("     BO KIEM THU PROCFS (XV6)          \n");
  printf("=======================================\n");

  test_read_file("/proc/uptime", "Doc file /proc/uptime");
  test_read_file("/proc/meminfo", "Doc file /proc/meminfo");
  test_small_buffer();

  printf("=======================================\n");
  exit(0);
}