

//fork from https://github.com/avs333/Nougat_dlfunctions
//do some modify
//support all cpu abi such as x86, x86_64
//support filename search if filename is not start with '/'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <elf.h>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <android/log.h>

#include "fake_dlfcn.h"

#define TAG_NAME    "myhook_dlfcn_ex"

#ifdef LOG_DBG
#define log_info(fmt, args...) __android_log_print(ANDROID_LOG_INFO, TAG_NAME, (const char *) fmt, ##args)
#define log_err(fmt, args...) __android_log_print(ANDROID_LOG_ERROR, TAG_NAME, (const char *) fmt, ##args)
#define log_dbg log_info
#else
#define log_dbg(...)
#define log_info(fmt, args...) __android_log_print(ANDROID_LOG_INFO, TAG_NAME, (const char *) fmt, ##args)
#define log_err(fmt, args...) __android_log_print(ANDROID_LOG_ERROR, TAG_NAME, (const char *) fmt, ##args)
#endif

#ifdef __LP64__
#define Elf_Ehdr Elf64_Ehdr
#define Elf_Shdr Elf64_Shdr
#define Elf_Sym  Elf64_Sym
#else
#define Elf_Ehdr Elf32_Ehdr
#define Elf_Shdr Elf32_Shdr
#define Elf_Sym  Elf32_Sym
#endif

extern "C" {

static int fake_dlclose(void *handle) {
    if (handle) {
        struct ctx *ctx = (struct ctx *) handle;
        if (ctx->dynsym) free(ctx->dynsym);    /* we're saving dynsym and dynstr */
        if (ctx->dynstr) free(ctx->dynstr);    /* from library file just in case */
        free(ctx);
    }
    return 0;
}

/* flags are ignored */
static void *fake_dlopen_with_path(const char *libpath, int flags) {
    FILE *maps;
    char buff[256];
    struct ctx *ctx = 0;
    off_t load_addr, size;
    int k, fd = -1, found = 0;
    char *shoff;
    Elf_Ehdr *elf = (Elf_Ehdr *) MAP_FAILED;

#define fatal(fmt, args...) do { \
    goto err_exit; \
} while(0)

    maps = fopen("/proc/self/maps", "r");
    if (!maps) fatal("failed to open maps");

    while (!found && fgets(buff, sizeof(buff), maps)) {
        if (strstr(buff, libpath) && (strstr(buff, "r-xp") || strstr(buff, "r--p"))) found = 1;
    }
    fclose(maps);

    if (!found) fatal("%s not found in my userspace", libpath);

    if (sscanf(buff, "%lx", &load_addr) != 1)
        fatal("failed to read load address for %s", libpath);

//    log_info("%s loaded in Android at 0x%08lx", libpath, load_addr);

    /* Now, mmap the same library once again */

    fd = open(libpath, O_RDONLY);
    if (fd < 0) fatal("failed to open %s", libpath);

    size = lseek(fd, 0, SEEK_END);
    if (size <= 0) fatal("lseek() failed for %s", libpath);
//    log_info("lib:%s size is:%d", libpath, size);

    elf = (Elf_Ehdr *) mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    fd = -1;

    if (elf == MAP_FAILED) fatal("mmap() failed for %s", libpath);

    ctx = (struct ctx *) calloc(1, sizeof(struct ctx));
    if (!ctx) fatal("no memory for %s", libpath);

    ctx->load_addr = (void *) load_addr;
    shoff = ((char *) elf) + elf->e_shoff;
//    log_info("%s: elf->e_shnum=%d, elf->e_shentsize:%d", __func__, elf->e_shnum, elf->e_shentsize);
    for (k = 0; k < elf->e_shnum; k++, shoff += elf->e_shentsize) {

        Elf_Shdr *sh = (Elf_Shdr *) shoff;
//        log_info("%s: k=%d shdr=%p type=%x", __func__, k, sh, sh->sh_type);

        switch (sh->sh_type) {

            case SHT_DYNSYM:
                if (ctx->dynsym) fatal("%s: duplicate DYNSYM sections", libpath); /* .dynsym */
                ctx->dynsym = malloc(sh->sh_size);
                if (!ctx->dynsym) fatal("%s: no memory for .dynsym", libpath);
                memcpy(ctx->dynsym, ((char *) elf) + sh->sh_offset, sh->sh_size);
                ctx->nsyms = (sh->sh_size / sizeof(Elf_Sym));
                break;

            case SHT_STRTAB:
                if (ctx->dynstr) break;    /* .dynstr is guaranteed to be the first STRTAB */
                ctx->dynstr = malloc(sh->sh_size);
                if (!ctx->dynstr) fatal("%s: no memory for .dynstr", libpath);
                memcpy(ctx->dynstr, ((char *) elf) + sh->sh_offset, sh->sh_size);
                break;

            case SHT_PROGBITS:
                if (!ctx->dynstr || !ctx->dynsym) break;
                /* won't even bother checking against the section name */
                ctx->bias = (off_t) sh->sh_addr - (off_t) sh->sh_offset;
                k = elf->e_shnum;  /* exit for */
                break;
        }
    }
//    log_info("%s: ctx->load_addr=%d, ctx->bias:%d", __func__, ctx->load_addr, ctx->bias);
    munmap(elf, size);
    elf = 0;
    if (!ctx->dynstr || !ctx->dynsym) fatal("dynamic sections not found in %s", libpath);
#undef fatal
    log_dbg("%s: ok, dynsym = %p, dynstr = %p", libpath, ctx->dynsym, ctx->dynstr);
    return ctx;
    err_exit:
    if (fd >= 0) close(fd);
    if (elf != MAP_FAILED) munmap(elf, size);
    fake_dlclose(ctx);
    return 0;
}


