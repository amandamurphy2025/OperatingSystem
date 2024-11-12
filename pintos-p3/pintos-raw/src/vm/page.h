#ifndef VM_PAGE_H
#define VM_PAGE_H
/*
Just prototypes, see page.c for more detail
*/


#define STACK_MAX (1024 * 1024)


static void destroy_page (struct hash_elem *p_, void *aux UNUSED);
void page_exit (void);
static struct page *page_for_addr (const void *address);
static bool do_page_in (struct page *p);
bool page_in (void *fault_addr);
bool page_out (struct page *p);
bool page_accessed_recently (struct page *p);
struct page * page_allocate (void *vaddr, bool read_only);
void page_deallocate (void *vaddr);
unsigned page_hash (const struct hash_elem *e, void *aux UNUSED);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
bool page_lock (const void *addr, bool will_write);
void page_unlock (const void *addr);

#endif
