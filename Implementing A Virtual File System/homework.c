/*
 * file:        homework.c
 * description: skeleton file for CS 5600/7600 file system
 *
 * CS 5600, Computer Systems, Northeastern CCIS
 * Peter Desnoyers, November 2016
 * Philip Gust, March 2019
 */

#define FUSE_USE_VERSION 27

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <limits.h>
#include <fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/select.h>

#include "fsx600.h"
#include "blkdev.h"


//extern int homework_part;       /* set by '-part n' command-line option */

/*
 * disk access - the global variable 'disk' points to a blkdev
 * structure which has been initialized to access the image file.
 *
 * NOTE - blkdev access is in terms of 1024-byte blocks
 */
extern struct blkdev *disk;

/* by defining bitmaps as 'fd_set' pointers, you can use existing
 * macros to handle them.
 *   FD_ISSET(##, inode_map);
 *   FD_CLR(##, block_map);
 *   FD_SET(##, block_map);
 */

/** pointer to inode bitmap to determine free inodes */
static fd_set *inode_map;
static int     inode_map_base;

/** pointer to inode blocks */
static struct fs_inode *inodes;
/** number of inodes from superblock */
static int   n_inodes;
/** number of first inode block */
static int   inode_base;

/** pointer to block bitmap to determine free blocks */
fd_set *block_map;
/** number of first data block */
static int     block_map_base;

/** number of available blocks from superblock */
static int   n_blocks;

/** number of root inode from superblock */
static int   root_inode;

/** array of dirty metadata blocks to write  -- optional */
static void **dirty;

/** length of dirty array -- optional */
static int    dirty_len;

/** the number of direct/indirect pointers per block (4 is the byte size of uint32) */
static int ptr_per_block = FS_BLOCK_SIZE/4;

/** add a file info struct */
struct file_info {
	uint32_t isDir;
	uint32_t inode;
};

//forward declaration
static int find_in_dir(struct fs_dirent *de, char *name);


/**
 *  free an array
 *
 * @param arr the array to free
 * @param count length of the array
 *
 */
static void free_arr(char* arr[], int count){
	for(int i = 0; i < count; i++){
		free(arr[i]);
		arr[i] = NULL;
	}
}

/* Suggested functions to implement -- you are free to ignore these
 * and implement your own instead
 */
/**
 * Look up a single directory entry in a directory.
 *
 * Errors
 *   -EIO     - error reading block
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - intermediate component of path not a directory
 *
 */
static int lookup(int inum, char *name)
{
	//check if inum is within range of n_inodes or inode is 0
	if(inum > n_inodes || inum == 0){
		return -1;
	}
	struct fs_inode *node = &inodes[inum];
	//check if it is a directory
	if (!S_ISDIR(node->mode)){
		return -ENOTDIR;
	}
	//read dirents
	struct fs_dirent* buffer = calloc(1, FS_BLOCK_SIZE);
	if (disk->ops->read(disk, node->direct[0], 1, buffer) < 0){
		return -EIO;
	}
	//find the name
	int dir_inode = find_in_dir(buffer, name);
	if (dir_inode == 0){
		return -1;
	}
	free(buffer);
    return dir_inode;
}

/**
 * Parse path name into tokens at most nnames tokens after
 * normalizing paths by removing '.' and '..' elements.
 *
 * If names is NULL,path is not altered and function  returns
 * the path count. Otherwise, path is altered by strtok() and
 * function returns names in the names array, which point to
 * elements of path string.
 *
 * @param path the directory path
 * @param names the argument token array or NULL
 * @param nnames the maximum number of names, 0 = unlimited
 * @return the number of path name tokens
 */
static int parse(char *path, char *names[], int nnames)
{
	if (path == NULL){
		return 0;
	}
	//make a copy of path
	char *_path = strdup(path);

	char *token;
	token = strtok(_path, "/");
	int count = 0;
	while(token != NULL){
		//go back to parent dir
		if (strcmp(token,"..") == 0){
			if (count > 0){
				count--;
			}
		}else if(strcmp(token,".") == 0){
			//ignore
		}else{
			if (names != NULL){
				names[count] = strdup(token);
			}
			count++;
		}
		//next token
		token = strtok(NULL, "/");
	}

	//if nnames is not unlimited and current count already exceed nnames
	if (nnames != 0 && count > nnames){
		//free the names array
		free_arr(names, count);
		return -1;
	}

	free(_path);
	return count;
}

/* Return inode number for specified file or
 * directory.
 *
 * Errors
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - an intermediate component of path not a directory
 *
 * @param path the file path
 * @return inode of path node or error
 */
static int translate(char *path)
{
	char* _path = strdup(path);
//	//if path is only "/", simply return root_inode
//	if(strcmp(_path, "/") == 0){
//		return root_inode;
//	}
	//start at root inode
	int idx = root_inode;
	//initilize the buffer to store path
	char* names[DIRENTS_PER_BLK];
	//get the number of entries
	int count = parse(_path, names, 0);
	// count-1: check if all parent entries are directories
	for (int i = 0; i < count; i++){
		//check if is directory
		if (!S_ISDIR(inodes[idx].mode)){
			free_arr(names, count);
			return -ENOTDIR;
		}
		//update inode number
		idx = lookup(idx, names[i]);
//		printf("file name: %s, %d\n", names[i], idx);
		if(idx < 0){
			free_arr(names, count);
			return -1;
		}
	}
	free_arr(names, count);
	free(_path);
    return idx;
}

/**
 *  Return inode number for path to specified file
 *  or directory, and a leaf name that may not yet
 *  exist.
 *
 * Errors
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - an intermediate component of path not a directory
 *
 * @param path the file path
 * @param leaf pointer to space for FS_FILENAME_SIZE leaf name
 * @return inode of path node or error
 */
