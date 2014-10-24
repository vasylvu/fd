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

#define ERR_NOMEM		-1
#define ERR_READ_DIR	-4
#define ERR_MEM_MAP		-5
#define ERR_OPEN_FILE	-6


struct list_entry {
	struct list_entry *next;
	struct list_entry *next_down;	/* List entry for more than two files of the same size	*/
	unsigned long size;
	int fd;
	void *mm;
	unsigned short crc;
	char name[FULLNAME_LEN];
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
	int size, ret;
	
	dir = opendir(dir_name);

	if (dir == NULL) {
		printf("Error reading of directory %s\n", dir_name);
		return ERR_READ_DIR;
	}

	while (NULL != (entry = readdir(dir))) {
		if((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))
			continue;

		strcpy(full_name, dir_name);
		strcat(full_name, "/");
		strcat(full_name, entry->d_name);
		if(entry->d_type == DT_DIR) {
			if(ret = scan_dir(full_name, min_size, list_cb, list)) {
				closedir(dir);
				return ret;
			}
		} else if(entry->d_type == DT_REG) {
			size = file_size(full_name);
			if (size >= min_size)
				if ((*list_cb)(list, full_name, size)) {
					closedir(dir);
					inc_files_failed(); 
					return ERR_NOMEM;
				}
		}
	}
	closedir(dir);
	return 0;
}

/* Inserting record into the file list arranged by size */
int insert_file_entry(struct list_entry *list, char *name, unsigned long size)
{
	struct list_entry *n; /* new entry */
	n = malloc(strlen(name) + sizeof(struct list_entry) - FULLNAME_LEN + 1);
	if(NULL == n) {
		printf("Insufficient of memory\n");
		return ERR_NOMEM;
	}
	strcpy(n->name, name);
	n->size = size;

	for(; list != NULL; list = list->next){
		if (list->next != NULL) {
			if (size < list->size) {		// TODO: check if need
				printf("-- find next size %lu\n", size);
				continue;
			}
			else if (size > list->size && size < list->next->size) {
				n->next = list->next;
				n->next_down = NULL;
				list->next = n;
				break;
			}
			else if (size == list->size) {
				n->next = NULL;
				n->next_down = list->next_down;
				list->next_down = n;
				break;
			}
		}
		else {
			n->next = NULL;
			n->next_down = NULL;
			list->next = n;
			break;
		}
	}
	inc_files_scanned();
	return 0;
}

void clean_malloc(struct list_entry *list)
{
	struct list_entry *tmp, *tmpd;

	while (list != NULL) {
		tmpd = list->next_down;
		while(NULL != tmpd) {			
			tmp = tmpd;
			tmpd = tmpd->next_down;
			free(tmp);
		}
		tmp = list;
		list = list->next;
		free(tmp);
	}
}

void unmap_and_close(struct list_entry *list)
{
	for (; list != NULL; list = list->next_down) {
		if (NULL != list->mm)
			munmap(list->mm, list->size);
		if (0 != list->fd)
			close(list->fd);
	}
}

int is_files_equal(char *f1, char *f2, size_t size)
{
#define ERROR 0;
	int fd1, fd2, ret;
	void *fm1, *fm2;
	
	fd1 = open(f1, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd1 <= 0) {
		printf("Unable to open file: %s\n", f1);
		inc_files_failed();
		return ERROR;
	}
	fd2 = open(f2, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd2 <= 0) {
		printf("Unable to open file: %s\n", f2);
		close(fd1);
		inc_files_failed();
		return ERROR;
	}

	fm1 = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
	if(MAP_FAILED == fm1) {
		printf("Unable to map file: %s\n", f1);
		close(fd1);
		close(fd2);
		inc_files_failed();
		return ERROR;
	}
		
	fm2 = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
	if(MAP_FAILED == fm2) {
		printf("Unable to map file: %s\n", f2);
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

int crc_calc(struct list_entry *list)
{
	void *fm;
	int fd, size;
	list->fd = 0;
	list->mm = NULL;

	fd = open(list->name, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd <= 1) {
		printf("Unable to open file: %s\n", list->name);
		return ERR_OPEN_FILE;
	}

	size = list->size > 512 ? 512 : list->size;
	fm = mmap(0, list->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(MAP_FAILED == fm) {
		printf("Unable to map file: %s\n", list->name);
		close(fd);
		return ERR_MEM_MAP;
	}	

	list->crc = crc16(0, fm, size);
	list->fd = fd;
	list->mm = fm;
	return 0;
}

void do_crc_comparisons(struct list_entry *list)
{
	int files_eq, display_first = 0;
	unsigned long size = list->size;
	struct list_entry *first, *sec;

	for(first = list; NULL != first && NULL != first->next_down; first = first->next_down) {
		if (0 == first->size)
			continue;
		for(sec = first->next_down; sec != NULL; sec = sec->next_down) {
			if(0 == sec->size || first->crc != sec->crc)
				continue;
			files_eq = !memcmp(first->mm, sec->mm, size);
			if(files_eq){
				if(!display_first) {
					printf("\n%lu\n%s\n",size, first->name);
					display_first = 1;
				}
				printf("%s\n", sec->name);
				munmap(sec->mm, size);
				sec->size = 0;
				close(sec->fd);
				sec->fd = 0;
			}
		}
		display_first = 0;
	}
}

void file_comparisons(struct list_entry **list)
{
	struct list_entry *l = *list, *used = NULL, *dl;	/* down list */

	for(;NULL != l; l = l->next) {
		*list = l;
		while(used) {
			struct list_entry *tmp = used;
			used = used->next_down;
			free(tmp);
		}
		used = l;

		if(NULL == l->next_down) {
			continue;
		}
		if(NULL == l->next_down->next_down) {
				if(is_files_equal(l->name, l->next_down->name, l->size))
					printf("\n%lu\n%s\n%s\n", l->size, l->name, l->next_down->name);
		} else {
			/*
			* More than two files have the same size 
			* For this case first calculate CRC of each file part,
			* then the files is compared if CRC is equal.
			*/
			for(dl = l; NULL != dl; dl = dl->next_down)
				crc_calc(dl);
			do_crc_comparisons(l);
			unmap_and_close(l);
		}
	}
}

int	file_dup_find(char *dir, unsigned long min_size)
{
	struct list_entry *list;
	list = malloc(sizeof(struct list_entry));
	if (NULL == list) {
		printf("Insufficient of memory\n");
		return ERR_NOMEM;
	}
	list->next = NULL;
	list->next_down = NULL;
	list->size = 0;
	list->name[0] = 0;

	printf("Scanning files...\n");
	scan_dir(dir, min_size, insert_file_entry, list);
	printf("\nStarting comparisons...\n");
	file_comparisons(&list);
	printf("%d files scanned, %d file(s) failed.\n", files_scanned(), files_failed());
	clean_malloc(list);
	return 0;
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

