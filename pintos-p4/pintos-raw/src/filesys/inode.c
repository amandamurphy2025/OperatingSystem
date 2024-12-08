#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_CNT 123
#define INDIRECT_CNT 1
#define DBL_INDIRECT_CNT 1
#define SECTOR_CNT (DIRECT_CNT + INDIRECT_CNT + DBL_INDIRECT_CNT)
#define PTRS_PER_SECTOR ((off_t) (BLOCK_SECTOR_SIZE / sizeof (block_sector_t)))
#define INODE_SPAN ((DIRECT_CNT \
+ PTRS_PER_SECTOR * INDIRECT_CNT \
+ PTRS_PER_SECTOR * PTRS_PER_SECTOR * DBL_INDIRECT_CNT) \
* BLOCK_SECTOR_SIZE)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t sectors[SECTOR_CNT]; /* Sectors. */
    enum inode_type type; /* FILE_INODE or DIR_INODE. */
    off_t length; /* File size in bytes. */
    unsigned magic; /* Magic number. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
{
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location. */
  int open_cnt; /* Number of openers. */
  bool removed; /* True if deleted, false otherwise. */
  struct lock lock; /* Protects the inode. */

  /* Denying writes. */
  struct lock deny_write_lock; /* Protects members below. */
  struct condition no_writers_cond; /* Signaled when no writers. */
  int deny_write_cnt; /* 0: writes ok, >0: deny writes. */
  int writer_cnt; /* Number of writers. */
  unsigned magic;
};

// /* Returns the block device sector that contains byte offset POS
//    within INODE.
//    Returns -1 if INODE does not contain data for a byte at offset
//    POS. */
// static block_sector_t
// byte_to_sector (const struct inode *inode, off_t pos) 
// {
//   ASSERT (inode != NULL);
//   if (pos < inode->data.length)
//     return inode->data.start + pos / BLOCK_SECTOR_SIZE;
//   else
//     return -1;
// }

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Controls access to open_inodes list. */
static struct lock open_inodes_lock;

static void deallocate_inode (const struct inode *);
/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  lock_init (&open_inodes_lock);
}

/* Initializes an inode of the given TYPE, writes the new inode
to sector SECTOR on the file system device, and returns the
inode thus created. Returns a null pointer if unsuccessful,
in which case SECTOR is released in the free map. */
struct inode *
inode_create (block_sector_t sector, enum inode_type type)
{
// NOTE: Write directly to the disk since we won't use a buffer,
// unlike in P2 where the inode struct has data attribute.
// ...
  //printf("inode create\n");
  
  struct inode_disk *disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode == NULL)
  {
    return NULL;
  }

  //printf("inode create 2\n");
  /* set inode type */
  disk_inode->type = type;
  disk_inode->magic = INODE_MAGIC;

  /* write sector to disk */
  block_write(fs_device, sector, disk_inode);
  //printf("inode create 3\n");

  /* free disk inode? */
  free(disk_inode);

  return inode_open(sector); // make sure this isn't null
}


// /* Initializes an inode with LENGTH bytes of data and
//    writes the new inode to sector SECTOR on the file system
//    device.
//    Returns true if successful.
//    Returns false if memory or disk allocation fails. */
// bool
// inode_create (block_sector_t sector, off_t length)
// {
//   struct inode_disk *disk_inode = NULL;
//   bool success = false;

//   ASSERT (length >= 0);

//   /* If this assertion fails, the inode structure is not exactly
//      one sector in size, and you should fix that. */
//   ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

//   disk_inode = calloc (1, sizeof *disk_inode);
//   if (disk_inode != NULL)
//     {
//       size_t sectors = bytes_to_sectors (length);
//       disk_inode->length = length;
//       disk_inode->magic = INODE_MAGIC;
//       if (free_map_allocate (sectors, &disk_inode->start)) 
//         {
//           block_write (fs_device, sector, disk_inode);
//           if (sectors > 0) 
//             {
//               static char zeros[BLOCK_SECTOR_SIZE];
//               size_t i;
              
//               for (i = 0; i < sectors; i++) 
//                 block_write (fs_device, disk_inode->start + i, zeros);
//             }
//           success = true; 
//         } 
//       free (disk_inode);
//     }
//   return success;
// }