static int translate_1(char *path, char *leaf)
{
	char* _path = strdup(path);
	int idx = root_inode;
	//initialize the buffer to store path
	char* names[DIRENTS_PER_BLK];
	int count = parse(_path, names, 0);
	// count-1: check all entries before the last one, last one could be file or directory
	for (int i = 0; i < count-1; i++){
		//check if is directory
		if (!S_ISDIR(inodes[idx].mode)){
			free_arr(names,count);
			return -ENOTDIR;
		}
		idx = lookup(idx, names[i]);
		if(idx < 0){
			free_arr(names, count);
			return -1;
		}
	}
	//check if the one before the last one is a directory
	if (!S_ISDIR(inodes[idx].mode)){
		return -ENOTDIR;
	}
	strcpy(leaf, names[count-1]);
	free_arr(names, count);
	free(_path);

    return idx;
}

/**
 * Mark a inode as dirty.
 *
 * @param in pointer to an inode
 */
static void mark_inode(struct fs_inode *in)
{
    int inum = in - inodes;
    int blk = inum / INODES_PER_BLK;
    dirty[inode_base + blk] = (void*)inodes + blk * FS_BLOCK_SIZE;
}

/**
 * Flush dirty metadata blocks to disk.
 */
void flush_metadata(void)
{
    int i;
    for (i = 0; i < dirty_len; i++) {
        if (dirty[i]) {
            disk->ops->write(disk, i, 1, dirty[i]);
            dirty[i] = NULL;
        }
    }
}

/**
 * Returns a free block number or 0 if none available.
 *
 * @return free block number or 0 if none available
 */
static int get_free_blk(void)
{
	int block_map_bytes = (inode_base-block_map_base)*FS_BLOCK_SIZE;
	for(int i = 1; i < block_map_bytes; i++){
		if(!FD_ISSET(i, block_map)){
			FD_SET(i, block_map);
			return i;
		}
	}
//	printf("block_map: %d\n", block_map_bytes);
	return 0;
}

//static void print_blk(void)
//{
//	int block_map_bytes = (inode_base-block_map_base)*FS_BLOCK_SIZE;
//	for(int i = 0; i < block_map_bytes; i++){
////		printf("idx: %d\n", i);
//		if(!FD_ISSET(i, block_map)){
//			printf("%d",0);
//		}else{
//			printf("%d",1);
//		}
//	}
//	printf("\n");
//}

///**
// * Return a block to the free list
// *
// * @param  blkno the block number
// */
//static void return_blk(int blkno)
//{
//	if (FD_ISSET(blkno, block_map)) {
//		FD_CLR(blkno, block_map);
//	}
//}

/**
 * Returns a free inode number
 *
 * @return a free inode number or 0 if none available
 */
static int get_free_inode(void)
{
	int inode_map_bytes = (block_map_base-inode_map_base)*FS_BLOCK_SIZE;
	// starts from 2 after root which is 1.
	for (uint32_t inum = 2; inum < inode_map_bytes; inum++) {
		if (!FD_ISSET(inum, inode_map)){
			FD_SET(inum, inode_map);
			return inum;
		}
	}
	return 0;
}

///**
// * Return a inode to the free list.
// *
// * @param  inum the inode number
// */
//static void return_inode(int inum)
//{
//	if (FD_ISSET(inum, inode_map)) {
//		FD_CLR(inum, inode_map);
//	}
//}

/**
 * Find inode for existing directory entry.
 *
 * @param fs_dirent ptr to first dirent in directory
 * @param name the name of the directory entry
 * @return the entry inode, or 0 if not found.
 */
static int find_in_dir(struct fs_dirent *de, char *name)
{
	for (int i = 0; i < DIRENTS_PER_BLK; i++){
		if (de[i].valid && strcmp(de[i].name, name) == 0){
			return de[i].inode;
		}
	}
	return 0;
}

/**
 * Find free directory entry.
 *
 * @return index of directory free entry or -ENOSPC
 *   if no space for new entry in directory
 */
//static int find_free_dir(struct fs_dirent *de)
//{
//    return -ENOSPC;
//}

/**
 * Determines whether directory is empty.
 *
 * @param de ptr to first entry in directory
 * @return 1 if empty 0 if has entries
 */
static int is_empty_dir(struct fs_dirent *de)
{
	int empty = 1;
	for (int i = 0; i < DIRENTS_PER_BLK; i++){
			if(de[i].valid == 1){
				empty = 0;
				break;
			}
		}
	return empty;
}

/**
 * Returns the n-th block of the file, or allocates
 * it if it does not exist and alloc == 1.
 *
 * @param in the file inode
 * @param n the 0-based block index in file
 * @param alloc 1=allocate block if does not exist 0 = fail
 *   if does not exist
 * @return block number of the n-th block or 0 if available
 */
//static int get_blk(struct fs_inode *in, int n, int alloc)
//{
//	return 0;
//}

/* Fuse functions
 */

/**
 * init - this is called once by the FUSE framework at startup.
 *
 * This is a good place to read in the super-block and set up any
 * global variables you need. You don't need to worry about the
 * argument or the return value.
 *
 * @param conn fuse connection information - unused
 * @return unused - returns NULL
 */
void* fs_init(struct fuse_conn_info *conn)
{
	// read the superblock
    struct fs_super sb;
    if (disk->ops->read(disk, 0, 1, &sb) < 0) {
        exit(1);
    }

    root_inode = sb.root_inode;

    /* The inode map and block map are written directly to the disk after the superblock */

    // read inode map
    inode_map_base = 1;
    inode_map = malloc(sb.inode_map_sz * FS_BLOCK_SIZE);
    if (disk->ops->read(disk, inode_map_base, sb.inode_map_sz, inode_map) < 0) {
        exit(1);
    }

    // read block map
    block_map_base = inode_map_base + sb.inode_map_sz;
    block_map = malloc(sb.block_map_sz * FS_BLOCK_SIZE);
    if (disk->ops->read(disk, block_map_base, sb.block_map_sz, block_map) < 0) {
        exit(1);
    }

    /* The inode data is written to the next set of blocks */
    inode_base = block_map_base + sb.block_map_sz;
    n_inodes = sb.inode_region_sz * INODES_PER_BLK;
    inodes = malloc(sb.inode_region_sz * FS_BLOCK_SIZE);
    if (disk->ops->read(disk, inode_base, sb.inode_region_sz, inodes) < 0) {
        exit(1);
    }

    // number of blocks on device
    n_blocks = sb.num_blocks;

    // dirty metadata blocks
    dirty_len = inode_base + sb.inode_region_sz;
    dirty = calloc(dirty_len*sizeof(void*), 1);

    /* your code here */

    return NULL;
}

