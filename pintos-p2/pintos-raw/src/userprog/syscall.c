#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"


syscall_function table[] = {

  get_args_sys_halt,
  get_args_sys_exit,
  get_args_sys_exec,
  get_args_sys_wait,
  get_args_sys_create,
  get_args_sys_remove,
  get_args_sys_open,
  get_args_sys_filesize,
  get_args_sys_read,
  get_args_sys_write,
  get_args_sys_seek,
  get_args_sys_tell,
  get_args_sys_close

}

//functions to get the args for the handlers
void get_args_sys_halt(struct intr_frame *, int *args){
  sys_halt();
}

void get_args_sys_exit(struct intr_frame *, int *args){
  sys_exit(args[0]);
}

void get_args_sys_exec(struct intr_frame *, int *args){
  f->eax = sys_exec((const char *)args[0]);
}

void get_args_sys_wait(struct intr_frame *, int *args){
  f->eax = sys_wait((pid_t)args[0]);
}

void get_args_sys_create(struct intr_frame *, int *args){
  f->eax = sys_create((const char *)args[0], (unsigned)args[1]);
}

void get_args_sys_remove(struct intr_frame *, int *args){
  f->eax = sys_remove((const char *)args[0]);
}

void get_args_sys_open(struct intr_frame *, int *args){
  f->eax = sys_open((const char *)args[0]);
}

void get_args_sys_filesize(struct intr_frame *, int *args){
  f->eax = sys_filesize(args[0]);
}

void get_args_sys_read(struct intr_frame *, int *args){
  f->eax = sys_read(args[0], (void *)args[1], (unsigned)args[2]);
}

void get_args_sys_write(struct intr_frame *, int *args){
  f->eax = sys_write(args[0], (const void *)args[1], (unsigned)args[2]);
}

void get_args_sys_seek(struct intr_frame *, int *args){
  sys_seek(args[0], (unsigned)args[1]);
}

void get_args_sys_tell(struct intr_frame *, int *args){
  f->eax = sys_tell(args[0]);
}

void get_args_sys_close(struct intr_frame *, int *args){
  sys_close(args[0]);
}

//this feels stupid but number of args per handler
const int arg_counts[] = {
  0,
  1,
  1,
  1,
  2,
  1,
  1,
  1,
  3,
  3,
  2,
  1,
  1
}

static void syscall_handler (struct intr_frame *);
static int sys_exec (const char *cmd_line);
void sys_halt (void);
void sys_exit (int status);
int sys_wait (pid_t pid);
bool sys_create (const char *file, unsigned initial_size);
bool sys_remove (const char *file);
int sys_open (const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, const void *buffer, unsigned size);
void sys_seek (int fd, unsigned position);
unsigned sys_tell (int fd);
void sys_close (int fd);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void sys_exit (int status){

  struct thread *curr = thread_current ();

  //thread_name() defined in thread.h, termination message defined in pintos_3.html
  printf("%s: exit(%d)\n", curr->name, status);

  // if (curr->parent_is_waiting){
  //   if (curr->parent != NULL && curr->parent_is_waiting){

  //   }
  // }

  thread_exit();

}

int sys_write (int fd, const void *buffer, unsigned size){
  
  if (buffer == NULL || !is_user_vaddr(buffer)){
    sys_exit(-1)
  }

  if (fd == 1){
    //find putbuf in console.c
    putbuf(buffer, size);
    return size;
  }

  struct file *file = get_file(fd);


}










static void
syscall_handler (struct intr_frame *f)
{

  unsigned call_nr;
  int args[3]; // It's 3 because that's the max number of arguments in all syscalls.
  copy_in (&call_nr, f->esp, sizeof call_nr); 

  // copy the args (depends on arg_cnt for every syscall).
  // note that if the arg passed is a pointer (e.g. a string),
  // then we just copy the pointer here, and you still need to
  // call 'copy_in_string' on the pointer to pass the string
  // from user space to kernel space

  //check if call number is valid
  if (call_nr >= sizeof(table) / sizeof(table[0])) {
      sys_exit(-1);
  }

  //grab arg number for that syscall function
  int arg_cnt = arg_counts[call_nr];

  memset (args, 0, sizeof args);
  copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * arg_cnt);

  table[call_nr](f, args);
}

static void 
copy_in (void *dst_, const void *usrc_, size_t size) {

  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;

  for (; size > 0; size--, dst++, usrc++)
    if (usrc >= (uint8_t *) PHYS_BASE || !get_user (dst, usrc))
      thread_exit ();

}


/* Copies a byte from user address USRC to kernel address DST. USRC must
be below PHYS_BASE. Returns true if successful, false if a segfault
occurred. Unlike the one posted on the p2 website, this one takes two
arguments: dst, and usrc */
static inline bool
get_user (uint8_t *dst, const uint8_t *usrc)
{
int eax;
asm ("movl $1f, %%eax; movb %2, %%al; movb %%al, %0; 1:"
: "=m" (*dst), "=&a" (eax) : "m" (*usrc));
return eax != 0;
}



/* Writes BYTE to user address UDST. UDST must be below PHYS_BASE.
Returns true if successful, false if a segfault occurred. */
static inline bool
put_user (uint8_t *udst, uint8_t byte)
{
int eax;
asm ("movl $1f, %%eax; movb %b2, %0; 1:"
: "=m" (*udst), "=&a" (eax) : "q" (byte));
return eax != 0;
}


/* Creates a copy of user string US in kernel memory and returns it as a
page that must be **freed with palloc_free_page()**. Truncates the string
at PGSIZE bytes in size. Call thread_exit() if any of the user accesses
are invalid. */
static char *
copy_in_string (const char *us)
{
  char *ks;
  size_t length;
  ks = palloc_get_page (0);
  if (ks == NULL)
    thread_exit ();
  for (...)
  {
    ...;
    // call get_user() until you see '\0'
    ...;
  }
  return ks;
  // don't forget to call palloc_free_page(..) when you're done
  // with this page, before you return to user from syscall
}