#if defined(__LP64__)
static const char *const kSystemLibDir = "/system/lib64/";
static const char *const kOdmLibDir = "/odm/lib64/";
static const char *const kVendorLibDir = "/vendor/lib64/";
static const char *const kApexLibDir = "/apex/com.android.runtime/lib64/";
static const char *const kApexArtNsLibDir = "/apex/com.android.art/lib64/";
#else
static const char *const kSystemLibDir = "/system/lib/";
static const char *const kOdmLibDir = "/odm/lib/";
static const char *const kVendorLibDir = "/vendor/lib/";
static const char *const kApexLibDir = "/apex/com.android.runtime/lib/";
static const char *const kApexArtNsLibDir = "/apex/com.android.art/lib/";
#endif

static void *fake_dlopen(const char *filename, int flags) {
    if (strlen(filename) > 0 && filename[0] == '/') {
        return fake_dlopen_with_path(filename, flags);
    } else {
        char buf[512] = {0};
        void *handle = NULL;
        //sysmtem
        strcpy(buf, kSystemLibDir);
        strcat(buf, filename);
        handle = fake_dlopen_with_path(buf, flags);
        if (handle) {
            return handle;
        }

        // apex in ns com.android.runtime
        memset(buf, 0, sizeof(buf));
        strcpy(buf, kApexLibDir);
        strcat(buf, filename);
        handle = fake_dlopen_with_path(buf, flags);
        if (handle) {
            return handle;
        }

        // apex in ns com.android.art
        memset(buf, 0, sizeof(buf));
        strcpy(buf, kApexArtNsLibDir);
        strcat(buf, filename);
        handle = fake_dlopen_with_path(buf, flags);
        if (handle) {
            return handle;
        }

        //odm
        memset(buf, 0, sizeof(buf));
        strcpy(buf, kOdmLibDir);
        strcat(buf, filename);
        handle = fake_dlopen_with_path(buf, flags);
        if (handle) {
            return handle;
        }

        //vendor
        memset(buf, 0, sizeof(buf));
        strcpy(buf, kVendorLibDir);
        strcat(buf, filename);
        handle = fake_dlopen_with_path(buf, flags);
        if (handle) {
            return handle;
        }

        return fake_dlopen_with_path(filename, flags);
    }
}

