#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "crc16.h"

#define FULLNAME_LEN 1024

struct list_entry {
	struct list_entry *next;
	unsigned long size;
	char	name[FULLNAME_LEN];
};

/* List entry for more than two files of the same size
*
* For this case first calculate CRC of each file part,
* then the files is compared if CRC is equal.
*/
struct crc_entry {
	struct crc_entry *next;
	char *name;
	unsigned short crc;
	int fd;
	void *mm;
};

static int files_scanned_num, files_failed_num;

static void inc_files_scanned(void)
{
	files_scanned_num++;
}

static void inc_files_failed(void)
{
	files_failed_num++;
}

static int files_scanned(void)
{
	return files_scanned_num;
}

static int files_failed(void)
{
	return files_failed_num;
}

int file_size(char *name)
{
	struct stat info;

	if(stat(name,&info) != 0) {
		printf("Error calculate size of file %s\n", name);
		inc_files_failed();
		return 0;
	}
	return info.st_size;
}

int	scan_dir(char *dir_name, unsigned long min_size, int(*list_cb)(struct list_entry*, char*, unsigned long), struct list_entry *list)
{
	DIR * dir;
	struct dirent *entry;
	char full_name[FULLNAME_LEN];
	int size;
	
	dir = opendir(dir_name);

	if (dir == NULL) {
		printf("Error reading of directory %s\n", dir_name);
		return -1;
	}

	while (NULL != (entry = readdir(dir))) {
		if((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))
			continue;

		strcpy(full_name, dir_name);
		strcat(full_name, "/");
		strcat(full_name, entry->d_name);
		if(entry->d_type == 0x04) {
			if(scan_dir(full_name, min_size, list_cb, list)) {
				closedir(dir);
				return -3;		/* TODO: error code  */
			}
		} else {
			size = file_size(full_name);
			if (size >= min_size)
				if ((*list_cb)(list, full_name, size)) {
					closedir(dir);
					printf("Error list entry inserting\n");
					inc_files_failed(); 
					return -2;
				}
		}
	}
	closedir(dir);

	return 0;
}

/* Inserting record into file list arranged by size */
int insert_file_entry(struct list_entry *list, char *name, unsigned long size)
{
	struct list_entry *n; /* new entry */
	n = malloc(strlen(name) + sizeof(struct list_entry) - FULLNAME_LEN + 1);
	if(NULL == n)
		return 1;
	strcpy(&(n->name[0]), name);
	n->size = size;

	while(list != NULL){
		if (list->next != NULL && size >= list->size && size <= list->next->size) {
			n->next = list->next;
			list->next = n;
			break;
		}
		else if(list->next == NULL) {
			n->next = NULL;
			list->next = n;
			break;
		}
			list = list->next;
	}
	
	inc_files_scanned();
	return 0;
}

void clean_malloc(struct list_entry *list)
{
	struct list_entry *curr_ptr;

	while (list != NULL) {
		curr_ptr = list;
		list = list->next;
		free(curr_ptr);
	}
}

void unmap_and_close(struct crc_entry *list, int size)
{
	while (list != NULL) {
		if (NULL != list->mm)
			munmap(list->mm, size);
		if (0 != list->fd)
			close(list->fd);
		list = list->next;
	}
}

void show_list(struct list_entry *list)
{
	while(list != NULL) {
		printf("size=%lu %s\n",list->size, list->name);
		list = list->next;
	}
}

int is_files_equal(char *f1, char *f2, size_t size)
{
#define ERROR 0;
	int fd1, fd2, ret;
	void *fm1, *fm2;
	
	fd1 = open(f1, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd1 <= 0) {
		printf("Error opening file %s\n", f1);
		inc_files_failed();
		return ERROR;
	}
	fd2 = open(f2, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd2 <= 0) {
		printf("Error opening file %s\n", f2);
		close(fd1);
		inc_files_failed();
		return ERROR;
	}

	fm1 = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
	if(MAP_FAILED == fm1) {
		printf("Error mapping file %s\n", f1);
		close(fd1);
		close(fd2);
		inc_files_failed();
		return ERROR;
	}
		
	fm2 = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
	if(MAP_FAILED == fm2) {
		printf("Error mapping file %s\n", f2);
		close(fd1);
		close(fd2);
		munmap(fm1, size);
		inc_files_failed();
		return ERROR;
	}
	ret = memcmp(fm1, fm2, size);

	munmap(fm1, size);
	munmap(fm2, size);
	close(fd1);
	close(fd2);
	return !ret;
}