/* Note on path translation errors:
 * In addition to the method-specific errors listed below, almost
 * every method can return one of the following errors if it fails to
 * locate a file or directory corresponding to a specified path.
 *
 * ENOENT - a component of the path is not present.
 * ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *           /a/b/c) is not a directory
 */

/* note on splitting the 'path' variable:
 * the value passed in by the FUSE framework is declared as 'const',
 * which means you can't modify it. The standard mechanisms for
 * splitting strings in C (strtok, strsep) modify the string in place,
 * so you have to copy the string and then free the copy when you're
 * done. One way of doing this:
 *
 *    char *_path = strdup(path);
 *    int inum = translate(_path);
 *    free(_path);
 */


static void fill_stat(int inum, struct stat* sb){
	struct fs_inode* node = &inodes[inum];
    sb->st_gid = node->gid;
    sb->st_mode = node->mode;
    sb->st_ctime = node->ctime;
    sb->st_uid = node->uid;
    sb->st_mtime = node->mtime;
    sb->st_size = node->size;
    sb->st_nlink = 1;
    sb->st_ino = inum;
	if (!S_ISDIR(sb->st_mode)){
		sb->st_blocks = (sb->st_size - 1)/FS_BLOCK_SIZE+1;
	}else{
		sb->st_blocks = 0;
	}
}
/**
 * getattr - get file or directory attributes. For a description of
 * the fields in 'struct stat', see 'man lstat'.
 *
 * Note - fields not provided in CS5600fs are:
 *    st_nlink - always set to 1
 *    st_atime, st_ctime - set to same value as st_mtime
 *
 * Errors
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - an intermediate component of path not a directory
 *
 * @param path the file path
 * @param sb pointer to stat struct
 * @return 0 if successful, or -error number
 */
static int fs_getattr(const char *path, struct stat *sb)
{
	char* _path = strdup(path);
    int inode_num = translate(_path);
    if (inode_num < 0) {
    	return inode_num;
    }
    fill_stat(inode_num, sb);
    free(_path);
    return 0;
}

/**
 * readdir - get directory contents.
 *
 * For each entry in the directory, invoke the 'filler' function,
 * which is passed as a function pointer, as follows:
 *     filler(buf, <name>, <statbuf>, 0)
 * where <statbuf> is a struct stat, just like in getattr.
 *
 * Errors
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - an intermediate component of path not a directory
 *
 * @param path the directory path
 * @param ptr  filler buf pointer
 * @param filler filler function to call for each entry
 * @param offset the file offset -- unused
 * @param fi the fuse file information
 * @return 0 if successful, or -error number
 */
static int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	//ls CALLED THIS FUNC
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum < 0){
		return inum;
	}
	//get the inode
	struct fs_inode* node = &inodes[inum];
	//check if it is a directory
	if (!S_ISDIR(node->mode)){
		return -ENOTDIR;
	}
	//set a buffer, read dirents into buffer
	struct fs_dirent* buffer = calloc(1, FS_BLOCK_SIZE);
	if (disk->ops->read(disk, node->direct[0], 1, buffer) < 0){
		return -EIO;
	}
	struct stat* sb = calloc(1, sizeof(struct stat));
	for (int i = 0; i < DIRENTS_PER_BLK; i++){
		if(buffer[i].valid){
			fill_stat(buffer[i].inode, sb);
//			sb->st_gid = inodes[buffer[i].inode].gid;
//			sb->st_mode = inodes[buffer[i].inode].mode;
//			sb->st_ctime = inodes[buffer[i].inode].ctime;
//			sb->st_uid = inodes[buffer[i].inode].uid;
//			sb->st_mtime = inodes[buffer[i].inode].mtime;
//			sb->st_size = inodes[buffer[i].inode].size;
//			sb->st_nlink = 1;
//			sb->st_ino = buffer[i].inode;
//			if (!S_ISDIR(sb->st_mode)){
//				sb->st_blocks = (sb->st_size - 1)/FS_BLOCK_SIZE+1;
//			}else{
//				sb->st_blocks = 0;
//			}
			filler(ptr, buffer[i].name, sb, 0);
		}
	}
	free(sb);
	free(_path);
	free(buffer);
    return 0;
}

/**
 * open - open file directory.
 *
 * You can save information about the open directory in
 * fi->fh. If you allocate memory, free it in fs_releasedir.
 *
 * Errors
 *   -ENOENT  - a component of the path is not present.
 *   -ENOTDIR - an intermediate component of path not a directory
 *
 * @param path the file path
 * @param fi fuse file system information
 * @return 0 if successful, or -error number
 */
static int fs_opendir(const char *path, struct fuse_file_info *fi)
{
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum < 0){
		return inum;
	}
	if (!S_ISDIR(inodes[inum].mode)){
		return -ENOTDIR;
	}
	struct file_info* fd = calloc(1, sizeof(struct file_info));
	fd->inode = inum;
	fd->isDir = 1;
	fi->fh = (uint64_t)fd;
	free(_path);
    return 0;
}

/**
 * Release resources when directory is closed.
 * If you allocate memory in fs_opendir, free it here.
 *
 * @param path the directory path
 * @param fi fuse file system information
 * @return 0 if successful, or -error number
 */
static int fs_releasedir(const char *path, struct fuse_file_info *fi)
{
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum < 0){
		return inum;
	}
	if (!S_ISDIR(inodes[inum].mode)){
		return -ENOTDIR;
	}
	free((struct file_info*)(uint64_t)fi->fh);

	free(_path);
    return 0;
}

/**
 * new_inode - create a new inode with 0 size.
 *
 * @param mode the mode, indicating block or character-special file
 * @return the inode number or 0 if cannot find an available one.
 */
static int new_inode(mode_t mode){
    time_t ctime;
    time(&ctime);
	struct fs_inode new_node = {
			.uid = getuid(),
			.gid = getgid(),
			.mode = mode,
			.ctime = ctime,
			.mtime = ctime,
			.size = 0,
	};
	int inum = get_free_inode();
	inodes[inum] = new_node;
	mark_inode(&inodes[inum]);
	disk->ops->write(disk, inode_map_base, block_map_base-inode_map_base, inode_map);
	flush_metadata();
	return inum;
}

