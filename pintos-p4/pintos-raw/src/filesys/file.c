#include "filesys/file.h"
#include <debug.h>
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include <stdbool.h>

/* An open file. */
struct file 
  {
    struct inode *inode;        /* File's inode. */
    off_t pos;                  /* Current position. */
    bool deny_dummy;            /* deny_write has "random" values without this */
    bool deny_write;            /* Has file_deny_write() been called? */   
  };

/* Creates a file in the given SECTOR,
initially LENGTH bytes long.
Returns inode for the file on success, null pointer on failure.
On failure, SECTOR is released in the free map. */
struct inode *
file_create (block_sector_t sector, off_t length)
{
  //printf("inode create! %d\n", sector);
  //printf("file create\n");
  /* setup file */
  struct inode *inode = inode_create (sector, FILE_INODE);
  if (inode == NULL)
  {
    //printf("inode create failed");
    return NULL;
  }

  if (length > 0)
  {
    // char *zeros = calloc(length, sizeof(char));
    
    int bytes_written = inode_write_at (inode, "", 1, length - 1);
    if (bytes_written != 1) {
      inode_remove(inode);
      inode_close(inode);
      free_map_release(sector);
      return NULL;
    }

    // ASSERT(bytes_written == length);
  }
  
  return inode;
}

/* Opens a file for the given INODE, of which it takes ownership,
   and returns the new file.  Returns a null pointer if an
   allocation fails or if INODE is null. */
struct file *
file_open (struct inode *inode) 
{
  struct file *file = calloc (1, sizeof *file);
  if (inode != NULL && file != NULL && inode_get_type (inode) == FILE_INODE)
    {
      inode_reopen(inode);
      file->inode = inode;
      file->pos = 0;
      file->deny_dummy = 0;
      file->deny_write = 0;
      return file;
    }
  else
    {
      inode_close (inode);
      free (file);
      return NULL; 
    }
}

/* Opens and returns a new file for the same inode as FILE.
   Returns a null pointer if unsuccessful. */
struct file *
file_reopen (struct file *file) 
{
  return file_open (inode_reopen (file->inode));
}

/* Closes FILE. */
void
file_close (struct file *file) 
{
  if (file != NULL)
    {
      // printf("file->deny_write %d\n", file->deny_write);
      // printf("file->deny_dummy, %d\n", file->deny_dummy);
      file_allow_write (file);
      inode_close (file->inode);
      free (file); 
    }
}

/* Returns the inode encapsulated by FILE. */
struct inode *
file_get_inode (struct file *file) 
{
  return file->inode;
}

/* Reads SIZE bytes from FILE into BUFFER,
   starting at the file's current position.
   Returns the number of bytes actually read,
   which may be less than SIZE if end of file is reached.
   Advances FILE's position by the number of bytes read. */
off_t
file_read (struct file *file, void *buffer, off_t size) 
{
  // printf("FILE_READ size %zu\n", size);
  off_t bytes_read = inode_read_at (file->inode, buffer, size, file->pos);
  file->pos += bytes_read;
  return bytes_read;
}

/* Reads SIZE bytes from FILE into BUFFER,
   starting at offset FILE_OFS in the file.
   Returns the number of bytes actually read,
   which may be less than SIZE if end of file is reached.
   The file's current position is unaffected. */
off_t
file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) 
{
  return inode_read_at (file->inode, buffer, size, file_ofs);
}

/* Writes SIZE bytes from BUFFER into FILE,
   starting at the file's current position.
   Returns the number of bytes actually written,
   which may be less than SIZE if end of file is reached.
   (Normally we'd grow the file in that case, but file growth is
   not yet implemented.)
   Advances FILE's position by the number of bytes read. */
off_t
file_write (struct file *file, const void *buffer, off_t size) 
{
  off_t bytes_written = inode_write_at (file->inode, buffer, size, file->pos);
  file->pos += bytes_written;
  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into FILE,
   starting at offset FILE_OFS in the file.
   Returns the number of bytes actually written,
   which may be less than SIZE if end of file is reached.
   (Normally we'd grow the file in that case, but file growth is
   not yet implemented.)
   The file's current position is unaffected. */
off_t
file_write_at (struct file *file, const void *buffer, off_t size,
               off_t file_ofs) 
{
  //printf("file write at\n");
  return inode_write_at (file->inode, buffer, size, file_ofs);
}

/* Prevents write operations on FILE's underlying inode
   until file_allow_write() is called or FILE is closed. */
void
file_deny_write (struct file *file) 
{
  // printf("file deny write %x\n", file->inode);
  ASSERT (file != NULL);
  if (!file->deny_write) 
    {
      file->deny_write = 1;
      file->deny_dummy = 1;
      inode_deny_write (file->inode);
    }
}

/* Re-enables write operations on FILE's underlying inode.
   (Writes might still be denied by some other file that has the
   same inode open.) */
void
file_allow_write (struct file *file) 
{
  // printf("file allow write %x\n", file->inode);
  // printf("file deny_write var %d\n", file->deny_write);
  // printf("file deny_dummy, %d\n", file->deny_dummy);
  ASSERT (file != NULL);
  if (file->deny_write) 
    {
      // printf("what\n");
      file->deny_write = 0;
      file->deny_dummy = 0;
      inode_allow_write (file->inode);
    }
}

/* Returns the size of FILE in bytes. */
off_t
file_length (struct file *file) 
{
  ASSERT (file != NULL);
  //printf("file length inode %d\n", file->inode);
  return inode_length (file->inode);
}

/* Sets the current position in FILE to NEW_POS bytes from the
   start of the file. */
void
file_seek (struct file *file, off_t new_pos)
{
  ASSERT (file != NULL);
  ASSERT (new_pos >= 0);
  file->pos = new_pos;
}

/* Returns the current position in FILE as a byte offset from the
   start of the file. */
off_t
file_tell (struct file *file) 
{
  ASSERT (file != NULL);
  return file->pos;
}
