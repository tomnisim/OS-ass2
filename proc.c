#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define NULL ((void *)0)




extern uint64 cas (volatile void *addr, int expected, int newval);

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;
int index_counter = 0;

struct proc unused_head;
struct proc sleeping_head;
struct proc zombie_head;


extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.



void 
add_to_list(struct proc *head, struct proc *p)
{
  if (head == NULL)
  {
    panic("The list is empty");
  }
  // printf("%s%d\n","=============================start add to list================================== ", get_cpu());
  // printf("%p\n",cpus[get_cpu()].head_runnable);
  struct proc *node = head;
  
  acquire(&node->l);
  struct proc *first_node = node->next;

 

  while(node->next != NULL)
  {
    release(&node->l);
    node = first_node;
    acquire(&node->l);
    first_node = node -> next;

  }
  node->next = p;
  release(&node->l);
  acquire(&p->l);
  p->next = NULL;
  release(&p->l);


  
  // printf("%p\n",cpus[get_cpu()].head_runnable);
  // printf("%s%d\n","=============================finish add to list================================== ", get_cpu());

  //release(&cpus[cpu_id].head_runnable ->lock);
}


void remove_from_list(struct proc *head, struct proc *p)
{
  // printf("%s%d\n", "=============================start remove from list================================== ", get_cpu());
  if (head-> next == NULL)
  {
    return;
  }
  acquire(&p->l);
  int pid = p->pid;
  release(&p->l);





  struct proc *first_node = head;
  acquire(&first_node->l);
  struct proc *second_node = first_node->next;
  acquire(&second_node->l);
  while(second_node!=NULL && second_node->pid != pid)
  {
    release(&first_node->l);
    first_node = second_node;
    second_node = second_node->next;
    acquire(&second_node->l);
  }
  if (second_node != NULL)
  {
    first_node->next = second_node->next;
  }
  release(&first_node->l);
  release(&second_node->l);

}


void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(struct cpu *cp = cpus ;cp < &cpus[NCPU] ;cp++)
  {
    cp->head_runnable.next = 0;
    initlock(&cp->head_runnable.l, "proc");
  }
  
  initlock(&unused_head.l, "unused");
  acquire(&unused_head.lock);
  unused_head.next = NULL;
  release(&unused_head.lock);

  initlock(&sleeping_head.l, "sleeping");
  acquire(&sleeping_head.lock);
  sleeping_head.next = NULL;
  release(&sleeping_head.lock);

  initlock(&zombie_head.l, "zombie");
  acquire(&zombie_head.lock);
  zombie_head.next = NULL;
  release(&zombie_head.lock);

  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      initlock(&p->l, "lock");
      p->kstack = KSTACK((int) (p - proc));

      // printf("%s%s\n","PROCINIT=============================before add to list===================================== ", "unused");
      if (p->state == UNUSED)
      {
          add_to_list(&unused_head, p);
      }
      
      // printf("%s%s\n","PROCINIT=============================after add to list===================================== ", "unused");
      //print_global_list(&unused_head);
  }


}





// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}



int
allocpid() {
  int pid = 0;
  do{ 
    pid = nextpid;
  } while (cas(&nextpid ,pid , pid + 1));

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  // printf("start allocproc\n");

  struct proc *p ;
  p = unused_head.next;
  if (p != NULL)
  {
    acquire(&p->lock);
    goto found;
  }
  else{
    return 0;
  }
    

found:
  // printf("allocproc in found0\n");
  p->pid = allocpid();
  p->state = USED;
  // printf("%s%s\n","allocproc=============================before remove from list===================================== ", "unused");
  remove_from_list(&unused_head, p);
  // printf("%s%s\n","ALLOCPROC=============================after remove from list===================================== ", "unused");

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }


  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  return p;
}



// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;


  // printf("%s%s\n","FREEPROC=============================before remove from list===================================== ", "zombie");
  remove_from_list(&zombie_head, p);
  // printf("%s%s\n","FREEPROC=============================after remove from list===================================== ", "zombie");
  
  

  // printf("%s%s\n","FREEPROC=============================before add to list===================================== ", "unused");
  add_to_list(&unused_head, p);
  // printf("%s%s\n","FREEPROC=============================after add to list===================================== ", "unused");
  //print_global_list(&unused_head);
}


// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  // int cpu_id = get_cpu();
  // printf("%s%d\n", "cpu id = ", cpu_id);
  if (p->pid==1)
  {
    #if ON
    // uint64 count = cpus[0].counter;
    while(cas(&cpus[0].counter,cpus[0].counter,cpus[0].counter + 1));
    #endif

    add_to_list(&cpus[0].head_runnable, p);
      //print_global_list(&cpus[0].head_runnable);
  }
  // printf("shahar is the queen!\n");
  release(&p->lock);
  // printf("fsfsfsfsfsfsfdfdfjdfvjdfvdjfvkfdsvnskjvnkjvndkj\n");
  
}


// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&p->lock);
  np->state = RUNNABLE;
  #if OFF
  // printf("in off\n");
  int cpu_id = get_cpu();
  struct cpu * cp = &cpus[cpu_id];
  #elif ON
  int cpu_id = get_min_cpu();
  // printf("the current cpu is: %d, and the min cpu is: %d\n", get_cpu(), cpu_id);
  struct cpu *cp = &cpus[cpu_id];
  // uint64 count = cp->counter;
  while(cas(&cp->counter,cp->counter, cp->counter + 1));
  #else 
  struct cpu *cp  = mycpu();
  int cpu_id = -1;
  panic("bncflg err\n");
  #endif
  release(&p->lock);
  acquire(&np->lock);
  add_to_list(&cp->head_runnable, np);
  //print_global_list(&cpus[cpu_id].head_runnable);


  release(&np->lock);

  return pid;
}



// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  
  p->state = ZOMBIE;
  

  // printf("%s%s\n","EXIT=============================before add to list===================================== ", "zombiee");
  add_to_list(&zombie_head, p);
  // printf("%s%s\n","EXIT=============================after add to list===================================== ", "zombiee");
  //print_global_list(&zombie_head);

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}



// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  // struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    printf("");
    
    
      //int cpu_id = get_cpu();
      struct proc *node = &c->head_runnable;
      acquire(&node->lock);
      struct proc *first_node = node->next;
      release(&node->lock);
      node = first_node;


      
      if(node != NULL){
        
        acquire(&node->lock);
        remove_from_list(&c->head_runnable,node);
        #if ON
        // uint64 count = c->counter;
        while(cas(&c->counter,c->counter,c->counter -1));
        #endif
        // printf("%s%d\n","pid =   ", node->pid);
        node->state = RUNNING;
        c->proc = node;

        //node->last_cpu = get_cpu();
        swtch(&c->context, &node->context);
        c->proc = 0;

        if (node!=NULL)
        {
          release(&node->lock);

        }
        
    }



    
    // else
    // {
    //   struct proc *p;
    //   for(p = proc; p < &proc[NPROC]; p++) {
    //     acquire(&p->lock);
    //     if(p->state == RUNNABLE) {
    //       // Switch to chosen process.  It is the process's job
    //       // to release its lock and then reacquire it
    //       // before jumping back to us.
    //       p->state = RUNNING;
    //       c->proc = p;
    //       swtch(&c->context, &p->context);

    //       // Process is done running for now.
    //       // It should have changed its p->state before coming back.
    //       c->proc = 0;
    //     }
    //     release(&p->lock);
    //   }
    // }


    // else{
    //   // printf("%s%d\n","there isnt anty proc to run on this cpu with the id: ", cpu_id);
    //   return;
    // }

    // for(p = proc; p < &proc[NPROC]; p++) {
    //   acquire(&p->lock);
    //   if(p->state == RUNNABLE) {
    //     // Switch to chosen process.  It is the process's job
    //     // to release its lock and then reacquire it
    //     // before jumping back to us.
    //     p->state = RUNNING;
    //     c->proc = p;
    //     swtch(&c->context, &p->context);

    //     // Process is done running for now.
    //     // It should have changed its p->state before coming back.
    //     c->proc = 0;
    //   }
    //   release(&p->lock);
    // }
  }
  
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  int cpu_id = get_cpu();
  #if ON
  // uint64 count = cpus[cpu_id].counter;
  // while(cas(&cpus[cpu_id].counter,cpus[cpu_id].counter,cpus[cpu_id].counter+1)); //not need see 4.2.1
  #endif
  add_to_list(&cpus[cpu_id].head_runnable, p);
  //print_global_list(&cpus[cpu_id].head_runnable);
  sched();
  release(&p->lock);
}



// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;

  // printf("%s%s\n","SLEEP============================before add to list===================================== ", "sleeping");
  add_to_list(&sleeping_head, p);
  // printf("%s%s\n","SLEEP=============================after add to list===================================== ", "sleeping");
  //print_global_list(&sleeping_head);
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}


// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *node = sleeping_head.next;
  while (node!=0)
  {
    if(node->chan == chan)
    {
      // printf("%s%s\n","WAKEUP=============================before remove from list=====================================", "sleeping");
      remove_from_list(&sleeping_head, node);
      // printf("%s%s\n","WAKEUP=============================after remove from list=====================================", "sleeping");
      #if OFF
      // printf("in off\n");
      int cpu_id = get_cpu();
      #elif ON
      int cpu_id = get_min_cpu();
      // int count = cpus[cpu_id].counter;
      while(cas(&cpus[cpu_id].counter,cpus[cpu_id].counter, cpus[cpu_id].counter+1)){}
      
      // printf("the current cpu is: %d, and the min cpu is: %d\n", get_cpu(), cpu_id);
      #else 
      int cpu_id = -1;
      panic("bncflg err\n");
      #endif
      add_to_list(&cpus[cpu_id].head_runnable, node);

      //print_global_list(&cpus[cpu_id].head_runnable);

    }
    node = node->next;
  }

  
  
  // else{
  //   struct proc *p;

  // for(p = proc; p < &proc[NPROC]; p++) {
  //   if(p != myproc()){
  //     acquire(&p->lock);
  //     if(p->state == SLEEPING && p->chan == chan) {
  //       p->state = RUNNABLE;
  //     }
  //     release(&p->lock);
  //   }
  // }
  // }
  
}


// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;
   printf("start kill\n");
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        remove_from_list(&sleeping_head, p);
        int id = p->last_cpu;
        struct cpu* c =& cpus[id];
        #if ON
        // uint64 count = cpus[id].counter;
        while(cas(&cpus[id].counter,cpus[id].counter,cpus[id].counter + 1));
        #endif
        add_to_list(&c->head_runnable, p);
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  printf("end kill\n");
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

//extern uint64 cas (volatile void *addr, int expected, int newval);

int 
get_cpu()
{
  push_off();
  int id = -1;
  id = cpuid();
  pop_off();
  return id;
}


int 
set_cpu(int cpu_num){
  struct proc *p =myproc();
    p->last_cpu = cpu_num;
    yield();
    return cpu_num;
}
int
cpu_process_count(int cpu_num){
  int ans = -1;
  struct cpu cp = cpus[cpu_num];
  ans = cp.counter;
  return ans;
  }

int 
get_min_cpu(){
  
  int cpu_id = 0;
  int min = cpus[0].counter;
  for(int i = 1 ; i < NCPU ; i++){
      if( cpus[i].counter < min){
        cpu_id = i;
        min = cpus[i].counter;
      }
  }

  return cpu_id;
}



  
// }
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}