/**
 * mknod - create a new file with permissions (mode & 01777)
 * minor device numbers extracted from mode. Behavior undefined
 * when mode bits other than the low 9 bits are used.
 *
 * The access permissions of path are constrained by the
 * umask(2) of the parent process.
 *
 * Errors
 *   -ENOTDIR  - component of path not a directory
 *   -EEXIST   - file already exists
 *   -ENOSPC   - free inode not available
 *   -ENOSPC   - results in >32 entries in directory
 *
 * @param path the file path
 * @param mode the mode, indicating block or character-special file
 * @param dev the character or block I/O device specification
 * @return 0 if successful, or -error number
 */
static int fs_mknod(const char *path, mode_t mode, dev_t dev)
{
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum > 0){
		return -EEXIST;
	}

	char* file_name = calloc(1,FS_FILENAME_SIZE);
	inum = translate_1(_path, file_name);
	if (inum < 0){
		return inum;
	}

	struct fs_inode *node = &inodes[inum];
	//set a buffer, read dirents into buffer
	struct fs_dirent* buffer = calloc(1, FS_BLOCK_SIZE);
	if (disk->ops->read(disk, node->direct[0], 1, buffer) < 0){
		return -ENOTDIR;
	}
	mark_inode(node);

	for(int i = 0; i < DIRENTS_PER_BLK; i++){
		if (buffer[i].valid == 0){
			buffer[i].inode = new_inode(mode & 01777);
			buffer[i].isDir = 0;
			strcpy(buffer[i].name, file_name);
//			printf("%s: %d\n", file_name, buffer[i].inode);
			buffer[i].valid = 1;
			break;
		}
	}
	disk->ops->write(disk, node->direct[0], 1, buffer);
//	printf("write dirent %s\n", buffer->name);
	free(_path);
	free(buffer);
	flush_metadata();
    return 0;
}

/**
 *  mkdir - create a directory with the given mode. Behavior
 *  undefined when mode bits other than the low 9 bits are used.
 *
 * Errors
 *   -ENOTDIR  - component of path not a directory
 *   -EEXIST   - directory already exists
 *   -ENOSPC   - free inode not available
 *   -ENOSPC   - results in >32 entries in directory
 *
 * @param path path to file
 * @param mode the mode for the new directory
 * @return 0 if successful, or -error number
 */
static int fs_mkdir(const char *path, mode_t mode)
{
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum > 0){
		return -EEXIST;
	}

	char* dir_name = calloc(1, FS_FILENAME_SIZE);
	inum = translate_1(_path, dir_name);
	if (inum < 0){
		return inum;
	}

	struct fs_inode *node = &inodes[inum];
	//set a buffer, read dirents into buffer
	struct fs_dirent* buffer = calloc(1, FS_BLOCK_SIZE);
	if (disk->ops->read(disk, node->direct[0], 1, buffer) < 0){
		return -ENOTDIR;
	}
	mark_inode(node);

	int found = 0;
	for(int i = 0; i < DIRENTS_PER_BLK; i++){
		if (buffer[i].valid == 0){
			buffer[i].inode = new_inode((mode & 01755) | S_IFDIR);
//			inodes[buffer[i].inode].direct[0] = get_free_block();
			buffer[i].isDir = 1;
			strcpy(buffer[i].name, dir_name);
			buffer[i].valid = 1;
			found = 1;
			int dirents_blk = get_free_blk();
//			printf("blk num: %d\n", dirents_blk);
			if (dirents_blk == 0){
//				printf("blk num: %d\n", dirents_blk);
				return -ENOSPC;
			}else{
				inodes[buffer[i].inode].direct[0] = dirents_blk;
			}
			struct fs_dirent* des = calloc(1, FS_BLOCK_SIZE);
			struct fs_dirent de = {
					.valid = 0,
			};
			for(int j = 0; j < DIRENTS_PER_BLK; j++){
				des[j] = de;
			}
			disk->ops->write(disk, inodes[buffer[i].inode].direct[0], 1, des);
			free(des);
			break;
		}
	}
	if(found == 0){
		return -ENOSPC;
	}

	disk->ops->write(disk, node->direct[0], 1, buffer);
	free(_path);
	free(buffer);
	flush_metadata();
    return 0;
}

/**
 * truncate - truncate file to exactly 'len' bytes.
 *
 * Errors:
 *   ENOENT  - file does not exist
 *   ENOTDIR - component of path not a directory
 *   EINVAL  - length invalid (only supports 0)
 *   EISDIR	 - path is a directory (only files)
 *
 * @param path the file path
 * @param len the length
 * @return 0 if successful, or -error number
 */
