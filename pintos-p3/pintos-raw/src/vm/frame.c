#include "vm/frame.h"
#include <stdio.h>
#include "vm/page.h"
#include "devices/timer.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/*
Managing the frame table

The main job is to obtain a free frame to map a page to. To do so:

1. Easy situation is there is a free frame in frame table and it can be
obtained. If there is no free frame, you need to choose a frame to evict
using your page replacement algorithm based on setting accessed and dirty
bits for each page. See section 4.1.5.1 and A.7.3 to know details of
replacement algorithm(accessed and dirty bits) If no frame can be evicted
without allocating a swap slot and swap is full, you should panic the
kernel.

2. remove references from any page table that refers to.

3.write the page to file system or swap.

*/

// we just provide frame_init() for swap.c
// the rest is your responsibility

static struct frame *frames;
static size_t frame_cnt;

static struct lock scan_lock;
static size_t hand;

void
frame_init (void)
{
  void *base;

  lock_init (&scan_lock);

  frames = malloc (sizeof *frames * init_ram_pages);
  if (frames == NULL)
    PANIC ("out of memory allocating page frames");

  while ((base = palloc_get_page (PAL_USER)) != NULL)
    {
      struct frame *f = &frames[frame_cnt++];
      lock_init (&f->lock);
      f->base = base;
      f->page = NULL;
    }
}

/* Tries to allocate and lock a frame for PAGE.
   Returns the frame if successful, false on failure. */
struct frame *try_frame_alloc_and_lock (struct page *page) {
   // printf("frame alloc and lock\n");
   lock_acquire(&scan_lock);

   //check for unlocked frame
   for (size_t i = 0; i < frame_cnt; i++){
      struct frame *f = &frames[i];
      if (!lock_held_by_current_thread(&f->lock) && !lock_try_acquire(&f->lock)){
         continue;
      }
      if (f->page == NULL){
         f->page = page;
         lock_release(&scan_lock);
         return f;
      }
      lock_release(&f->lock);
   }

   while (true)
   {
      // printf("looking for frame to evict\n");
      hand += 1;
      hand %= frame_cnt;
      struct frame *f = &frames[hand];
      if (!lock_try_acquire(&f->lock))
      {
         continue;
      }


      if (f->page != NULL)
      {
         if (!page_out(f->page)){
            lock_release(&f->lock);
            continue;
            //lock_release(&scan_lock);
            //return NULL;
         } 
      }

      f->page = page;
      lock_release(&scan_lock);
      return f;
   }
   

   //no frames r free :(
   /*for (size_t i = 0 ; i < frame_cnt * 2 ; i++){
      struct frame *f = &frames[hand];
      hand += 1;
      if (hand >= frame_cnt){
         hand = 0;
      }
      if (!lock_try_acquire(&f->lock)){
         continue;
      }
      //MAKE SURE TO:
      //page is not null, frame is locked
      if (f->page == NULL){
         f->page = page;
         lock_release (&scan_lock);
         return f;
      }
      if (page_accessed_recently(f->page)){
         lock_release(&f->lock);
         continue;
      }
      lock_release (&scan_lock);

      //evict me!
      if (f->page != NULL){
         if (!page_out(f->page)){
            lock_release(&f->lock);
            return NULL;
         }
      }
      f->page = page;
      return f;

   }*/

   lock_release(&scan_lock);
   return NULL;
}

/* Tries really hard to allocate and lock a frame for PAGE.
   Returns the frame if successful, false on failure. */
struct frame *frame_alloc_and_lock (struct page *page) {

   size_t do_it_again;

   for (do_it_again = 0 ; do_it_again < 3 ; do_it_again++){
      struct frame *f = try_frame_alloc_and_lock (page);
      if (f != NULL){
         return f;
      }
      timer_msleep(100);
   }
   return NULL;
}

/* Locks P's frame into memory, if it has one.
   Upon return, p->frame will not change until P is unlocked. */
void frame_lock (struct page *p) {
   if (p->frame != NULL){
      lock_acquire(&p->frame->lock);
   }
}
/* Releases frame F for use by another page.
   F must be locked for use by the current process.
   Any data in F is lost. */
void frame_free (struct frame *f) {

   ASSERT (lock_held_by_current_thread(&f->lock));

   f->page = NULL;
   lock_release(&f->lock);
   
}

/* Unlocks frame F, allowing it to be evicted.
   F must be locked for use by the current process. */
void frame_unlock (struct frame *f) {
   ASSERT(lock_held_by_current_thread(&f->lock));
   lock_release(&f->lock); 
}


