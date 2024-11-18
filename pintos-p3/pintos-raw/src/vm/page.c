#include "vm/page.h"
#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <string.h>
#include "threads/palloc.h"


/* Destroys a page, which must be in the current process's
   page table.  Used as a callback for hash_destroy(). */
static void destroy_page (struct hash_elem *p_, void *aux UNUSED)  {

   struct page *p = hash_entry(p_, struct page, hash_elem);
   frame_lock(p);
   if (p->frame){
      frame_free(p->frame);
   }
   free(p);
}


/* Destroys the current process's page table. */
void page_exit (void)  {

   struct hash *h = &thread_current ()->spt;
   if (h != NULL){
      hash_destroy(h, destroy_page);
   }

}

/* Returns the page containing the given virtual ADDRESS,
   or a null pointer if no such page exists.
   Allocates stack pages as necessary. */
struct page *page_for_addr (const void *address) {

   if (address < PHYS_BASE){
      struct page page;
      struct hash_elem *e;

      page.addr = (void *) pg_round_down(address);
      e = hash_find(&thread_current ()->spt, &page.hash_elem);
      if (e != NULL){
         return hash_entry(e, struct page, hash_elem);
      }

      //stack growth here ?????
   }
   return NULL;

}


/* Locks a frame for page P and pages it in.
   Returns true if successful, false on failure. */
static bool do_page_in (struct page *p) {

   p->frame = try_frame_alloc_and_lock(p);
   if (p->frame == NULL){
      return false;
   }
   if (p->file != NULL){
      off_t read = file_read_at(p->file, p->frame->base, p->f_bytes, p->file_offset);
      off_t zero = PGSIZE - read;
      memset(p->frame->base + read, 0, zero);
      if (read != (off_t) p->f_bytes){
         p->frame->page = NULL;
         frame_unlock(p->frame);
         return false;
      }
   }
   else {
      memset(p->frame->base, 0, PGSIZE);
   }
   return true;
}


/* Faults in the page containing FAULT_ADDR.
   Returns true if successful, false on failure. */
bool page_in (void *fault_addr) {

   bool success;

   struct page *page = page_for_addr(fault_addr);
   if (page == NULL){
      return false;
   }

   frame_lock(page);
   if (page->frame == NULL){
      if (!do_page_in (page)){
         frame_unlock(page);
         return false;
      }
   }
   //assert lock??
   success = pagedir_set_page(thread_current ()->pagedir, page->addr, page->frame->base, !page->read_only);

   frame_unlock(page->frame);
   return success;
   // struct page *p = page_for_addr(fault_addr);
   // if (p == NULL){
   //    return false;
   // }

   // struct frame *frame = try_frame_alloc_and_lock(p);
   // if (frame == NULL){
   //    return false;
   // }

   // if (p->file != NULL){
   //    off_t read = file_read_at(p->file, frame->base, p->f_bytes, p->file_offset);
   //    if (read != (off_t) p->f_bytes){
   //       frame->page = NULL;
   //       frame_unlock(frame);
   //       return false;
   //    }
   //    memset(frame->base + read, 0, p->leftover_bytes);
   // } else {
   //    memset(frame->base, 0, PGSIZE);
   // }

   // p->frame = frame;
   // frame_unlock(frame);
   // return true;

}


/* Evicts page P.
   P must have a locked frame.
   Return true if successful, false on failure. */
bool page_out (struct page *p) {
   return false;
}


/* Returns true if page P's data has been accessed recently,
   false otherwise.
   P must have a frame locked into memory. */
bool page_accessed_recently (struct page *p) {

   bool accessed_recently_queen;

   ASSERT(p->frame != NULL);
   //assert lock too?

   accessed_recently_queen = pagedir_is_accessed(p->thread->pagedir, p->addr);
   if (accessed_recently_queen){
      pagedir_set_accessed(p->thread->pagedir, p->addr, false);
   }
   return accessed_recently_queen;

}


/* Adds a mapping for user virtual address VADDR to the page hash
   table. Fails if VADDR is already mapped or if memory
   allocation fails. */
struct page * page_allocate (void *vaddr, bool read_only) {

   struct page *p = palloc_get_page(0);
   struct thread *curr_thread = thread_current ();
   if (p != NULL){
      p->addr = pg_round_down(vaddr);
      p->read_only = read_only;
      p->thread = curr_thread;
      p->frame = NULL;
      p->file = NULL;
      p->file_offset = 0;
      p->f_bytes = 0;

      if (hash_insert(&thread_current ()->spt, &p->hash_elem) != NULL){
         free(p);
         p = NULL;
      }
   }
   return p;

}


/* Evicts the page containing address VADDR
   and removes it from the page table. */
void page_deallocate (void *vaddr) {

   struct page *p = page_for_addr(vaddr);
   ASSERT(p != NULL);

   if (p->frame != NULL){
      lock_acquire(&p->frame->lock);
      p->frame->page = NULL;
      lock_release(&p->frame->lock);
      p->frame = NULL;
   }

   hash_delete(&p->thread->spt, &p->hash_elem);
   free(p);
   
}


/* Returns a hash value for the page that E refers to. */
unsigned page_hash (const struct hash_elem *e, void *aux UNUSED) {

   const struct page *p = hash_entry(e, struct page, hash_elem);
   return hash_bytes(&p->addr, sizeof p->addr);

}



/* Returns true if page A precedes page B. */
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {

   const struct page *a = hash_entry(a_, struct page, hash_elem);
   const struct page *b = hash_entry(b_, struct page, hash_elem);

   return a->addr < b->addr;

}


/* Tries to lock the page containing ADDR into physical memory.
   If WILL_WRITE is true, the page must be writeable;
   otherwise it may be read-only.
   Returns true if successful, false on failure. */
bool page_lock (const void *addr, bool will_write) {

   struct page *p = page_for_addr(addr);
   if (p == NULL){
      return false;
   }
   if (p->read_only && will_write){
      return false;
   }

   frame_lock(p);
   if (p->frame == NULL){
      bool result = do_page_in(p);
      bool set_result = pagedir_set_page(thread_current ()->pagedir, p->addr, p->frame->base, !p->read_only);
      return result && set_result;
   }
   return true;

}

/* Unlocks a page locked with page_lock(). */
void page_unlock (const void *addr) {

   struct page *page = page_for_addr(addr);
   if (page != NULL){
      frame_unlock(page->frame);
   }
}