static int fs_truncate(const char *path, off_t len)
{
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
//	printf("block map before truncate:\n");
//	print_blk();
    if (len != 0) {
    	return -EINVAL;		/* invalid argument */
    }
    char* _path = strdup(path);
    int inum = translate(_path);
    if(inum < 0){
    	return inum;
    }
    //get the inode of the path
    struct fs_inode* node = &inodes[inum];
    //should be a file
    if(S_ISDIR(node->mode)){
    	return -EISDIR;
    }
    mark_inode(node);

    struct fs_dirent* buffer = calloc(1, FS_BLOCK_SIZE);

    //1. clear the direct dirents
    for(int i = 0; i < N_DIRECT; i++){
    	if(node->direct[i]!= 0){
    		if (disk->ops->read(disk, node->direct[i], 1, buffer) < 0) exit(1);
    		memset(buffer, 0, FS_BLOCK_SIZE);
    		if(disk->ops->write(disk, node->direct[i], 1, buffer) < 0) exit(1);

    		FD_CLR(node->direct[i], block_map);
    		node->direct[i] = 0;
    	}
    }
    //2. clear level1 indirect dirents
    if(node->indir_1 != 0){
    	uint32_t blk_ptrs[256];
    	if(disk->ops->read(disk, node->indir_1, 1, blk_ptrs) < 0) {
    		exit(1);
    	}
    	for(int i = 0; i < 256; i++){
    		if(blk_ptrs[i] != 0){
    			memset(buffer, 0, FS_BLOCK_SIZE);
    			if (disk->ops->read(disk, blk_ptrs[i], 1, buffer) < 0) exit(1);
    			memset(buffer, 0, FS_BLOCK_SIZE);
    			if(disk->ops->write(disk, blk_ptrs[i], 1, buffer) < 0) exit(1);

				FD_CLR(blk_ptrs[i], block_map);
				blk_ptrs[i] = 0;
    		}
    	}
    	FD_CLR(node->indir_1, block_map);
    	if(disk->ops->write(disk, node->indir_1, 1, blk_ptrs) < 0) {
    		exit(1);
    	}
    	node->indir_1 = 0;
    }
    //3. clear level2 indirect dirents
    if(node->indir_2 != 0){
        	uint32_t level1_ptrs[256];
        	if(disk->ops->read(disk, node->indir_2, 1, level1_ptrs) < 0) {
        		exit(1);
        	}
        	for(int i = 0; i < 256; i++){
        		if(level1_ptrs[i] != 0){
        			uint32_t level2_ptrs[256];
        			if(disk->ops->read(disk, level1_ptrs[i], 1, level2_ptrs) < 0) {
        				exit(1);
        			}
        			for(int j = 0; j < 256; j++){
        				if(level2_ptrs[j] != 0){
						    memset(buffer, 0, FS_BLOCK_SIZE);
							if (disk->ops->read(disk, level2_ptrs[j], 1, buffer) < 0) exit(1);
							memset(buffer, 0, FS_BLOCK_SIZE);
							//clear the disk
							if(disk->ops->write(disk, level2_ptrs[j], 1, buffer) < 0) exit(1);

							FD_CLR(level2_ptrs[j], block_map);
							level2_ptrs[j] = 0;
        				}
        			}
        			if(disk->ops->write(disk, level1_ptrs[i], 1, level2_ptrs) < 0) {
        				exit(1);
        			}
        			FD_CLR(level1_ptrs[i], block_map);
        			level1_ptrs[i] = 0;
        		}
        	}
        	if(disk->ops->write(disk, node->indir_2, 1, level1_ptrs) < 0) {
        		exit(1);
        	}
        	FD_CLR(node->indir_2, block_map);
        	node->indir_2 = 0;
      }
	//set inode size
	node->size = 0;
	//update block map
	if(disk->ops->write(disk, block_map_base, 1, block_map) < 0) {
		exit(1);
	}
	flush_metadata();
	free(_path);
//	printf("block map after truncate:\n");
//	print_blk();
    return 0;
}

/**
 * unlink - delete a file.
 *
 * Errors
 *   -ENOENT   - file does not exist
 *   -ENOTDIR  - component of path not a directory
 *   -EISDIR   - cannot unlink a directory
 *
 * @param path path to file
 * @return 0 if successful, or -error number
 */
static int fs_unlink(const char *path)
{
	char* _path = strdup(path);
	int inum = translate(_path);
	if(inum < 0){
		return inum;
	}
	struct fs_inode* node = &inodes[inum];
	//if the given path is a directory
	if(S_ISDIR(node->mode)){
		return -EISDIR;
	}
	fs_truncate(_path, 0);
	//get the name
	char* leaf = calloc(1, sizeof(char*));
	int parent_inum = translate_1(_path, leaf);
	if(parent_inum < 0){
		return parent_inum;
	}
	//get the parent inode
	struct fs_inode* parent_node = &inodes[parent_inum];
	if(!S_ISDIR(parent_node->mode)){
		return -ENOTDIR;
	}
	//get the dirents of parent inode
	struct fs_dirent* dirents = calloc(1, FS_BLOCK_SIZE);
	if (disk->ops->read(disk, parent_node->direct[0], 1, dirents) < 0){
		return -EIO;
	}

	//search for the delete file with the name(leaf)
	for(int i = 0; i < DIRENTS_PER_BLK; i++){
		if(dirents[i].valid && strcmp(dirents[i].name, leaf) == 0){
			dirents[i].valid = 0;
			FD_CLR(inum, inode_map);
			break;
		}
	}
	//update dirents
	if (disk->ops->write(disk, parent_node->direct[0], 1, dirents) < 0){
		return -EIO;
	}
	//update inode_map
	if(disk->ops->write(disk, inode_map_base, block_map_base - inode_map_base, inode_map) < 0) {
		exit(1);
	}
	free(dirents);
	free(_path);
	free(leaf);
	return 0;
}


/**
 * rmdir - remove a directory.
 *
 * Errors
 *   -ENOENT   - file does not exist
 *   -ENOTDIR  - component of path not a directory
 *   -ENOTDIR  - path not a directory
 *   -ENOTEMPTY - directory not empty
 *
 * @param path the path of the directory
 * @return 0 if successful, or -error number
 */
static int fs_rmdir(const char *path)
{
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum < 0){
		return inum;
	}
	//get the inode
	struct fs_inode* node = &inodes[inum];

	//check if it is a directory
	if (!S_ISDIR(node->mode)){
		return -ENOTDIR;
	}

	//set a buffer, read dirents into buffer
	struct fs_dirent* buffer = calloc(1, FS_BLOCK_SIZE);
	if (disk->ops->read(disk, node->direct[0], 1, buffer) < 0){
		return -EIO;
	}
	if (is_empty_dir(buffer) == 0){
		return -ENOTEMPTY;
	}

	// clear the dir's dirents block
	memset(buffer, 0, FS_BLOCK_SIZE);
	if (disk->ops->write(disk, node->direct[0], 1, buffer) < 0){
		return -EIO;
	}
	FD_CLR(node->direct[0],block_map);
	node->direct[0] = 0;
	if (disk->ops->write(disk, block_map_base, 1, block_map) < 0){
		return -EIO;
	}

	char* leaf = malloc(sizeof(char*));
	int parent_inum = translate_1(_path, leaf);
	if(parent_inum < 0){
		return parent_inum;
	}
	//get the parent inode
	struct fs_inode* parent_node = &inodes[parent_inum];
	if(!S_ISDIR(parent_node->mode)){
		return -ENOTDIR;
	}
	//get the dirents of parent inode
	struct fs_dirent* dirents = calloc(1, FS_BLOCK_SIZE);
	if (disk->ops->read(disk, parent_node->direct[0], 1, dirents) < 0){
		return -EIO;
	}

	//search for the delete file with the name(leaf)
	for(int i = 0; i < DIRENTS_PER_BLK; i++){
		if(dirents[i].valid && strcmp(dirents[i].name, leaf) == 0){
			dirents[i].valid = 0;
			mark_inode(&inodes[inum]);
			FD_CLR(inum, inode_map);
			flush_metadata();
			break;
		}
	}
	//update dirents
	if (disk->ops->write(disk, parent_node->direct[0], 1, dirents) < 0){
		return -EIO;
	}
	//update inode_map
	if(disk->ops->write(disk, inode_map_base, block_map_base - inode_map_base, inode_map) < 0) {
		return -EIO;
	}
