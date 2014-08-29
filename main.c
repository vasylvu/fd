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

struct crc_entry {
	struct crc_entry *next;
	char *name;
	unsigned short crc;
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

	while (entry = readdir(dir)) {
		if((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))
			continue;

		strcpy(full_name, dir_name);
		strcat(full_name, "/");
		strcat(full_name, entry->d_name);
		if(entry->d_type == 0x04) {
			if(scan_dir(full_name, min_size, list_cb, list)) {
				closedir(dir);
				return -3;			// TODO: error code
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

int crc(unsigned short *crc, struct list_entry *list)
{
	void *fm;
	int fd, size;

	fd = open(list->name, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd <= 1)
		return 1;

	size = list->size > 512 ? 512 : list->size;
	fm = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(MAP_FAILED == fm) {
		close(fd);
		return 1;
	}	

	*crc = crc16(0, fm, size);
	munmap(fm, size);
	close(fd);
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
	if(crc(&new_ent->crc, list)) {
		free(new_ent);
		return 0;
	}		
	*hash_list = new_ent;
	return 0;
}

void do_crc_comparisons(struct crc_entry* *hl, unsigned long size)
{
	void *fm1, *fm;
	int fd1 = 0, fd, files_eq, display_1st = 0;
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

			if(fd1 == 0) {
				fd1 = open(first->name, O_RDWR, S_IRUSR | S_IWUSR);
				fm1 = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
			}
			fd = open(list->name, O_RDWR, S_IRUSR | S_IWUSR);
			if(fd <= 1)
				goto nxt;
			
			fm = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if(MAP_FAILED == fm) {
				close(fd);
				goto nxt;
			}	
			files_eq = !memcmp(fm1, fm, size);
			munmap(fm, size);
			close(fd);
			if(files_eq){
				if(!display_1st) {
					printf("%lu\n%s\n",size, first->name);
					display_1st = 1;
				}
				printf("%s\n", list->name);
				nxt:
				prev->next = list->next; /* error if list->next == NULL */
				free(list);
				list = prev->next;
			}else{
				list = list->next;
				prev = prev->next;
			}
		}
		
		if(fd1) {
			munmap(fm1, size);
			close(fd1);
			fd1 = 0;
			display_1st = 0;
		}
		first = first->next;
	}
}

void file_comparisons(struct list_entry *l)
{
	struct list_entry *sf;	/* second_file */
	struct crc_entry **hl;	/* hash list */
	struct crc_entry *first;

	l = l->next; /* drop empty entry */
	
	while(NULL != l->next) {
		if(l->size != l->next->size) {
			l = l->next;
			continue;
		}

		sf = l->next;
		if((NULL != sf  &&  l->size == sf->size) &&
			(NULL != sf->next &&  sf->next->size != sf->size || NULL == sf->next)) {
				if(is_files_equal(l->name, sf->name, l->size))
					printf("%lu\n%s\n%s\n", l->size, l->name, sf->name);
		} else {
			first = malloc(sizeof(struct crc_entry));
			if(NULL == first) {
				printf("Memory allocation error\n");
				return;
			}
			first->name = l->name;
			first->next = NULL;
			crc(&first->crc, l);
			hl = &first;
			while(NULL != sf && l->size == sf->size) {
				if(push_to_crc_list(hl, sf)) {
					clean_malloc(*hl);
					return;
				}
				sf = sf->next;
				l = l->next;
			}
			do_crc_comparisons(hl, l->size);
			clean_malloc(*hl);
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
	printf("\nComparisons started...\n");
	file_comparisons(list);
	printf("%d files scanned, %d file(s) failed.\n", files_scanned(), files_failed());
	clean_malloc(list);
	return ret;
}

int	main(int argc, char *argv[])
{
	int str_end;
	unsigned long min_size = 0;

	if (argc < 2 || argc > 3) {
		printf("Use: %s <Directory> [Minimal File size] [Big file size for list]\n", argv[0]);
		return 0;
	}

	if(argc > 2) {
		min_size = atoi(argv[2]);
	}
	str_end = strlen(argv[1]) - 1;
	if (argv[1][str_end] == '/')
		argv[1][str_end] = 0;

	return file_dup_find(argv[1], min_size);
}

