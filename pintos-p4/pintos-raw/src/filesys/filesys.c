#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);


/* Initializes the file system module.
If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
to disk. */
void
filesys_done (void)
{
  free_map_close ();
}

/* Extracts a file name part from *SRCP into PART,
and updates *SRCP so that the next call will return the next
file name part.
Returns 1 if successful, 0 at end of string, -1 for a too-long
file name part. */
// NOTE: this is simple function to help you chop path string
// e.g. if
// char part[NAME_MAX];
// const char* name = "/a/b/c";
// then:
// 1st get_next_part(part, name) // part: "a" name: "/b/c"
// 2nd get_next_part(part, name) // part: "b" name: "/c"
// 3rd get_next_part(part, name) // part: "c" name: ""
// you get the idea :)
static int
get_next_part (char part[NAME_MAX], const char **srcp)
{
const char *src = *srcp;
char *dst = part;
/* Skip leading slashes.
If it's all slashes, we're done. */
while (*src == '/')
src++;
if (*src == '\0')
return 0;
/* Copy up to NAME_MAX character from SRC to DST.
Add null terminator. */
while (*src != '/' && *src != '\0')
{
if (dst < part + NAME_MAX)
*dst++ = *src;
else
return -1;
src++;
}
*dst = '\0';
/* Advance source pointer. */
*srcp = src;
return 1;
}

/* Resolves relative or absolute file NAME.
   Returns true if successful, false on failure.
   Stores the directory corresponding to the name into *DIRP,
   and the file name part into BASE_NAME. */
static bool
resolve_name_to_entry (const char *name,
                       struct dir **dirp, char base_name[NAME_MAX + 1]) 
{
  if (name == NULL || strlen(name) == 0) 
    return false;

  /* Determine starting directory (current working directory or root). */
  struct dir *dir = thread_current()->cwd;
  if (name[0] == '/' || dir == NULL) 
  {
    dir = dir_open_root();
  } 

  if (dir == NULL)
    return false;

  /* Parse the path component by component. */
  char part[NAME_MAX + 1];
  struct inode *inode = NULL;

  while (get_next_part(part, &name) == 1) 
  {
    //printf("part(loop): %s\n", part);
    //printf("name(loop): %s\n", name);
    /* Lookup the next part in the directory. */
    if (!dir_lookup(dir, part, &inode)) 
    {
      break; // Part not found.
    }

    /* If the found inode is a directory, move into it. */
    if (inode_get_type(inode) == DIR_INODE && strcmp(name, "")) 
    {
      struct dir *next_dir = dir_open(inode);
      if (next_dir == NULL) 
      {
        inode_close(inode);
        dir_close(dir);
        return false; // Failed to open directory.
      }
      dir_close(dir);
      dir = next_dir;
    } 
    // else 
    // {
    //   /* Found a file in the middle of the path. */
    //   inode_close(inode);
    //   dir_close(dir);
    //   return false;
    // }
  }

  /* Copy the last part of the path into base_name. */
  strlcpy(base_name, part, NAME_MAX + 1);
  *dirp = dir;
  return true;
}