//	if(disk->ops->write(disk, block_map_base, 1, block_map) < 0) {
//		exit(1);
//	}
	free(dirents);
	free(buffer);
	free(leaf);
	free(_path);
	return 0;
}

/**
 * rename - rename a file or directory.
 *
 * Note that this is a simplified version of the UNIX rename
 * functionality - see 'man 2 rename' for full semantics. In
 * particular, the full version can move across directories, replace a
 * destination file, and replace an empty directory with a full one.
 *
 * Errors:
 *   -ENOENT   - source file or directory does not exist
 *   -ENOTDIR  - component of source or target path not a directory
 *   -EEXIST   - destination already exists
 *   -EINVAL   - source and destination not in the same directory
 *
 * @param src_path the source path
 * @param dst_path the destination path.
 * @return 0 if successful, or -error number
 */
static int fs_rename(const char *src_path, const char *dst_path)
{
	char* s_path = strdup(src_path);
	int s_inum = translate(s_path);
	if (s_inum < 0){
		return s_inum;
	}
	char* d_path = strdup(dst_path);
	//find source path parent
	char* s_leaf = malloc(sizeof(char*));
	int s_parent_inum = translate_1(s_path, s_leaf);
	if(s_parent_inum < 0){
		return s_parent_inum;
	}
	//find dest path parent
	char* d_leaf = malloc(sizeof(char*));
	int d_parent_inum = translate_1(d_path, d_leaf);
	if(d_parent_inum < 0){
		return d_parent_inum;
	}
	//check if in the same directory
	if(s_parent_inum != d_parent_inum){
		return -EINVAL;
	}

	struct fs_dirent* dirents = calloc(1, FS_BLOCK_SIZE);
	if (disk->ops->read(disk, inodes[s_parent_inum].direct[0], 1, dirents) < 0){
		return -EIO;
	}
//	printf("parent inum: %d, s_inum: %d\n", s_parent_inum, s_inum);
	for(int i = 0; i < DIRENTS_PER_BLK; i++){
		if(dirents[i].valid && strcmp(dirents[i].name, d_leaf) == 0){
			return -EEXIST;
		}
	}

	for(int i = 0; i < DIRENTS_PER_BLK; i++){
		if(dirents[i].valid && strcmp(dirents[i].name, s_leaf) == 0){
			int j = 0;
			for(; j < strlen(d_leaf); j++){
				dirents[i].name[j] = d_leaf[j];
			}
			for(int k = j; k < FS_FILENAME_SIZE; k++){
				char c = '\0';
				dirents[i].name[k] = c;
			}
		}
	}
	//update dirents
	if (disk->ops->write(disk, inodes[s_parent_inum].direct[0], 1, dirents) < 0){
		return -EIO;
	}
	free(dirents);
	free(s_leaf);
	free(d_leaf);
	free(s_path);
	free(d_path);
    return 0;
}

/**
 * chmod - change file permissions
 *
 * Errors:
 *   -ENOENT   - file does not exist
 *   -ENOTDIR  - component of path not a directory
 *
 * @param path the file or directory path
 * @param mode the mode_t mode value -- see man 'chmod'
 *   for description
 * @return 0 if successful, or -error number
 */
static int fs_chmod(const char *path, mode_t mode)
{
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum < 0){
		return inum;
	}
	mark_inode(&inodes[inum]);
	if (S_ISDIR(inodes[inum].mode)){
		inodes[inum].mode = mode | S_IFDIR;
	}else{
		inodes[inum].mode = mode;
	}
	flush_metadata();
	free(_path);
	return 0;
}

/**
 * utime - change access and modification times.
 *
 * Errors:
 *   -ENOENT   - file does not exist
 *   -ENOTDIR  - component of path not a directory
 *
 * @param path the file or directory path.
 * @param ut utimbuf - see man 'utime' for description.
 * @return 0 if successful, or -error number
 */
int fs_utime(const char *path, struct utimbuf *ut)
{
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum < 0){
		return inum;
	}
	struct fs_inode* node = &inodes[inum];
	mark_inode(node);
	node->mtime = ut->modtime;
	flush_metadata();
	free(_path);
    return 0;
}

static void read_helper(struct fs_inode* node, int offset, int block_num, int block_offset, char* tmp)
{
	if(offset >= node->size){
		return;
	}
	if(block_num < 6){
		disk->ops->read(disk, node->direct[block_num], 1, tmp);
	}else if (block_num >= 6 && block_num < ptr_per_block + 6){//from indir1 base to end of indir1
		//set block_num to start of indir1 (index in indir1)
		block_num -= 6;
		uint32_t blk_ptrs[256];
		disk->ops->read(disk, node->indir_1, 1, blk_ptrs);
		disk->ops->read(disk, blk_ptrs[block_num], 1, tmp);
	}else if (block_num >= ptr_per_block + 6 && block_num < ptr_per_block*ptr_per_block + ptr_per_block + 6){// from indir2 base to end of indir2
		//set block_num to start of indir2 (index in indir2)
		block_num -= (6+ ptr_per_block);
		uint32_t level1_block_num = block_num / ptr_per_block;
		uint32_t level2_block_num = block_num % ptr_per_block;

		uint32_t level1_ptrs[256];
		disk->ops->read(disk, node->indir_2, 1, level1_ptrs);

		uint32_t level2_ptrs[256];
		disk->ops->read(disk, level1_ptrs[level1_block_num], 1, level2_ptrs);
		disk->ops->read(disk, level2_ptrs[level2_block_num], 1, tmp);
	}
}

