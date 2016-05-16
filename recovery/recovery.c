#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <string.h>


// data structure of boot sector, copy from tutorial 6
#pragma pack(push,1)
struct BootEntry {
    unsigned char BS_jmpBoot[3];    /* Assembly instruction to jump to boot code */
    unsigned char BS_OEMName[8];    /* OEM Name in ASCII */
    unsigned short BPB_BytsPerSec; /* Bytes per sector. Allowed values include 512, 1024, 2048, and 4096 */
    unsigned char BPB_SecPerClus; /* Sectors per cluster (data unit). Allowed values are powers of 2, but the cluster size must be 32KB or smaller */
    unsigned short BPB_RsvdSecCnt;    /* Size in sectors of the reserved area */
    unsigned char BPB_NumFATs;    /* Number of FATs */
    unsigned short BPB_RootEntCnt; /* Maximum number of files in the root directory for FAT12 and FAT16. This is 0 for FAT32 */
    unsigned short BPB_TotSec16;    /* 16-bit value of number of sectors in file system */
    unsigned char BPB_Media;    /* Media type */
    unsigned short BPB_FATSz16; /* 16-bit size in sectors of each FAT for FAT12 and FAT16.  For FAT32, this field is 0 */
    unsigned short BPB_SecPerTrk;    /* Sectors per track of storage device */
    unsigned short BPB_NumHeads;    /* Number of heads in storage device */
    unsigned int BPB_HiddSec;    /* Number of sectors before the start of partition */
    unsigned int BPB_TotSec32; /* 32-bit value of number of sectors in file system.  Either this value or the 16-bit value above must be 0 */
    unsigned int BPB_FATSz32;    /* 32-bit size in sectors of one FAT */
    unsigned short BPB_ExtFlags;    /* A flag for FAT */
    unsigned short BPB_FSVer;    /* The major and minor version number */
    unsigned int BPB_RootClus;    /* Cluster where the root directory can be found */
    unsigned short BPB_FSInfo;    /* Sector where FSINFO structure can be found */
    unsigned short BPB_BkBootSec;    /* Sector where backup copy of boot sector is located */
    unsigned char BPB_Reserved[12];    /* Reserved */
    unsigned char BS_DrvNum;    /* BIOS INT13h drive number */
    unsigned char BS_Reserved1;    /* Not used */
    unsigned char BS_BootSig; /* Extended boot signature to identify if the next three values are valid */
    unsigned int BS_VolID;    /* Volume serial number */
    unsigned char BS_VolLab[11]; /* Volume label in ASCII. User defines when creating the file system */
    unsigned char BS_FilSysType[8];    /* File system type label in ASCII */
};

// data structure of directory entry, copy from tutorial 6
struct DirEntry {
    unsigned char DIR_Name[11]; /* File name */
    unsigned char DIR_Attr; /* File attributes */
    unsigned char DIR_NTRes;  /* Reserved */
    unsigned char DIR_CrtTimeTenth; /* Created time (tenths of second) */
    unsigned short DIR_CrtTime; /* Created time (hours, minutes, seconds) */
    unsigned short DIR_CrtDate; /* Created day */
    unsigned short DIR_LstAccDate; /* Accessed day */
    unsigned short DIR_FstClusHI; /* High 2 bytes of the first cluster address */
    unsigned short DIR_WrtTime; /* Written time (hours, minutes, seconds */
    unsigned short DIR_WrtDate; /* Written day */
    unsigned short DIR_FstClusLO;  /* Low 2 bytes of the first cluster address */
    unsigned int DIR_FileSize; /* File size in bytes. (0 for directories) */
};
#pragma pack(pop)

struct BootEntry *boot;
struct DirEntry *dir;
struct DirEntry *tmpDE;
unsigned int *fat;
unsigned int fat_size;
unsigned int byte_per_sector;
unsigned int sector_per_cluster;
unsigned int total_sector;
unsigned int reserved_sector;
unsigned int fat_sector;
unsigned int data_sector;
unsigned int cluster_size;
unsigned int cluster_count;
unsigned int max_dir_count;
unsigned int dir_per_cluster;
unsigned int current; // current cluster

