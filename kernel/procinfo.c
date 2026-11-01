#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern struct spinlock wait_lock;
extern struct proc proc[NPROC];

int
procinfo_get(int pid, struct procinfo *out)
{
  struct proc *p;
  int found = 0;

  if (out == 0)
    return -1;

  acquire(&wait_lock);
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED && p->pid == pid) {
      out->pid = p->pid;
      out->ppid = p->parent ? p->parent->pid : 0;
      out->state = p->state;
      out->sz = p->sz;
      out->killed = p->killed;
      safestrcpy(out->name, p->name, sizeof(out->name));
      found = 1;
    }
    release(&p->lock);
    if (found)
      break;
  }
  release(&wait_lock);

  return found ? 0 : -1;
}

int
procinfo_pids(int *pids, int max)
{
  struct proc *p;
  int n = 0;

  if (pids == 0 || max <= 0)
    return 0;

  for (p = proc; p < &proc[NPROC] && n < max; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED)
      pids[n++] = p->pid;
    release(&p->lock);
  }
  return n;
}

const char*
procstate_name(enum procstate state)
{
  switch (state) {
  case UNUSED:   return "unused";
  case USED:     return "used";
  case SLEEPING: return "sleeping";
  case RUNNABLE: return "runnable";
  case RUNNING:  return "running";
  case ZOMBIE:   return "zombie";
  default:       return "???";
  }
}