/**
 *  write - write data to a file
 *
 * It should return exactly the number of bytes requested, except on
 * error.
 *
 * Errors:
 *   -ENOENT  - file does not exist
 *   -ENOTDIR - component of path not a directory
 *   -EISDIR  - file is a directory
 *   -EINVAL  - if 'offset' is greater than current file length.
 *  			(POSIX semantics support the creation of files with
 *  			"holes" in them, but we don't)
 *
 * @param path the file path
 * @param buf the buffer to write
 * @param len the number of bytes to write
 * @param offset the offset to starting writing at
 * @param fi the Fuse file info for writing
 * @return 0 if successful or -error number
 *
 */

static int write_helper(struct fs_inode* node, int block_num, char* buf2){
	//direct pointers
	if(block_num < 6){
		if(node->direct[block_num] == 0){
			node->direct[block_num] = get_free_blk();
//			printf("writing to direct block %d\n",node->direct[block_num]);
			if (node->direct[block_num] == 0) {
				return -ENOSPC;
			}
		}
//		printf("writing to free block %d\n", node->direct[block_num]);
		if(disk->ops->write(disk, node->direct[block_num], 1, buf2) < 0){
			return -EIO;
		}
	} else if (block_num >= 6 && block_num < ptr_per_block + 6){//from indir1 base to end of indir1
		//set block_num to start of indir1 (index in indir1)
		block_num -= 6;
		uint32_t blk_ptrs[256];
		int indir_1_blk = node->indir_1;
		if (indir_1_blk == 0){
			node->indir_1 = get_free_blk();
//			printf("indir_1 block %d\n", node->indir_1);
			if (node->indir_1 == 0) {
				return -ENOSPC;
			}
			indir_1_blk = node->indir_1;
//			printf("assigning first indirect pointer %d\n", node->indir_1);

		}

		if( disk->ops->read(disk, node->indir_1, 1, blk_ptrs)< 0){
			return -EIO;
		}

		if (blk_ptrs[block_num] == 0){
			blk_ptrs[block_num] = get_free_blk();
//			printf("blk_ptrs[block_num] block %d\n", blk_ptrs[block_num]);
			if (blk_ptrs[block_num] == 0) {
				return -ENOSPC;
			}
		}
		//update the indirect block on disk
		if(disk->ops->write(disk, indir_1_blk, 1, blk_ptrs) < 0){
			return -EIO;
		}
//		printf("writing to free indirect block %d\n", blk_ptrs[block_num]);
		if(disk->ops->write(disk, blk_ptrs[block_num], 1, buf2) < 0){
			return -EIO;
		}
	} else if (block_num >= ptr_per_block + 6 && block_num < ptr_per_block*ptr_per_block + ptr_per_block + 6){// from indir2 base to end of indir2
		// from indir2 base to end of indir2
		//set block_num to start of indir2 (index in indir2)
		block_num -= (6+ ptr_per_block);
		uint32_t level1_block_num = block_num / ptr_per_block;
		uint32_t level2_block_num = block_num % ptr_per_block;

		int indir_l1_blk = node->indir_2;
		if (indir_l1_blk == 0){
			node->indir_2 = get_free_blk();
			if (node->indir_2 == 0) {
				return -ENOSPC;
			}
			indir_l1_blk = node->indir_2;
//			printf("assigning first indirect pointer %d\n", node->indir_2);

		}
		uint32_t level1_ptrs[256];
		if(disk->ops->read(disk, node->indir_2, 1, level1_ptrs)<0){
			return -EIO;
		}
		if (level1_ptrs[level1_block_num] == 0){
			level1_ptrs[level1_block_num] = get_free_blk();
			if (level1_ptrs[level1_block_num] == 0) {
				return -ENOSPC;
			}
			if(disk->ops->write(disk, node->indir_2, 1, level1_ptrs) < 0){
				return -ENOSPC;
			}
		}

		uint32_t level2_ptrs[256];
		if(disk->ops->read(disk, level1_ptrs[level1_block_num], 1, level2_ptrs)<0){
			exit(1);
		}
		if (level2_ptrs[level2_block_num] == 0){
			level2_ptrs[level2_block_num] = get_free_blk();
			if (level2_ptrs[level2_block_num] == 0) {
				return -ENOSPC;
			}
			if(disk->ops->write(disk, level1_ptrs[level1_block_num], 1, level2_ptrs) < 0){
				return -ENOSPC;
			}
		}
//		printf("writing to direct block %d\n",level2_ptrs[level2_block_num]);
		if(disk->ops->write(disk, level2_ptrs[level2_block_num], 1, buf2) < 0){
			return -ENOSPC;
		}
	}
	return 0;
}

/**
 * read - read data from an open file.
 *
 * Should return exactly the number of bytes requested, except:
 *   - if offset >= file len, return 0
 *   - if offset+len > file len, return bytes from offset to EOF
 *   - on error, return <0
 *
 * Errors:
 *   -ENOENT  - file does not exist
 *   -ENOTDIR - component of path not a directory
 *   -EISDIR  - file is a directory
 *   -EIO     - error reading block
 *
 * @param path the path to the file
 * @param buf the read buffer
 * @param len the number of bytes to read
 * @param offset to start reading at
 * @param fi fuse file info
 * @return number of bytes actually read if successful, or -error number
 */