const unsigned int BAD_CLUSTER = 0x0FFFFFF7;
const unsigned char DIRECTORY = 0x10;  // File Attribute
const unsigned char LFN = 0x0f; // File Attribute
const unsigned char DELETED = 0xe5;

int fd; // file descriptor
char fullpath[1025]; // full pathname

off_t fat_offset;
off_t data_offset;

char *device = NULL;
char *target = NULL;
//char *ltarget = NULL;
//char *rtarget = NULL;
char *dest = NULL;

int dflag = 0;
int lflag = 0;
int rflag = 0;
int oflag = 0;

void print_usage(char *cmd) {
  printf(
        "Usage: %s -d [device filename] [other arguments]\n"  \
        "-l target            List the target directory\n"  \
        "-r target -o dest    Recover the target pathname\n", \
        cmd);
  exit(EXIT_FAILURE);
}

void options(int argc, char **argv) {
  int cmd_opt;
  while ((cmd_opt = getopt(argc, argv, "d:l:r:o:")) != -1) {
    switch (cmd_opt) {
      case 'd':
        dflag = 1;
        device = optarg;
        break;
      case 'l':
        if (!dflag)
          print_usage(argv[0]);
        lflag = 1;
        target = optarg;
        break;
      case 'r':
        if (!dflag)
          print_usage(argv[0]);
        rflag = 1;
        target = optarg;
        strcpy(fullpath,optarg);
        break;
      case 'o':
        if (!dflag || !rflag)
          print_usage(argv[0]);
        oflag = 1;
        dest = optarg;
        break;
      default:
        print_usage(argv[0]);
        break;
    }
  }
  /* will l and r appear at the same time? NO */
  if ( !dflag || (!lflag && !rflag) || (lflag && rflag) || (rflag && !oflag) || optind != argc )
    print_usage(argv[0]);
}

void get_dir_entries() {
  unsigned int i;
  int dir_span = 0;
  for (i=current; i<BAD_CLUSTER; i=fat[i]) {
      dir_span++;
  }
  //printf("dir_span: %d\n", dir_span);
  dir = malloc(dir_span * cluster_size); // Maximum: dir_span * cluster_size
  tmpDE = dir;

  max_dir_count = dir_span * dir_per_cluster; // Maximum: dir_span * dir_per_cluster;
  //printf("max_dir_count: %d\n", max_dir_count);

  for (i=current; i<BAD_CLUSTER; i=fat[i]) {
      pread(fd, tmpDE, cluster_size, data_offset + (i-2) * cluster_size);
      tmpDE += dir_per_cluster;
  }
}

void go_down(char token[]) {
  int i; int j;
  for (i=0; i<max_dir_count; i++) {
    char dirname[1025] = "";
    int k = 0;
    if (!(dir[i].DIR_Attr & DIRECTORY))
      continue;
    for (j=0; j<8; j++) { // only name part for directory
      if (dir[i].DIR_Name[j] == ' ')  // skip space
        break;
      dirname[k++] = dir[i].DIR_Name[j];
    }
    dirname[k] = '\0';
    /* compare */
    if (strcmp(token,dirname) == 0) {
      //printf("bingo: %d\n", i);
      current = (dir[i].DIR_FstClusHI<<16)+dir[i].DIR_FstClusLO;
      break;
    }
  }
}

/* note that suffix increment(++) has a higher percedence than dereference(*) */
void list() {
  int i; int j; int orderno = 1;
	for (i=0; i<max_dir_count; i++) {
    char filename[1025] = "";
    int k = 0;
		if (dir[i].DIR_Name[0] == 0 || dir[i].DIR_Attr == LFN) // dir[i].DIR_Attr == 0x0f indicates LFN, skip
			continue;
    for (j=0; j<8; j++) { // name part
		    if (dir[i].DIR_Name[j] == ' ')  // skip space
          break;
        if (dir[i].DIR_Name[j] == DELETED) // deleted, always be dir[i].DIR_Name[0]
          filename[k++] = '?';
		    else
          filename[k++] = dir[i].DIR_Name[j];
    }
    /*
    if (dir[i].DIR_Attr == 0x0f) { // LFN
        filename[6] = '~';
        filename[7] = '1';
    }

    if (((dir[i].DIR_Attr >> 3) & 1) == 1)  // directory
        *filename++ = '/';
    */
	  if (dir[i].DIR_Name[8] != ' ') {
		    filename[k++] = '.';  // dot
		    for(j=8; j<=10; j++) { // extension part
			       if (dir[i].DIR_Name[j] == ' ')
              break;
             filename[k++] = dir[i].DIR_Name[j];
        }
    }
    if (dir[i].DIR_Attr & DIRECTORY)  //  bitwise AND // directory
  		filename[k++] = '/';

    filename[k] = '\0';

		printf("%d, %s, %d, %d\n", orderno++, filename, dir[i].DIR_FileSize, (dir[i].DIR_FstClusHI<<16)+dir[i].DIR_FstClusLO);
	}
}