/* Reads an inode from SECTOR
and returns a `struct inode' that contains it.
Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  // printf("inode open! %d\n", sector);
  //printf("inode open\n");
// Don't forget to access open_inodes list
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
  {
    //printf("iterating\n");
    inode = list_entry (e, struct inode, elem);
    if (inode->sector == sector) 
    {
      //printf("?\n");
      inode_reopen (inode);
      return inode; 
    }
  }

  //printf("inode isn't already open\n");

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  //printf("initializing inode\n");
  /* initialize */
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->removed = false;
  lock_init(&inode->lock);
  lock_init(&inode->deny_write_lock);
  cond_init(&inode->no_writers_cond);
  inode->deny_write_cnt = 0;
  inode->writer_cnt = 0;
  inode->magic = INODE_MAGIC;

  //printf("adding to open inodes list\n");
  /* add to open inodes list */
  lock_acquire(&open_inodes_lock);
  list_push_front (&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);

  return inode;
}

// /* Reads an inode from SECTOR
//    and returns a `struct inode' that contains it.
//    Returns a null pointer if memory allocation fails. */
// struct inode *
// inode_open (block_sector_t sector)
// {
//   struct list_elem *e;
//   struct inode *inode;

//   /* Check whether this inode is already open. */
//   for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
//        e = list_next (e)) 
//     {
//       inode = list_entry (e, struct inode, elem);
//       if (inode->sector == sector) 
//         {
//           inode_reopen (inode);
//           return inode; 
//         }
//     }

//   /* Allocate memory. */
//   inode = malloc (sizeof *inode);
//   if (inode == NULL)
//     return NULL;

//   /* Initialize. */
//   list_push_front (&open_inodes, &inode->elem);
//   inode->sector = sector;
//   inode->open_cnt = 1;
//   inode->deny_write_cnt = 0;
//   inode->removed = false;
//   block_read (fs_device, inode->sector, &inode->data);
//   return inode;
// }

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  ASSERT(inode->open_cnt > 0);
  //printf("inode_reopen\n");
  if (inode != NULL)
  {
    lock_acquire (&open_inodes_lock);
    //printf("lock acquired\n");
    inode->open_cnt++;
    lock_release (&open_inodes_lock);
    //printf("lock released\n");
  }
  //printf("returning inode\n");
  //printf("inode open count %d\n", inode->open_cnt);
  return inode;
}

/* Returns the type of INODE. */
enum inode_type
inode_get_type (const struct inode *inode)
{
  ASSERT(inode != NULL);

  struct inode_disk disk_inode;
  block_read(fs_device, inode->sector, &disk_inode);
  return disk_inode.type;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      lock_acquire(&open_inodes_lock);
      list_remove (&inode->elem);
      lock_release(&open_inodes_lock);

      /* if removed, we want to get rid of data */

      /* deallocate inode and free */
      deallocate_inode(inode);
      free (inode); 
    }
}

/* Deallocates SECTOR and anything it points to recursively.
LEVEL is 2 if SECTOR is doubly indirect,
or 1 if SECTOR is indirect,
or 0 if SECTOR is a data sector. */
static void
deallocate_recursive (block_sector_t sector, int level)
{
// deallocate_recursive, .....
  // printf("deallocate recursive not implemented\n");
}

/* Deallocates the blocks allocated for INODE. */
static void
deallocate_inode (const struct inode *inode)
{
// NOTE: you should call deallocate recursive here to
// recursively iterate each level of indirection ..
  //printf("Deallocate inode not implemented\n");
}


/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Translates SECTOR_IDX into a sequence of block indexes in
OFFSETS and sets *OFFSET_CNT to the number of offsets.
offset_cnt can be 1 to 3 depending on whether sector_idx
points to sectors within DIRECT, INDIRECT, or DBL_INDIRECT ranges.
*/
static void
calculate_indices (off_t sector_idx, size_t offsets[], size_t *offset_cnt)
{
  //printf("calculate indices\n");
  /* Handle direct blocks. When sector_idx < DIRECT_CNT */
  if (sector_idx < DIRECT_CNT)
  {
    *offset_cnt = 1;
    offsets[0] = sector_idx;
  } 
  else if (sector_idx < DIRECT_CNT + PTRS_PER_SECTOR * INDIRECT_CNT)
  {
    *offset_cnt = 2;
    offsets[0] = DIRECT_CNT;
    offsets[1] = (sector_idx - DIRECT_CNT); //% PTRS_PER_SECTOR; 
  }
  else
  {
    *offset_cnt = 3;
    offsets[0] = DIRECT_CNT + INDIRECT_CNT;  
    size_t remaining = sector_idx - (DIRECT_CNT + (INDIRECT_CNT * PTRS_PER_SECTOR));
    offsets[1] = remaining / PTRS_PER_SECTOR; 
    offsets[2] = remaining % PTRS_PER_SECTOR; 
  }

  //printf("calculated indices\n");
}

