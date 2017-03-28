#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
// Blcok 0 is unused
// Block 1 is super blcok 
// Inode starts at 2

#define ROOTINO 1 // root i-number
#define BSIZE 512 // block size
int power(int base, int exp) {
  int i;
  int product = 1;
  for(i = 0; i< exp; ++i) {
    product *= base;
  }
  return product;
}

// File system super block
struct superblock {
  uint size;        // Size of file system image (blocks)
  uint nblocks;     // Number of data blocks
  uint ninodes;     // Number of inodes
};

#define NDIRECT (12)
// On-disk inode structure
struct dinode {
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

#define T_DIR (1)
#define T_FILE (2)
#define T_DEV (3)

#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};


// xv6 fs img
// similar to vsfs
// free block | superblock | inode table | bitmap (data) | data blocks
// (some gaps in here)

int
main(int argc, char *argv[])
{

  if(argc != 2) {
    fprintf(stderr, "image not found.\n");
    exit(1);
    } 
  int fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "image not found.\n");
    exit(1);
  }

  int rc;
  struct stat sbuf;
  rc  = fstat(fd, &sbuf);
  assert(rc == 0);

  void *img_ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(img_ptr != MAP_FAILED);
  
  struct superblock *sb;
  sb = (struct superblock  *) (img_ptr + BSIZE);

  unsigned int inode_size = sizeof(struct dinode)*sb->ninodes;
  unsigned int iblocks = (inode_size/BSIZE) + 1;

  // array of used blocks
  unsigned int used[sb->size];
  memset(used, 0, sb->size*sizeof(unsigned int));

  // array of blocks
  unsigned int block_array[sb->size]; 
  memset(block_array, 0, sb->size*sizeof(unsigned int));

  // array of referenced inodes
  unsigned int ireferenced[sb->ninodes];
  memset(ireferenced, 0, sb->ninodes*sizeof(unsigned int));

  // array of used inodes
  unsigned int iused[sb->ninodes];
  memset(iused, 0, sb->ninodes*sizeof(unsigned int));

  // array for referenced
  unsigned int ilinks[sb->ninodes];
  memset(ilinks, 0, sb->ninodes*sizeof(unsigned int));

  // array for extra links
  unsigned int dironce[sb->ninodes];
  memset(dironce, 0, sb->ninodes*sizeof(unsigned int));

  // make bitmap
  int buf[sb->size];
  char* bitmap = img_ptr + (2*BSIZE) + (iblocks*BSIZE);
  int m;
  int offset = 0;
  for(m = 0; m < sb->size; m++) {
    int temp = power(2, offset);
    buf[m] = temp & *bitmap;
    if(offset >= 7) {
      offset = 0;
      bitmap++;
    }
    else {
      offset++;
    }
  }
 
  int i;
  struct dinode *head = (struct dinode*)(img_ptr + 2*BSIZE);
  struct dinode *dip = (struct dinode *) (img_ptr + 2*BSIZE);
  for ( i = 0; i < sb->ninodes; i++) {
    // check if valid type
    if (dip->type != T_DIR && dip->type != T_FILE && dip->type != T_DEV && dip->type != 0) {
      fprintf(stderr, "ERROR: bad inode.\n");
      exit(1);
    }
    // check if inode 1 is a directory (root)
   if (i == ROOTINO && dip->type != T_DIR) {
      fprintf(stderr, "ERROR: root directory does not exist.\n");
      exit(1);
    }

   if (i == ROOTINO && dip->type == T_DIR) {
      struct dirent *dir;
      dir = img_ptr+(BSIZE*dip->addrs[0]);
      struct dirent *dir_next = dir+1;
      if(dir->inum != dir_next->inum && (dir->inum == ROOTINO || dir_next->inum == ROOTINO)) {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
        }
   }

    if(dip->type != 0) {
    // mark inode as used
    iused[i] = 1;
    // first check indirect
    int j;

    for (j = 0; j < NDIRECT+1; j++) {
      if ((dip->addrs[j] > 1023) || 
         (dip->addrs[j] < 0)) {
        fprintf(stderr, "ERROR: bad address in inode.\n");
        exit(1);
      }
      if (buf[dip->addrs[j]] <= 0) {
        fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
        exit(1);
      }
      if (used[dip->addrs[j]] == 1 && dip->addrs[j] != 0) {
        fprintf(stderr, "ERROR: address used more than once.\n");
        exit(1);
        }
      else {
        used[dip->addrs[j]] = 1;
        }
      block_array[dip->addrs[j]] = 1;
    }
    // check indirect
  //  if(dip->addrs[NDIRECT] != 0) {
    int k;
    void *block = img_ptr + (BSIZE*(dip->addrs[NDIRECT]));
    unsigned int *b = (unsigned int*) block;
    for (k = 0; k < 128; k++) {
      if (*(b) > 1023 || (*(b) < 0)) {
        fprintf(stderr, "ERROR: bad address in inode.\n");
        exit(1);
      }
      if (buf[*b] <= 0) {
        fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
        exit(1);
      }
      block_array[*b] = 1;
      if (used[*b] == 1 && *b != 0) {
        fprintf(stderr, "ERROR: address used more than once.\n");
        exit(1);
        }
      else {
        used[*b] = 1;
        }
      b++;
    }
 // } 

  // Checking . and .. directories
  if (dip->type == T_DIR) {
    int dot_exist = 0;
    int double_dot_exist = 0;
    struct dirent *dir;
    struct dirent *next;
    struct dirent *dir_parent;
    int pflag = 0;
    dir = img_ptr+(BSIZE*dip->addrs[0]);
    next = dir+1;
      if(strcmp(dir->name, ".") == 0) {
        dot_exist = 1;
      }
      if(strcmp(next->name, "..") == 0) {
        double_dot_exist = 1;
      }

    
    if (dot_exist == 0 || double_dot_exist == 0) {
      fprintf(stderr, "ERROR: directory not properly formatted.\n");
      exit(1);      
      }
    struct dinode *iparent = head+ next->inum;
    if (iparent->type != T_DIR) {
      fprintf(stderr, "ERROR: parent directory mismatch.\n");
      exit(1);
    }
 
    unsigned int num_dirs = BSIZE/sizeof(struct dirent);
    int d;
    for(d = 0; d < NDIRECT; d++) {
      int c;
      dir = img_ptr+(BSIZE*dip->addrs[d]);
      dir_parent = img_ptr+(BSIZE*iparent->addrs[d]); // Parents datablocks
      for(c = 0; c < num_dirs; c++) {
         if(dir->inum!=0) {
           ireferenced[dir->inum] = 1;
           struct dinode *node = (struct dinode *)(head + dir->inum);
           if (node->type == T_FILE) {
             ilinks[dir->inum]++;
           }
           if(node->type == T_DIR && strcmp(dir->name, ".") != 0 && strcmp(dir->name, "..") != 0) {
             dironce[dir->inum]++;
           }
         }
         if(dir_parent->inum!=0) {
           if(dir_parent->inum == i  && strcmp(dir_parent->name, ".") != 0 && strcmp(dir_parent->name, "..") != 0) {
             pflag = 1;
          }
         }
         dir++;
         dir_parent++; 
      }
    }
    void *block = img_ptr + (BSIZE*(dip->addrs[NDIRECT]));
    unsigned int *b = (unsigned int*) block;
    // parent indirent for test 5
    void *p_un = img_ptr + (BSIZE*(iparent->addrs[NDIRECT]));
    unsigned int *pi = (unsigned int*) p_un;
    for(d = 0; d < 128; d++) {
      int c;
      dir = img_ptr+(BSIZE*(*b));
      dir_parent = img_ptr+(BSIZE*(*pi));
      for(c = 0; c < num_dirs; c++) {
         if(dir->inum!=0) {
           ireferenced[dir->inum] = 1;
           struct dinode *node = (struct dinode *)(head + dir->inum);
           if (node->type == T_FILE) {
             ilinks[dir->inum]++;
           }
           if(node->type == T_DIR && strcmp(dir->name, ".") != 0 &&
 strcmp(dir->name, "..") != 0) {
             dironce[dir->inum]++;
           }
         }
         if(dir_parent->inum != 0) {
           if(dir_parent->inum == i && strcmp(dir_parent->name, ".") != 0 && strcmp(dir_parent->name, "..") != 0) {
             pflag = 1;
           }
         }
         dir++;
         dir_parent++;
      }
      b++;
      pi++;
    }

    // Error 5
    if(pflag == 0 && i != ROOTINO) {
      fprintf(stderr, "ERROR: parent directory mismatch.\n");
      exit(1);
    }
       
    }
    
    }
   dip++;
  }

  // check bitmap
  int p;
  for(p = 0; p < sb->size; p++) {
    if(p <= iblocks+2)
      continue;
    if(block_array[p] == 0 && buf[p] > 0) {
      fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
      exit(1);
      }
    }

  // check inodes
  struct dinode *node = head;
  for(p = 0; p < sb->ninodes; p++) {
    if (iused[p] == 1 && ireferenced[p] == 0) {
      fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
      exit(1);
    }

    if (ireferenced[p] == 1 && iused[p] == 0) {
      fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
      exit(1);
    }
    
    if(ilinks[p] != node->nlink && node->type == T_FILE) {
      fprintf(stderr, "ERROR: bad reference count for file.\n");
      exit(1);
    }
    if(node->type == T_DIR && dironce[p] > 1) {
      fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
      exit(1);  
    
    }

    node++;
  }

  return 0;
}
