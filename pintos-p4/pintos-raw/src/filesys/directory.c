#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/free-map.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir
{
  struct inode *inode; /* Backing store. */
  off_t pos; /* Current position. */
};

/* A single directory entry. */
struct dir_entry
{
  block_sector_t inode_sector; /* Sector number of header. */
  char name[NAME_MAX + 1]; /* Null terminated file name. */
  bool in_use; /* In use or free? */
};

/* Creates a directory in the given SECTOR.
The directory's parent is in PARENT_SECTOR.
Returns inode of created directory if successful,
null pointer on faiilure.
On failure, SECTOR is released in the free map. */
struct inode *
dir_create (block_sector_t sector, block_sector_t parent_sector)
{
  // NOTE: call inode_create and write special directories
  // entries as directory "data". remember lec12-io3-fsmeta :)
  // ...
  ASSERT(sector != 0); // ensure valid sector
  ASSERT(parent_sector != 0);

  struct inode *inode = inode_create(sector, DIR_INODE);
  if (inode == NULL)
  {
    free_map_release(sector);
    return NULL;
  }

  struct dir *dir = dir_open(inode);
  if (dir == NULL)
  {
    free_map_release(sector);
    return NULL;
  }

  // add self-reference and parent reference
  if (!dir_add(dir, ".", sector) || !dir_add(dir, "..", parent_sector))
  {
    dir_close(dir);
    free_map_release(sector);
    return NULL;
  }

  dir_close(dir);
  return inode;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL && inode_get_type (inode) == DIR_INODE)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  // printf("lookup\n");
  // printf("cwd %x\n", thread_current()->cwd);
  // for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
  //      ofs += sizeof e) 
  // {
  //   printf("name: %s\n", e.name);
  // }

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
  {
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  }
  return false;
}


/* Searches DIR for a file with the given NAME
and returns true if one exists, false otherwise.
On success, sets *INODE to an inode for the file, otherwise to
a null pointer. The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
struct inode **inode)
{
  //printf("dir lookup\n");
  struct dir_entry e;
  bool ok;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  inode_lock (dir->inode);
  ok = lookup (dir, name, &e, NULL);
  inode_unlock (dir->inode);

  // printf("ok? %d\n", ok);

  *inode = ok ? inode_open (e.inode_sector) : NULL;
  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
file by that name. The file's inode is in sector
INODE_SECTOR.
Returns true if successful, false on failure.
Fails if NAME is invalid (i.e. too long) or a disk or memory
error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
struct dir_entry e;
off_t ofs;
bool success = false;
ASSERT (dir != NULL);
ASSERT (name != NULL);
/* Check NAME for validity. */
if (*name == '\0' || strchr (name, '/') || strlen (name) > NAME_MAX)
return false;
/* Check that NAME is not in use. */
inode_lock (dir->inode);
if (lookup (dir, name, NULL, NULL))
goto done;
/* Set OFS to offset of free slot.
If there are no free slots, then it will be set to the
current end-of-file.
inode_read_at() will only return a short read at end of file.
Otherwise, we'd need to verify that we didn't get a short
read due to something intermittent such as low memory. */
for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
ofs += sizeof e)
if (!e.in_use)
break;

/* Write slot. */
e.in_use = true;
strlcpy (e.name, name, sizeof e.name);
e.inode_sector = inode_sector;
success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
//printf("success %s %d\n", name, success);
done:
inode_unlock (dir->inode);
return success;
}

/* Removes any entry for NAME in DIR.
Returns true if successful, false on failure,
which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  //printf("dir remove\n");
struct dir_entry e;
struct inode *inode = NULL;
bool success = false;
off_t ofs;
ASSERT (dir != NULL);
ASSERT (name != NULL);
if (!strcmp (name, ".") || !strcmp (name, ".."))
return false;
/* Find directory entry. */
inode_lock (dir->inode);
if (!lookup (dir, name, &e, &ofs))
goto done;
/* Open inode. */
inode = inode_open (e.inode_sector);
if (inode == NULL)
goto done;
/* NOTE: Verify that it is not an in-use or non-empty directory. */
// ....
// ....
// ....
/* Erase directory entry. */
e.in_use = false;
if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
goto done;
/* Remove inode. */
inode_remove (inode);
success = true;
done:
inode_unlock (dir->inode);
inode_close (inode);
return success;
}

/* Reads the next directory entry in DIR and stores the name in
NAME. Returns true if successful, false if the directory
contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;
  inode_lock (dir->inode);
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
  {
    dir->pos += sizeof e;
    if (e.in_use /* && .....??? ..... */)
    {
      inode_unlock (dir->inode);
      strlcpy (name, e.name, NAME_MAX + 1);
      return true;
    }
  }
  inode_unlock (dir->inode);
  return false;
}
