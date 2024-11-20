#include "vm/swap.h"
/*

Managing the swap table

You should handle picking an unused swap slot for evicting a page from its
frame to the swap partition. And handle freeing a swap slot which its page
is read back.

You can use the BLOCK_SWAP block device for swapping, obtaining the struct
block that represents it by calling block_get_role(). Also to attach a swap
disk, please see the documentation.

and to attach a swap disk for a single run, use this option ‘--swap-size=n’

*/

// we just provide swap_init() for swap.c
// the rest is your responsibility

/* Set up*/
void
swap_init (void)
{
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    {
      printf ("no swap device--swap disabled\n");
      swap_bitmap = bitmap_create (0);
    }
  else
    swap_bitmap = bitmap_create (block_size (swap_device)
                                 / PAGE_SECTORS);
  if (swap_bitmap == NULL)
    PANIC ("couldn't create swap bitmap");
  lock_init (&swap_lock);
}

/* Swaps in page P, which must have a locked frame
   (and be swapped out). */
bool
swap_in (struct page *p)
{
  //printf("swap in\n ");
    // might want to use these functions:
    // - lock_held_by_current_thread()
  ASSERT (lock_held_by_current_thread (&p->frame->lock));
    // - block_read()
    // - bitmap_reset()

  for (size_t i = 0 ; i < PAGE_SECTORS ; i++){
    block_read (swap_device, p->swap_sect+i, p->frame->base + i * BLOCK_SECTOR_SIZE);
  }
  lock_acquire(&swap_lock);
  bitmap_reset(swap_bitmap, p->swap_sect / PAGE_SECTORS);
  lock_release(&swap_lock);
  p->swap_sect = (block_sector_t) - 1;
}

/* Swaps out page P, which must have a locked frame. */
bool 
swap_out (struct page *p) 
{
  //printf("swap out\n");
  // might want to use these functions:
  // - lock_held_by_current_thread()
  ASSERT (lock_held_by_current_thread (&p->frame->lock));
  // - bitmap_scan_and_flip()
  // - block_write()

  lock_acquire(&swap_lock);
  size_t swap_slot = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
  lock_release(&swap_lock);

  if (swap_slot == BITMAP_ERROR){
    return false;
  }
  //check for error?

  p->swap_sect = swap_slot * PAGE_SECTORS;

  for (size_t i = 0 ; i < PAGE_SECTORS ; i++){
    block_write(swap_device, p->swap_sect + i, p->frame->base + i * BLOCK_SECTOR_SIZE);
  }

  p->file = NULL;
  p->file_bytes = 0;
  p->file_offset = 0;

  return true;
    
  
}