/* Resolves relative or absolute file NAME to an inode.
Returns an inode if successful, or a null pointer on failure.
The caller is responsible for closing the returned inode. */
static struct inode *
resolve_name_to_inode (const char *name)
{
  //printf("resolve_name_to_inode %s\n", name);
  struct dir *dir;
  char base_name[NAME_MAX + 1];
  struct inode *inode;

  if(!resolve_name_to_entry(name, &dir, base_name))
  {
    return NULL;
  }

  //printf("base name %s\n", base_name);
  if (dir != NULL)
    dir_lookup(dir, base_name, &inode);
  dir_close(dir);



  return inode;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
Returns true if successful, false otherwise.
Fails if a file named NAME already exists,
or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, enum inode_type type)
{
  if (name == NULL || !strcmp(name, ""))
  {
    sys_exit(-1);
  }

  //printf("name %s\n", name);

  block_sector_t inode_sector = 0;
  struct dir *dir;
  char base_name[NAME_MAX + 1];
  bool resolved = resolve_name_to_entry(name, &dir, base_name); // return type?
  bool success = false;

  if (base_name == NULL || !strcmp(base_name, ""))
  {
    printf("basename issue\n");
  }

  //printf("resolved %d\n", resolved);
  //printf("base_name %s\n", base_name);

  if (type == FILE_INODE)
  {
    // printf("file create\n");
    // printf("1. %d\n", dir != NULL);
    // printf("2. %d\n", free_map_allocate(&inode_sector));
    // printf("3. %d\n", file_create(inode_sector, initial_size));
    // printf("4. %d\n", dir_add(dir, base_name, inode_sector));
    success = (
      dir != NULL
      && free_map_allocate(&inode_sector)
      && file_create(inode_sector, initial_size)
      && dir_add(dir, base_name, inode_sector)
    );
  }
  else
  {
    success = (
      dir != NULL
      && free_map_allocate(&inode_sector)
      && dir_create(inode_sector, inode_get_inumber(dir_get_inode(dir)))
      && dir_add(dir, base_name, inode_sector)
    );
  }
  

  /* should I include this? */
  /*&if (!success && inode_sector != 0) 
  {
    free_map_release(&inode_sector);
  }*/

  if (dir != NULL)
  {
    dir_close(dir);
  }
  
  return success;
}

// /* Creates a file named NAME with the given INITIAL_SIZE.
//    Returns true if successful, false otherwise.
//    Fails if a file named NAME already exists,
//    or if internal memory allocation fails. */
// bool
// filesys_create (const char *name, off_t initial_size) 
// {
//   block_sector_t inode_sector = 0;
//   struct dir *dir = dir_open_root ();
//   bool success = (dir != NULL
//                   && free_map_allocate (1, &inode_sector)
//                   && inode_create (inode_sector, initial_size)
//                   && dir_add (dir, name, inode_sector));
//   if (!success && inode_sector != 0) 
//     free_map_release (inode_sector, 1);
//   dir_close (dir);

//   return success;
// }

/* Opens the file with the given NAME.
Returns the new file if successful or a null pointer
otherwise.
Fails if no file named NAME exists,
or if an internal memory allocation fails. */
struct inode *
filesys_open (const char *name)
{
  return resolve_name_to_inode(name);
}

// /* Opens the file with the given NAME.
//    Returns the new file if successful or a null pointer
//    otherwise.
//    Fails if no file named NAME exists,
//    or if an internal memory allocation fails. */
// struct file *
// filesys_open (const char *name)
// {
//   struct dir *dir = dir_open_root ();
//   struct inode *inode = NULL;

//   if (dir != NULL)
//     dir_lookup (dir, name, &inode);
//   dir_close (dir);

//   return file_open (inode);
// }

/* Deletes the file named NAME.
Returns true if successful, false on failure.
Fails if no file named NAME exists,
or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct dir *dir;
  char base_name[NAME_MAX + 1];

  bool success = 
  (
    resolve_name_to_entry(name, &dir, base_name)
    && dir != NULL
    && dir_remove(dir, base_name)
  );
  dir_close(dir);

  return success;
}

// /* Deletes the file named NAME.
//    Returns true if successful, false on failure.
//    Fails if no file named NAME exists,
//    or if an internal memory allocation fails. */
// bool
// filesys_remove (const char *name) 
// {
//   struct dir *dir = dir_open_root ();
//   bool success = dir != NULL && dir_remove (dir, name);
//   dir_close (dir); 

//   return success;
// }

/* Change current directory to NAME.
Return true if successful, false on failure. */
bool
filesys_chdir (const char *name)
{
  //printf("filesys_chdir\n");
  struct inode *inode = resolve_name_to_inode(name);
  if (inode == NULL || inode_get_type(inode) != DIR_INODE)
  {
    return false;
  }

  if(thread_current()->cwd)
    dir_close(thread_current()->cwd);
  thread_current()->cwd = dir_open(inode);
  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  struct inode *inode;
  printf ("Formatting file system...");

  /* Set up free map. */
  free_map_create ();

  /* Set up root directory. */
  inode = dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR);

  if (inode == NULL)
    PANIC ("root directory creation failed");
  inode_close (inode);

  free_map_close ();
  printf ("done.\n");
}