void recover(char *target,char *dest) {  // will target be pathname ?????? YES
  int i; int j; int found = 0;
  //char o_target = target++;
  target++; // remove first character
  for (i=0; i<max_dir_count; i++) {
    char deletename[1025] = "";
    int k = 0;
    if (dir[i].DIR_Name[0] != DELETED) // skip non-deleted
      continue;
    for (j=1; j<8; j++) { // name part  // starting from second characters
      if (dir[i].DIR_Name[j] == ' ')  // skip space
        break;
      deletename[k++] = dir[i].DIR_Name[j];
    }
    if (dir[i].DIR_Name[8] != ' ') {
		    deletename[k++] = '.';  // dot
		    for(j=8; j<=10; j++) { // extension part
			       if (dir[i].DIR_Name[j] == ' ')
              break;
             deletename[k++] = dir[i].DIR_Name[j];
        }
    }
    deletename[k] = '\0';
    /* compare */
    if (strcmp(target,deletename) == 0) {  //  only one bingo
      //printf("bingo: %d\n", i);
      found = 1;
      if (dir[i].DIR_FileSize != 0) { // not empty file
        current = (dir[i].DIR_FstClusHI<<16)+dir[i].DIR_FstClusLO;  // find starting cluster
        if (fat[current] == 0) {  // can recover
          /* note that recoverd file is within only ONE cluster */
          pread(fd, tmpDE, cluster_size, data_offset + (current-2) * cluster_size);
          if ((fd = open(dest,O_WRONLY|O_CREAT|O_TRUNC,0600)) == -1) // note that fd is changed // 0600 allow permission
            printf("%s: failed to open\n", dest);   // return -1 if open fails
          else {
            write(fd, tmpDE, dir[i].DIR_FileSize);
            printf("%s: recovered\n", fullpath);
            }
          }
        else
          printf("%s: error - fail to recover\n", fullpath);  /* IMPORTANT!! deleted empty file will reach this!! */ /* Solved */
          break;
        }
      else {  // empty file // never "fail to recover"
        if (fopen(dest,"w") == NULL)  // just open, no need to write
          printf("%s: failed to open\n", dest); // return NULL if fopen fails
        else
          printf("%s: recovered\n", fullpath);
      }
    }
  }
  if (!found)
    printf("%s: error - file not found\n", fullpath);
}

