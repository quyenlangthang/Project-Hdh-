#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
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
    assert_test(0, "Doc voi buffer nho (Mo file /proc/uptime)");
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

// Kiểm thử phân giải cây thư mục 2 cấp và fstat
void
test_two_level_lookup(void)
{
  struct stat st;
  int fd;

  // Cấp 0: /proc
  fd = open("/proc", O_RDONLY);
  if (fd < 0 || fstat(fd, &st) < 0 || st.type != T_DIR || st.ino != 9000) {
    assert_test(0, "Lookup /proc va xac dinh T_DIR");
    if (fd >= 0)
      close(fd);
  } else {
    close(fd);
    assert_test(1, "Lookup /proc va xac dinh T_DIR");
  }

  // Cấp 1: /proc/1
  fd = open("/proc/1", O_RDONLY);
  if (fd < 0 || fstat(fd, &st) < 0 || st.type != T_DIR || st.ino != 9100) {
    assert_test(0, "Lookup /proc/1 va xac dinh T_DIR");
    if (fd >= 0)
      close(fd);
  } else {
    close(fd);
    assert_test(1, "Lookup /proc/1 va xac dinh T_DIR");
  }

  // Cấp 2: /proc/1/status
  fd = open("/proc/1/status", O_RDONLY);
  if (fd < 0 || fstat(fd, &st) < 0 || st.type != T_FILE || st.ino != 9200) {
    assert_test(0, "Lookup /proc/1/status va xac dinh T_FILE");
    if (fd >= 0)
      close(fd);
  } else {
    close(fd);
    assert_test(1, "Lookup /proc/1/status va xac dinh T_FILE");
  }

  // Cấp 2: /proc/1/maps
  fd = open("/proc/1/maps", O_RDONLY);
  if (fd < 0 || fstat(fd, &st) < 0 || st.type != T_FILE || st.ino != 9300) {
    assert_test(0, "Lookup /proc/1/maps va xac dinh T_FILE");
    if (fd >= 0)
      close(fd);
  } else {
    close(fd);
    assert_test(1, "Lookup /proc/1/maps va xac dinh T_FILE");
  }

  // Từ chối đường dẫn không tồn tại hoặc vượt quá 2 cấp
  int bad1 = open("/proc/9999", O_RDONLY);
  int bad2 = open("/proc/1/status/extra", O_RDONLY);
  if (bad1 < 0 && bad2 < 0) {
    assert_test(1, "Tu choi duong dan khong hop le / vuot cap");
  } else {
    if (bad1 >= 0)
      close(bad1);
    if (bad2 >= 0)
      close(bad2);
    assert_test(0, "Tu choi duong dan khong hop le / vuot cap");
  }
}

// Kiểm thử duyệt thư mục ảo
void
test_readdir(void)
{
  int fd = open("/proc", O_RDONLY);
  if (fd < 0) {
    assert_test(0, "Duyet thu muc /proc");
    return;
  }

  struct dirent de;
  int has_dot = 0, has_dotdot = 0, has_uptime = 0, has_meminfo = 0,
      has_pid1 = 0;

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (strcmp(de.name, ".") == 0)
      has_dot = 1;
    else if (strcmp(de.name, "..") == 0)
      has_dotdot = 1;
    else if (strcmp(de.name, "uptime") == 0)
      has_uptime = 1;
    else if (strcmp(de.name, "meminfo") == 0)
      has_meminfo = 1;
    else if (strcmp(de.name, "1") == 0)
      has_pid1 = 1;
  }
  close(fd);

  int proc_ok = has_dot && has_dotdot && has_uptime && has_meminfo && has_pid1;
  assert_test(proc_ok, "Duyet thu muc /proc (., .., uptime, meminfo, PID)");

  // Duyệt /proc/1
  fd = open("/proc/1", O_RDONLY);
  if (fd < 0) {
    assert_test(0, "Duyet thu muc /proc/1");
    return;
  }

  has_dot = 0;
  has_dotdot = 0;
  int has_status = 0, has_maps = 0;

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (strcmp(de.name, ".") == 0)
      has_dot = 1;
    else if (strcmp(de.name, "..") == 0)
      has_dotdot = 1;
    else if (strcmp(de.name, "status") == 0)
      has_status = 1;
    else if (strcmp(de.name, "maps") == 0)
      has_maps = 1;
  }
  close(fd);

  int pid_ok = has_dot && has_dotdot && has_status && has_maps;
  assert_test(pid_ok, "Duyet thu muc /proc/1 (., .., status, maps)");
}

// Kiểm thử kiểm soát quyền chỉ đọc
void
test_read_only_protection(void)
{
  int w1 = open("/proc/uptime", O_WRONLY);
  int w2 = open("/proc/uptime", O_RDWR);
  int w3 = open("/proc/uptime", O_CREATE | O_RDONLY);
  int w4 = open("/proc/uptime", O_TRUNC | O_RDONLY);
  int w5 = open("/proc/1/status", O_WRONLY);
  int w6 = open("/proc/newfile", O_CREATE | O_RDWR);

  if (w1 >= 0)
    close(w1);
  if (w2 >= 0)
    close(w2);
  if (w3 >= 0)
    close(w3);
  if (w4 >= 0)
    close(w4);
  if (w5 >= 0)
    close(w5);
  if (w6 >= 0)
    close(w6);

  int open_flags_blocked =
    (w1 < 0 && w2 < 0 && w3 < 0 && w4 < 0 && w5 < 0 && w6 < 0);
  assert_test(open_flags_blocked, "Tu choi O_WRONLY/O_RDWR/O_CREATE/O_TRUNC");

  int unl = unlink("/proc/uptime");
  int mkd = mkdir("/proc/newdir");
  int lnk = link("/proc/uptime", "uptimelink");

  assert_test(unl < 0, "Chan thao tac unlink /proc");
  assert_test(mkd < 0, "Chan thao tac mkdir /proc");
  assert_test(lnk < 0, "Chan thao tac link /proc");
}

// Kiểm thử rò rỉ Inode cache (NINODE = 50, lặp 150 lần mở và đóng)
void
test_inode_cache_leak(void)
{
  int ok = 1;
  for (int i = 0; i < 150; i++) {
    int fd1 = open("/proc/uptime", O_RDONLY);
    int fd2 = open("/proc/meminfo", O_RDONLY);
    int fd3 = open("/proc/1/status", O_RDONLY);
    if (fd1 < 0 || fd2 < 0 || fd3 < 0) {
      ok = 0;
      if (fd1 >= 0)
        close(fd1);
      if (fd2 >= 0)
        close(fd2);
      if (fd3 >= 0)
        close(fd3);
      break;
    }
    close(fd1);
    close(fd2);
    close(fd3);
  }
  assert_test(ok, "Khong ro ri Inode cache (lap 150 lan mo/dong file)");
}

int
main(int argc, char *argv[])
{
  printf("=======================================\n");
  printf("     BO KIEM THU PROCFS (XV6)          \n");
  printf("=======================================\n");

  test_read_file("/proc/uptime", "Doc file /proc/uptime");
  test_read_file("/proc/meminfo", "Doc file /proc/meminfo");
  test_read_file("/proc/1/status", "Doc file /proc/1/status");
  test_read_file("/proc/1/maps", "Doc file /proc/1/maps");
  test_small_buffer();
  test_two_level_lookup();
  test_readdir();
  test_read_only_protection();
  test_inode_cache_leak();

  printf("=======================================\n");
  exit(0);
}
