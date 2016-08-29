/* tools/mkbootimg/mkbootimg.c
**
** Copyright 2007, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

static void *load_file(const char *fn, unsigned *_sz)
{
    char *data;
    int sz;
    int fd;

    data = 0;
    fd = open(fn, O_RDONLY);
    if(fd < 0) return 0;

    sz = lseek(fd, 0, SEEK_END);
    if(sz < 0) goto oops;

    if(lseek(fd, 0, SEEK_SET) != 0) goto oops;

    data = (char*) malloc(sz);
    if(data == 0) goto oops;

    if(read(fd, data, sz) != sz) goto oops;
    close(fd);

    if(_sz) *_sz = sz;
    return data;

oops:
    close(fd);
    if(data != 0) free(data);
    return 0;
}

int usage(void)
{
    fprintf(stderr,"usage: mkbootimg\n"
            "       --kernel <filename>\n"
            "       [ --ramdisk <filename> ]\n"
            "       [ --second <2ndbootloader-filename> ]\n"
            "       [ --cmdline <kernel-commandline> ]\n"
            "       [ --board <boardname> ]\n"
            "       [ --base <address> ]\n"
            "       [ --pagesize <pagesize> ]\n"
            "       [ --dt <filename> ]\n"
            "       [ --id ]\n"
            "       -o|--output <filename>\n"
            );
    return 1;
}

int main(int argc, char **argv)
{
    char *kernel_fn = NULL;
    void *kernel_data = NULL;
    char *bootimg = NULL;
    int fd;
    uint32_t kernel_size;

    argc--;
    argv++;

    bool get_id = false;
    while(argc > 0){
        char *arg = argv[0];
        if (!strcmp(arg, "--id")) {
            get_id = true;
            argc -= 1;
            argv += 1;
        } else if(argc >= 2) {
            char *val = argv[1];
            argc -= 2;
            argv += 2;
            if(!strcmp(arg, "--output") || !strcmp(arg, "-o")) {
                bootimg = val;
            } else if(!strcmp(arg, "--kernel")) {
                kernel_fn = val;
            } else if(!strcmp(arg, "--ramdisk")) {
            } else if(!strcmp(arg, "--second")) {
            } else if(!strcmp(arg, "--cmdline")) {
            } else if(!strcmp(arg, "--base")) {
            } else if(!strcmp(arg, "--kernel_offset")) {
            } else if(!strcmp(arg, "--ramdisk_offset")) {
            } else if(!strcmp(arg, "--second_offset")) {
            } else if(!strcmp(arg, "--os_version")) {
            } else if(!strcmp(arg, "--os_patch_level")) {
            } else if(!strcmp(arg, "--tags_offset")) {
            } else if(!strcmp(arg, "--board")) {
            } else if(!strcmp(arg,"--pagesize")) {
            } else if(!strcmp(arg, "--id")) {
            } else {
                return usage();
            }
        } else {
            return usage();
        }
    }

    if(bootimg == 0) {
        fprintf(stderr,"error: no output filename specified\n");
        return usage();
    }

    if(kernel_fn == 0) {
        fprintf(stderr,"error: no kernel image specified\n");
        return usage();
    }

    kernel_data = load_file(kernel_fn, &kernel_size);

    fd = open(bootimg, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if(fd < 0) {
        fprintf(stderr,"error: could not create '%s'\n", bootimg);
        return 1;
    }

    if(write(fd, kernel_data, kernel_size) != (ssize_t) kernel_size) goto fail;

    return 0;

fail:
    unlink(bootimg);
    close(fd);
    fprintf(stderr,"error: failed writing '%s': %s\n", bootimg,
            strerror(errno));
    return 1;
}