int main(int argc, char **argv) {

    options(argc, argv);
    /* DEBUG */
    /*
    printf("device: %s\n", device);
    printf("list target: %s\n", ltarget);
    printf("recover target: %s\n", rtarget);
    printf("dest: %s\n", dest);
    */
    //printf("fullpath: %s\n", fullpath);

    /* open device file */
    fd = open(device,O_RDONLY);
    /* read boot entry */
    size_t boot_size = sizeof(struct BootEntry);
  	boot = malloc(boot_size);
    read(fd,boot,boot_size);
    /* DUBUG */
    /*
    printf("Bytes per sector: %d\n", boot->BPB_BytsPerSec);
    printf("Sectors per cluster: %d\n", boot->BPB_SecPerClus);
    printf("Number of reserved sector: %d\n", boot->BPB_RsvdSecCnt);
    printf("Number of FATs: %d\n", boot->BPB_NumFATs);
    printf("Number of sector(16bits): %d\n", boot->BPB_TotSec16);
    printf("Number of sector(32bits): %d\n", boot->BPB_TotSec32);
    printf("Number of sector of one FAT(32 bits): %d\n", boot->BPB_FATSz32);
    printf("Cluster of root directory: %d\n", boot->BPB_RootClus);
    */
    /* basic */
    byte_per_sector = boot->BPB_BytsPerSec;
    sector_per_cluster = boot->BPB_SecPerClus;
    /* sector */
    total_sector = boot->BPB_TotSec32;
    reserved_sector = boot->BPB_RsvdSecCnt;
    fat_sector = boot->BPB_NumFATs * boot->BPB_FATSz32;
    data_sector = total_sector - (reserved_sector + fat_sector);
    /* cluster */
    cluster_size = sector_per_cluster * byte_per_sector; // size of one Cluster
  	cluster_count = data_sector / sector_per_cluster; // no. of cluster
    /* offset */
    fat_offset = reserved_sector * byte_per_sector;
  	data_offset = (reserved_sector + fat_sector) * byte_per_sector;
    /* read FAT */
    fat_size = boot->BPB_FATSz32 * byte_per_sector; // size of one FAT
    fat = malloc(fat_size);
    pread(fd, fat, fat_size, fat_offset);
    /* BEBUG */
    /*
    printf("total_sector: %d\n", total_sector);
    printf("reserved_sector: %d\n", reserved_sector);
    printf("fat_sector: %d\n", fat_sector);
    printf("data_sector: %d\n", data_sector);
    printf("cluster_size: %d\n", cluster_size);
    printf("cluster_count: %d\n", cluster_count);
    printf("fat_size: %d\n", fat_size);
    printf("fat_offset: %zd\n", fat_offset);
    printf("data_offset: %zd\n", data_offset);
    */
/*
    unsigned int i;
    for (i=0; i<fat_size; i++)
      printf("%d: %d\n", i, fat[i]);
*/
    /* data area is in terms of cluster */
    /* first cluster of data area is cluster number 2 */
    /* in FAT32, root directory is at the beginning of data area */
    /* 0xFFFFFFFF is EOC mark, indicates that this is the last cluster of the root directory */
    /* 0x0FFFFFF7 is bad cluster mark */
    /* From wiki */
    /*
    0x?0000000 (Free Cluster)
    0x?0000001 (Reserved Cluster)
    0x?0000002 - 0x?FFFFFEF (Used cluster; value points to next cluster)
    0x?FFFFFF0 - 0x?FFFFFF6 (Reserved values)
    0x?FFFFFF7 (Bad cluster)
    0x?FFFFFF8 - 0x?FFFFFFF (Last cluster in file)
    */
    /* From wiki */

    size_t dir_size = sizeof(struct DirEntry);
    //printf("dir_size: %zd\n", dir_size);
    dir_per_cluster = cluster_size / dir_size;
    //printf("dir_per_cluster: %d\n", dir_per_cluster);

    current = boot->BPB_RootClus; //  initialize current cluster

    //fullpath = target;
    /* strtok */ /* need for list and recover */
    char token[1025][1025];
    int tokencnt = 0;
    char *retval = strtok(target , "/");
    while (retval != NULL) {
      strcpy(token[tokencnt++], retval);
      retval = strtok(NULL, "/");
    }
    // DEBUG
    //int i;
    //for (i=0; i<tokencnt; i++)
      //printf("token%d: %s\n", i, token[i]);


    /* list */
    if (lflag) {
      int i;
      get_dir_entries();
      for (i=0; i<tokencnt; i++) {
        //printf("going down\n");
        go_down(token[i]);
        //printf("getting dir\n");
        get_dir_entries();
      }
      /* now current should be in right place */
      //printf("listing\n");
      list();
    }

    /* recover */
    if (rflag) {
      int i;
      get_dir_entries();
      for (i=0; i<tokencnt-1; i++) {
        //printf("going down\n");
        go_down(token[i]);
        //printf("getting dir\n");
        get_dir_entries();
      }
      /* now current should be in right place */
      //printf("recovering\n");
      //printf("target: %s\n", token[i]);
      recover(token[i],dest); // need extra fullpath for arguments? YES. Solved.
    }

    close(fd);
    return 0;
  }
