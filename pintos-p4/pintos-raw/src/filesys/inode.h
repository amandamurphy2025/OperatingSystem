#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H
#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
struct bitmap;
/* TA note: Most of the modifications for large files
will be around inode.h and inode.c */
/* Type of an inode. */
enum inode_type
{
FILE_INODE, /* Ordinary file. */
DIR_INODE /* Directory. */
};
void inode_init (void);
struct inode *inode_create (block_sector_t, enum inode_type);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
enum inode_type inode_get_type (const struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
int inode_open_cnt (const struct inode *);
void inode_lock (struct inode *);
void inode_unlock (struct inode *);
#endif /* filesys/inode.h */
