/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_CONFIG_LOCK_H
#define QC5_CONFIG_LOCK_H
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
static int qcs_lock(int directory)
{
 int fd=openat(directory,".qc5-config.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return -1;struct stat s;
 if(fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_uid!=geteuid()||s.st_nlink!=1||flock(fd,LOCK_EX)){close(fd);return -1;}return fd;
}
#endif