int crc_calc(struct crc_entry *crc, struct list_entry *list)
{
	void *fm;
	int fd, size;

	crc->fd = 0;
	crc->mm = NULL;

	fd = open(list->name, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd <= 1) {
		printf("Unable to open file: %s\n", list->name);
		return 1;
	}

	size = list->size > 512 ? 512 : list->size;
	fm = mmap(0, list->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(MAP_FAILED == fm) {
		printf("Unable to map file: %s\n", list->name);
		close(fd);
		return 1;
	}	

	crc->crc = crc16(0, fm, size);
	crc->fd = fd;
	crc->mm = fm;
	return 0;
}

int push_to_crc_list(struct crc_entry* *hash_list, struct list_entry *list)
{
	struct crc_entry *new_ent;
	new_ent = malloc(sizeof(struct crc_entry));
	if(NULL == new_ent) {
		printf("Memory allocation error\n");
		return 1;
	}
	new_ent->name = list->name;
	new_ent->next = *hash_list;
	if(crc_calc(new_ent, list)) {
		free(new_ent);
		return 0;  /*  return success  */
	}		
	*hash_list = new_ent;
	return 0;
}

void do_crc_comparisons(struct crc_entry* *hl, unsigned long size)
{
	int files_eq, display_first = 0;
	struct crc_entry *first = *hl, *list, *prev;

	while(NULL != first && NULL != first->next) {
		list = first->next;
		prev = first;
		while(list != NULL) {
			if(first->crc != list->crc) {
				list = list->next;
				prev = prev->next;
				continue;
			}

			files_eq = !memcmp(first->mm, list->mm, size);
			if(files_eq){
				if(!display_first) {
					printf("\n%lu\n%s\n",size, first->name);
					display_first = 1;
				}
				printf("%s\n", list->name);
/*				nxt:                */
				prev->next = list->next; /* ??????? error if list->next == NULL */
				munmap(list->mm, size);
				close(list->fd);
				free(list);
				list = prev->next;
			}else{
				list = list->next;
				prev = prev->next;
			}
		}
		
		display_first = 0;
		first = first->next;
	}
}

void file_comparisons(struct list_entry *l)
{
	struct list_entry *sf;	/* second_file */
	struct crc_entry **cl;	/* crc list */
	struct crc_entry *first;

	if (NULL != l->next)
		l = l->next; /* drop empty entry */
	else
		return;
	
	while(NULL != l->next) {
		if(l->size != l->next->size) {
			l = l->next;
			continue;
		}

		sf = l->next;
		if((NULL != sf  &&  l->size == sf->size) &&
			((NULL != sf->next &&  sf->next->size != sf->size) || NULL == sf->next)) {
				if(is_files_equal(l->name, sf->name, l->size))
					printf("\n%lu\n%s\n%s\n", l->size, l->name, sf->name);
		} else {
			/*  More than two files have the same size */
			first = malloc(sizeof(struct crc_entry));
			if(NULL == first) {
				printf("Memory allocation error\n");
				return;
			}
			first->name = l->name;
			first->next = NULL;
			crc_calc(first, l);
			cl = &first;
			while(NULL != sf && l->size == sf->size) {
				if(push_to_crc_list(cl, sf)) {
					printf("Insufficient of memory\n");
					unmap_and_close(*cl, l->size);
					clean_malloc((struct list_entry*) *cl);
					return;
				}
				sf = sf->next;
				l = l->next;
			}
			do_crc_comparisons(cl, l->size);

			unmap_and_close(*cl, l->size);
			clean_malloc((struct list_entry*) *cl);
		}
		l = l->next;
	}
}

int	file_dup_find(char *dir, unsigned long min_size)
{
	struct list_entry *list;
	int ret;
	list = malloc(sizeof(struct list_entry));
	list->next = NULL;
	list->size = 0;
	list->name[0] = 0;

	printf("Scanning files...\n");
	ret = scan_dir(dir, min_size, insert_file_entry, list);
	printf("\nStarting comparisons...\n");
	file_comparisons(list);
	printf("%d files scanned, %d file(s) failed.\n", files_scanned(), files_failed());
	clean_malloc(list);
	return ret;
}

int	main(int argc, char *argv[])
{
	int str_end;
	unsigned long min_size = 1;

	if (argc < 2 || argc > 3) {
		printf("Use: %s <Directory> [Minimal File size]\n", argv[0]);
		return 0;
	}

	if (argc > 2) {
		min_size = atoi(argv[2]);
		if (min_size < 1)
			min_size = 1;
	}
	str_end = strlen(argv[1]) - 1;
	if (argv[1][str_end] == '/')
		argv[1][str_end] = 0;

	return file_dup_find(argv[1], min_size);
}

