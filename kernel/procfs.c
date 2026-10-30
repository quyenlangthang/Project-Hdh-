#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "stat.h"

// Biến ticks và khóa đồng bộ thời gian từ kernel/trap.c
extern uint ticks;
extern struct spinlock tickslock;

// Inode ảo tĩnh đại diện cho tệp /proc/uptime trong RAM
static struct inode proc_uptime_inode;
static struct inode proc_meminfo_inode;
static int proc_inited = 0;

// Hàm phụ trợ so sánh 2 chuỗi ký tự an toàn trong kernel
static int
procfs_streq(const char *s1, const char *s2)
{
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return (uchar)*s1 - (uchar)*s2;
}

// Hàm phụ trợ chuyển đổi số nguyên dương sang chuỗi ASCII
static int
procfs_itoa(char *buf, uint n)
{
  char tmp[16];
  int i = 0, j = 0;

  if (n == 0) {
    buf[j++] = '0';
    buf[j] = '\0';
    return j;
  }

  while (n > 0) {
    tmp[i++] = (n % 10) + '0';
    n /= 10;
  }

  while (i > 0) {
    buf[j++] = tmp[--i];
  }
  buf[j] = '\0';
  return j;
}

// 1. Hàm tra cứu và cấp phát Inode ảo cho ProcFS
struct inode *
procfs_lookup(char *path)
{
  // Khởi tạo Inode ảo một lần duy nhất
  if (proc_inited == 0) {
    initsleeplock(&proc_uptime_inode.lock, "proc_uptime");
    proc_uptime_inode.dev = 0;
    proc_uptime_inode.inum = 9999; // Định danh inum ảo đặc biệt
    proc_uptime_inode.ref = 0;
    proc_uptime_inode.type = T_PROC;
    proc_uptime_inode.valid = 1; // valid = 1 để ilock() không đọc block từ đĩa
    proc_uptime_inode.nlink = 1;
    initsleeplock(&proc_meminfo_inode.lock, "proc_meminfo");
    proc_meminfo_inode.dev = 0;
    proc_meminfo_inode.inum = 9998;
    proc_meminfo_inode.ref = 0;
    proc_meminfo_inode.type = T_PROC;
    proc_meminfo_inode.valid = 1;
    proc_meminfo_inode.nlink = 1;
    proc_inited = 1;
  }

  // Khớp đường dẫn /proc/uptime hoặc proc/uptime
  if (procfs_streq(path, "/proc/uptime") == 0 ||
      procfs_streq(path, "proc/uptime") == 0) {
    proc_uptime_inode.ref++;
    return &proc_uptime_inode;
  }
if (procfs_streq(path, "/proc/meminfo") == 0 ||
      procfs_streq(path, "proc/meminfo") == 0) {
    proc_meminfo_inode.ref++;
    return &proc_meminfo_inode;
  }
  return 0; // Trả về null nếu không tìm thấy tệp
}

// 2. Logic sinh dữ liệu on-demand cho /proc/uptime
int
procfs_read_uptime(int user_dst, uint64 dst, uint off, uint n)
{
  char buf[32];

  // Chiếm khóa tickslock an toàn trước khi đọc ticks
  acquire(&tickslock);
  uint cur_ticks = ticks;
  release(&tickslock);

  // Định dạng chuỗi: "<ticks>\n"
  int len = procfs_itoa(buf, cur_ticks);
  buf[len++] = '\n';
  buf[len] = '\0';

  // Nếu vị trí đọc (offset) vượt quá độ dài dữ liệu hiện có -> trả về 0 (EOF)
  if (off >= len)
    return 0;

  // Tính số lượng byte thực tế cần chép
  uint m = len - off;
  if (m > n)
    m = n;

  // Sao chép an toàn từ buffer kernel sang buffer của user process
  if (either_copyout(user_dst, dst, buf + off, m) == -1)
    return -1;

  return m;
}

int
procfs_read_meminfo(int user_dst, uint64 dst, uint off, uint n)
{
  char buf[64];
  uint64 free_b = freemem();
  int len = 0;

  char *prefix = "MemFree: ";
  while (*prefix) {
    buf[len++] = *prefix++;
  }

  len += procfs_itoa(buf + len, free_b);
  buf[len++] = '\n';
  buf[len] = '\0';

  if (off >= len)
    return 0;

  uint m = len - off;
  if (m > n)
    m = n;

  if (either_copyout(user_dst, dst, buf + off, m) == -1)
    return -1;

  return m;
}
// 3. Hàm điều phối đọc chính của hệ thống tệp ảo ProcFS
int
procfs_read(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  if (ip == &proc_uptime_inode || ip->inum == 9999) {
    return procfs_read_uptime(user_dst, dst, off, n);
  }
if (ip == &proc_meminfo_inode || ip->inum == 9998) {
    return procfs_read_meminfo(user_dst, dst, off, n);
  }
  return -1;
}