/* Retrieves the data block for the given byte OFFSET in INODE,
setting *DATA_BLOCK to the block and data_sector to the sector to write
(for inode_write_at method).
Returns true if successful, false on failure.
If ALLOCATE is false (usually for inode read), then missing blocks
will be successful with *DATA_BLOCK set to a null pointer.
If ALLOCATE is true (for inode write), then missing blocks will be allocated.
This method may be called in parallel */
static bool
get_data_block (struct inode *inode, off_t offset, bool allocate,
void **data_block, block_sector_t *data_sector)
{
  // printf("get data block\n");
  size_t offsets[3];
  size_t offset_cnt;
  off_t sector_idx = offset / BLOCK_SECTOR_SIZE; // TODO is this right?
  uint8_t *buffer = calloc(1, BLOCK_SECTOR_SIZE);

  calculate_indices(sector_idx, offsets, &offset_cnt);
  // printf("offset count %d\n", offset_cnt);

  block_sector_t sector = inode->sector;
  for (int i = 0; i < offset_cnt; i++)
  {
    // printf("in loop i:%d\n", i);
    block_read(fs_device, sector, buffer);
    block_sector_t next_sector = ((block_sector_t *) buffer)[offsets[i]];

    // printf("read block and got next sector\n");
    // printf("next sector: %d\n", next_sector);
    // printf("allocate?: %d\n", allocate);

    if (next_sector == 0) // is this the right check?
    {
      if (!allocate)
      {
        *data_block = NULL;
        *data_sector = 0;
        return false; //?
      }

      //printf("free map allocation\n");
      if (!free_map_allocate(&next_sector))
      {
        // allocation of a new sector failed
        return false; 
      }

      //printf("new sector: %d\n", next_sector);
      // allocate new device with zeros
      char *zeros = calloc(1, BLOCK_SECTOR_SIZE);
      block_write(fs_device, next_sector, zeros);

      // printf("block wrote\n");

      // update current sector with allocated block
      ((block_sector_t *) buffer)[offsets[i]] = next_sector;
      block_write(fs_device, sector, buffer);
    }

    sector = next_sector;
  }

  // read data block and set data
  //printf("finished loop\n");
  block_read(fs_device, sector, buffer);
  *data_block = buffer;
  *data_sector = sector;

  return true;

/* NOTE: calculate_indices ... then access the sectors in the sequence
* indicated by calculate_indices
* Don't forget to check whether the block is allocated (e.g., direct,
indirect,
* and double indirect sectors may be zero/unallocated, which needs to be
handled
* based on the bool allocate
* ALLOCATE in this function means you need to ask
* bitmap/freemap (space manager) to allocate free sector for you */
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
Returns the number of bytes actually read, which may be less
than SIZE if an error occurs or end of file is reached.
Modifications might be/might not be needed for this function template. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  block_sector_t target_sector = 0; // not really useful for inode_read
  while (size > 0)
  {
  /* Sector to read, starting byte offset within sector, sector data. */
  int sector_ofs = offset % BLOCK_SECTOR_SIZE;
  void *block; // NOTE: may need to be allocated in get_data_block method,
  // and don't forget to free it in the end
  /* Bytes left in inode, bytes left in sector, lesser of the two. */
  off_t inode_left = inode_length (inode) - offset;
  int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
  int min_left = inode_left < sector_left ? inode_left : sector_left;
  /* Number of bytes to actually copy out of this sector. */
  int chunk_size = size < min_left ? size : min_left;
  if (chunk_size <= 0 || !get_data_block (inode, offset, false, &block,
  &target_sector))
  break;
  if (block == NULL)
  memset (buffer + bytes_read, 0, chunk_size);
  else
  {
    memcpy (buffer + bytes_read, block + sector_ofs, chunk_size);
  }
  /* Advance. */
  size -= chunk_size;
  offset += chunk_size;
  bytes_read += chunk_size;
  free(block); // NOTE: if needed?
  }
  return bytes_read;
}