void* FindSymbolInSymtab(const char* libpath, const char* symname) {
    int fd = open(libpath, O_RDONLY);
    if (fd < 0) return nullptr;
    void *handle = mydlopen(libpath,RTLD_NOW);
    struct ctx *ctx = (struct ctx *) handle;

    off_t size = lseek(fd, 0, SEEK_END);
    void* map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    Elf_Ehdr* ehdr = (Elf_Ehdr*)map;
    Elf_Shdr* shdr = (Elf_Shdr*)((char*)map + ehdr->e_shoff);

    // 查找 .symtab 和对应的 .strtab
    Elf_Shdr* symtab_shdr = nullptr;
    Elf_Shdr* strtab_shdr = nullptr;
    char* shstrtab = (char*)map + shdr[ehdr->e_shstrndx].sh_offset;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab_shdr = &shdr[i];
        } else if (strcmp(&shstrtab[shdr[i].sh_name], ".strtab") == 0) {
            strtab_shdr = &shdr[i];
        }
    }

    if (!symtab_shdr || !strtab_shdr) {
        munmap(map, size);
        return nullptr;
    }

    // 遍历符号表
    Elf_Sym* sym = (Elf_Sym*)((char*)map + symtab_shdr->sh_offset);
    char* strtab = (char*)map + strtab_shdr->sh_offset;
    size_t sym_count = symtab_shdr->sh_size / sizeof(Elf_Sym);

    for (size_t i = 0; i < sym_count; i++) {
        if (strcmp(&strtab[sym[i].st_name], symname) == 0) {
//            void* addr = (void*)((uintptr_t)map + sym[i].st_value);
            void* addr = (void*)((uintptr_t)ctx->load_addr + sym[i].st_value);

            munmap(map, size);
            return addr;
        }
    }
    munmap(map, size);
    return nullptr;
}


static void *fake_dlsym(void *handle, const char *name) {
    int k;
    struct ctx *ctx = (struct ctx *) handle;
    Elf_Sym *sym = (Elf_Sym *) ctx->dynsym;
    char *strings = (char *) ctx->dynstr;
    for (k = 0; k < ctx->nsyms; k++, sym++)
//        log_info("%s found at", strings + sym->st_name);
        if (strcmp(strings + sym->st_name, name) == 0) {
            /*  NB: sym->st_value is an offset into the section for relocatables,
            but a VMA for shared libs or exe files, so we have to subtract the bias */
            void *ret = (char *) ctx->load_addr + sym->st_value - ctx->bias;
//            log_info("%s found at %p", name, ret);
            return ret;
        }
    return 0;
}


static const char *fake_dlerror() {
    return NULL;
}

// =============== implementation for compat ==========
static int SDK_INT = -1;
static int get_sdk_level() {
    if (SDK_INT > 0) {
        return SDK_INT;
    }
    char sdk[PROP_VALUE_MAX] = {0};;
    __system_property_get("ro.build.version.sdk", sdk);
    SDK_INT = atoi(sdk);
    return SDK_INT;
}

int dlclose_ex(void *handle) {
    if (get_sdk_level() >= 24) {
        return fake_dlclose(handle);
    } else {
        return dlclose(handle);
    }
}

void *mydlopen(const char *filename, int flags) {
//    log_info("dlopen: %s", filename);
    if (get_sdk_level() >= 24) {
        return fake_dlopen(filename, flags);
    } else {
        return dlopen(filename, flags);
    }
}

void *mydlsym(void *handle, const char *symbol) {
    if (get_sdk_level() >= 24) {
        return fake_dlsym(handle, symbol);
    } else {
        return dlsym(handle, symbol);
    }
}

const char *dlerror_ex() {
    if (get_sdk_level() >= 24) {
        return fake_dlerror();
    } else {
        return dlerror();
    }
}
}
