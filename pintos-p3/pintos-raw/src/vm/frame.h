#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "threads/synch.h"
#include <debug.h>
#include "threads/loader.h"
#include <stdbool.h>

/*
Just prototypes. See frame.c for more detail.
*/

struct frame {
    void *base; //this is the kernel virt address ("phyical") - Roy
    struct page *page; //keeping track of the page associated with this frame
    struct lock lock; //lock for the frame
};


void frame_init (void);
struct frame *try_frame_alloc_and_lock (struct page *page);
struct frame *frame_alloc_and_lock (struct page *page);
void frame_lock (struct page *p);
void frame_free (struct frame *f);
void frame_unlock (struct frame *f);

#endif