/* Extends INODE to be at least LENGTH bytes long. */
static void
extend_file (struct inode *inode, off_t length)
{
  struct inode_disk *disk_inode = calloc (1, sizeof *disk_inode);
  block_read(fs_device, inode->sector, disk_inode);
  if (disk_inode->length < length) {
    disk_inode->length = length;
  }
  block_write(fs_device, inode->sector, disk_inode);
  free(disk_inode);

  /*ASSERT(inode != NULL);

  if (length > INODE_SPAN)
  {
    length = INODE_SPAN; // max length of a file
  }

  // read disk inode
  struct inode_disk disk_inode;
  block_read(fs_device, inode->sector, &disk_inode);

  size_t needed_sectors = bytes_to_sectors(length);
  size_t current_sectors = bytes_to_sectors(disk_inode.length);

  if (needed_sectors <= current_sectors)
  {
    return; // do i need to update file length?
  }

  for(size_t i = current_sectors; i < needed_sectors; i++)
  {
    uint8_t *block;
    block_sector_t sector;
    if (!get_data_block(inode, i * BLOCK_SECTOR_SIZE, true, &block, &sector))
    {
      // allocation failed
      return false;
    }
    
  }

  disk_inode.length = length;
  block_write(fs_device, inode->sector, &disk_inode);
  return true;*/
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
Returns the number of bytes actually written, which may be
less than SIZE if an error occurs.
Some modifications might be needed for this function template.*/
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
off_t offset)
{
 //printf("inode write at\n");


  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  block_sector_t target_sector = 0;
  /* Don't write if writes are denied. */
  lock_acquire (&inode->deny_write_lock);
  if (inode->deny_write_cnt)
  {
  lock_release (&inode->deny_write_lock);
  return 0;
  }
  inode->writer_cnt++;
  lock_release (&inode->deny_write_lock);

  //printf("inode write at 2\n");
  while (size > 0)
  {
    //printf("size > 0: %d\n", size);
  /* Sector to write, starting byte offset within sector, sector data. */
  int sector_ofs = offset % BLOCK_SECTOR_SIZE;
  void *block; // may need to be allocated in get_data_block method,
  // and don't forget to free it in the end
  /* Bytes to max inode size, bytes left in sector, lesser of the two. */
  off_t inode_left = INODE_SPAN - offset;
  int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
  int min_left = inode_left < sector_left ? inode_left : sector_left;
  /* Number of bytes to actually write into this sector. */
  int chunk_size = size < min_left ? size : min_left;
  if (chunk_size <= 0 || !get_data_block (inode, offset, true, &block,
  &target_sector))
  break;
  //printf("got data block\n");
  memcpy (block + sector_ofs, buffer + bytes_written, chunk_size);
  block_write(fs_device, target_sector, block);
  /* Advance. */
  size -= chunk_size;
  offset += chunk_size;
  bytes_written += chunk_size;
  free(block); // NOTE: if needed?
  }

  //printf("inode write at 3\n");
  extend_file (inode, offset);
  lock_acquire (&inode->deny_write_lock);
  if (--inode->writer_cnt == 0)
  cond_signal (&inode->no_writers_cond, &inode->deny_write_lock);
  lock_release (&inode->deny_write_lock);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  // printf("inode deny write\n");
  lock_acquire(&inode->deny_write_lock);
  while (inode->writer_cnt > 0) {
    cond_wait(&inode->no_writers_cond, &inode->deny_write_lock);
  }
  inode->deny_write_cnt++;
  lock_release(&inode->deny_write_lock);
  // printf("inode deny write count %d\n", inode->deny_write_cnt);

  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  // printf("deny write count %d\n", inode->deny_write_cnt);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);

  lock_acquire(&inode->deny_write_lock);
  inode->deny_write_cnt--;
  lock_release(&inode->deny_write_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  ASSERT(inode != NULL);
  //printf("inode->sector, %d\n", inode->sector);
  //printf("inode magic, %d\n", inode->magic == INODE_MAGIC);
  //printf("\n");

  struct inode_disk *disk_inode = calloc(1, sizeof *disk_inode);
  block_read(fs_device, inode->sector, disk_inode);
  off_t length = disk_inode->length;
  free(disk_inode);
  return length;
  
}

/* Returns the number of openers. */
int
inode_open_cnt (const struct inode *inode)
{
  int open_cnt;

  lock_acquire (&open_inodes_lock);
  open_cnt = inode->open_cnt;
  lock_release (&open_inodes_lock);

  return open_cnt;
}

/* Locks INODE. */
void
inode_lock (struct inode *inode)
{
  lock_acquire (&inode->lock);
}

/* Releases INODE's lock. */
void
inode_unlock (struct inode *inode)
{
  lock_release (&inode->lock);
}