static int fs_read(const char *path, char *buf, size_t len, off_t offset,
		    struct fuse_file_info *fi)
{
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum < 0){
		return inum;
	}
	struct fs_inode* node = &inodes[inum];
	//must be file
	if(S_ISDIR(node->mode)){
		return -EISDIR;
	}
	//if offset >= file len, return 0
	if(offset >= node->size){
		return 0;
	}
	if (node->size < len){
		len = node->size;
	}
	int block_num = offset/FS_BLOCK_SIZE;
	int block_offset = offset % FS_BLOCK_SIZE;
	char* buf_ptr = buf;
	int count = 0;

	char* tmp = (char*)calloc(1, FS_BLOCK_SIZE);
	while(len > 0 && offset < node->size){
		read_helper(node, offset, block_num, block_offset, tmp);
		size_t read_len = FS_BLOCK_SIZE-block_offset;
		if (offset+read_len > node->size){
			read_len = node->size-offset;
		}
		if (read_len > len){
			read_len = len;
		}
		char* tmp_ptr = tmp+block_offset;
		memcpy(buf_ptr, tmp_ptr, read_len);
		buf_ptr+= read_len;
		offset += read_len;
		count+= read_len;
		len-= read_len;
		block_num = offset/FS_BLOCK_SIZE;
		block_offset = offset % FS_BLOCK_SIZE;
	}
	free(tmp);
	free(_path);
	return count;
}

/**
 *  write - write data to a file
 *
 * It should return exactly the number of bytes requested, except on
 * error.
 *
 * Errors:
 *   -ENOENT  - file does not exist
 *   -ENOTDIR - component of path not a directory
 *   -EISDIR  - file is a directory
 *   -EINVAL  - if 'offset' is greater than current file length.
 *  			(POSIX semantics support the creation of files with
 *  			"holes" in them, but we don't)
 *
 * @param path the file path
 * @param buf the buffer to write
 * @param len the number of bytes to write
 * @param offset the offset to starting writing at
 * @param fi the Fuse file info for writing
 * @return number of bytes actually written if successful, or -error number
 *
 */
static int fs_write(const char *path, const char *buf, size_t len,
		     off_t offset, struct fuse_file_info *fi)
{
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum < 0){
		return inum;
	}
	//get the inode
	struct fs_inode* node = &inodes[inum];
	//if not file
	if(S_ISDIR(node->mode)){
		return -EISDIR;
	}
	mark_inode(node);

	int block_num = offset/FS_BLOCK_SIZE;
	int block_offset = offset % FS_BLOCK_SIZE;
	char *buf2 = strdup(buf);
	char* buf2p = buf2;
	int rlen = len;
	char* tmp = calloc(1, FS_BLOCK_SIZE);
	while(rlen > 0){
		read_helper(node, offset, block_num, block_offset, tmp);
		char* tmp_p = tmp;
		tmp_p += block_offset;
		int write_len = FS_BLOCK_SIZE - block_offset;
		if (rlen < write_len){
			write_len = rlen;
		}
		memcpy(tmp_p, buf2p, write_len);
		int error = write_helper(node, block_num, buf2p);
		if (error < 0){
			return error;
		}
		buf2p += write_len;
		node->size += write_len;
		rlen -= write_len;
		offset += write_len;
		block_num = offset/FS_BLOCK_SIZE;
		block_offset = offset % FS_BLOCK_SIZE;
	}
	if(disk->ops->write(disk, block_map_base, 1, block_map) < 0) {
		return -EIO;
	}
//	printf("%d written\n", len);
	flush_metadata();
	free(_path);
	free(tmp);

    return len;
}

/**
 * Open a filesystem file or directory path.
 *
 * Errors:
 *   -ENOENT  - file does not exist
 *   -ENOTDIR - component of path not a directory
 *   -EISDIR  - file is a directory
 *
 * @param path the path
 * @param fuse file info data
 * @return 0 if successful, or -error number
 */
static int fs_open(const char *path, struct fuse_file_info *fi)
{
	//PUT, GET, SHOW command called this function
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum < 0){
		return inum;
	}
	if (S_ISDIR(inodes[inum].mode)){
		return fs_opendir(_path, fi);
	}
	struct file_info* fd = calloc(1, sizeof(struct file_info));
	fd->inode = inum;
	fd->isDir = 0;
	fi->fh = (uint64_t)fd;
	free(_path);
    return 0;
}

/**
 * Release resources created by pending open call.
 *
 * Errors:
 *   -ENOENT  - file does not exist
 *   -ENOTDIR - component of path not a directory
 *
 * @param path the file name
 * @param fi the fuse file info
 * @return 0 if successful, or -error number
 */
static int fs_release(const char *path, struct fuse_file_info *fi)
{
	//PUT, GET, SHOW command called this function
	char* _path = strdup(path);
	int inum = translate(_path);
	if (inum < 0){
		return inum;
	}
	if (S_ISDIR(inodes[inum].mode)){
		return fs_releasedir(_path, fi);
	}
	free((struct file_info*)(uint64_t)fi->fh);

	free(_path);
	return 0;
}

/**
 * statfs - get file system statistics.
 * See 'man 2 statfs' for description of 'struct statvfs'.
 *
 * Errors
 *   none -  Needs to work
 *
 * @param path the path to the file
 * @param st the statvfs struct
 * @return 0 for successful
 */
static int fs_statfs(const char *path, struct statvfs *st)
{
    /* needs to return the following fields (set others to zero):
     *   f_bsize = BLOCK_SIZE
     *   f_blocks = total image - metadata
     *   f_bfree = f_blocks - blocks used
     *   f_bavail = f_bfree
     *   f_namelen = <whatever your max namelength is>
     *
     * this should work fine, but you may want to add code to
     * calculate the correct values later.
     */
    st->f_bsize = FS_BLOCK_SIZE;
    st->f_blocks = 0;           /* probably want to */
    st->f_bfree = 0;            /* change these */
    st->f_bavail = 0;           /* values */
    st->f_namemax = FS_FILENAME_SIZE - 1;

    return 0;
}

/**
 * Operations vector. Please don't rename it, as the
 * skeleton code in misc.c assumes it is named 'fs_ops'.
 */
struct fuse_operations fs_ops = {
    .init = fs_init,
    .getattr = fs_getattr,
    .opendir = fs_opendir,
    .readdir = fs_readdir,
    .releasedir = fs_releasedir,
    .mknod = fs_mknod,
    .mkdir = fs_mkdir,
    .unlink = fs_unlink,
    .rmdir = fs_rmdir,
    .rename = fs_rename,
    .chmod = fs_chmod,
    .utime = fs_utime,
    .truncate = fs_truncate,
    .open = fs_open,
    .read = fs_read,
    .write = fs_write,
    .release = fs_release,
    .statfs = fs_statfs,
};


