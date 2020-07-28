/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <utils/util.h>
#include <stdbool.h>
#include <sel4/sel4.h>
#include <elf/elf.h>
#include <string.h>
#include <assert.h>
#include <cspace/cspace.h>

#include "vm/frame_table.h"
#include "ut.h"
#include "mapping.h"
#include "elfload.h"
#include "vm/addrspace.h"
#include "vm/pagetable.h"

/*
 * Convert ELF permissions into seL4 permissions.
 */
static inline seL4_CapRights_t get_sel4_rights_from_elf(unsigned long permissions)
{
    bool canRead = permissions & PF_R || permissions & PF_X;
    bool canWrite = permissions & PF_W;

    if (!canRead && !canWrite) {
        return seL4_AllRights;
    }

    return seL4_CapRights_new(false, false, canRead, canWrite);
}

/*
 * Load an elf segment into the given vspace.
 *
 * TODO: The current implementation maps the frames into the loader vspace AND the target vspace
 *       and leaves them there. Additionally, if the current implementation fails, it does not
 *       clean up after itself.
 *
 *       This is insufficient, as you will run out of resouces quickly, and will be completely fixed
 *       throughout the duration of the project, as different milestones are completed.
 *
 *       Be *very* careful when editing this code. Most students will experience at least one elf-loading
 *       bug.
 *
 * The content to load is either zeros or the content of the ELF
 * file itself, or both.
 * The split between file content and zeros is a follows.
 *
 * File content: [dst, dst + file_size)
 * Zeros:        [dst + file_size, dst + segment_size)
 *
 * Note: if file_size == segment_size, there is no zero-filled region.
 * Note: if file_size == 0, the whole segment is just zero filled.
 *
 * @param cspace        of the loader, to allocate slots with
 * @param loader        vspace of the loader
 * @param loadee        vspace to load the segment in to
 * @param src           pointer to the content to load
 * @param segment_size  size of segment to load
 * @param file_size     end of section that should be zero'd
 * @param dst           destination base virtual address to load
 * @param permissions   for the mappings in this segment
 * @return
 *
 */
static int load_segment_into_vspace(addrspace_t *as, cspace_t *cspace, seL4_CPtr loadee, vnode_t *vnode, size_t offset, size_t segment_size,
                                    size_t file_size, uintptr_t dst, seL4_CapRights_t permissions, seL4_ARM_VMAttributes attr, coro_t coro)
{
    assert(file_size <= segment_size);

    /* We work a page at a time in the destination vspace. */
    unsigned int pos = 0;
    seL4_Error err = seL4_NoError;
    while (pos < segment_size) {
        uintptr_t loadee_vaddr = (ROUND_DOWN(dst, PAGE_SIZE_4K));

        pte_t loadee_pte;
 
        err = alloc_map_frame(as, cspace, loadee_vaddr, permissions, attr, &loadee_pte, coro);

        /* A frame has already been mapped at this address. This occurs when segments overlap in
         * the same frame, which is permitted by the standard. That's fine as we
         * leave all the frames mapped in, and this one is already mapped. Give back
         * the ut we allocated and continue on to do the write.
         *
         * Note that while the standard permits segments to overlap, this should not occur if the segments
         * have different permissions - you should check this and return an error if this case is detected. */
        if (err == seL4_DeleteFirst) {
            region_t *r = get_region(as->regions, loadee_vaddr);
            if (r->rights.words[0] != permissions.words[0] || r->attrs != attr) {
                ZF_LOGE("wrong perm");
                return -1;
            }
        } else if (err != seL4_NoError) {
            ZF_LOGE("Failed to map into loadee at %p, error %u", (void *) loadee_vaddr, err);
            return -1;
        }

        seL4_CPtr loadee_frame = loadee_pte.cap;

        /* finally copy the data */
        seL4_CPtr lcptr;
        size_t size;
        unsigned char *loader_data = map_vaddr_to_sos(cspace, as, loadee_vaddr, &lcptr, &size, coro);
        *loader_data = 0x88;


        /* Write any zeroes at the start of the block. */
        size_t leading_zeroes = dst % PAGE_SIZE_4K;
        memset(loader_data, 0, leading_zeroes);
        loader_data += leading_zeroes;

        /* Copy the data from the source. */
        size_t segment_bytes = PAGE_SIZE_4K - leading_zeroes;
        size_t file_bytes = MIN(segment_bytes, file_size - pos);
        if (pos < file_size) {
            // memcpy(loader_data, src, file_bytes);
            // TODO
            uio_t myuio;
            if (uio_kinit(&myuio, loader_data, file_bytes, offset, UIO_WRITE)) {
                ZF_LOGE("can't uio_kinit");
                unmap_vaddr_from_sos(cspace, lcptr);
                return -1;
            }
            int readbytes = VOP_PREAD(vnode, &myuio, coro);
            if (readbytes != file_bytes) {
                ZF_LOGE("can't read elf file");
                unmap_vaddr_from_sos(cspace, lcptr);
                return -1;
            }
            uio_destroy(&myuio, NULL);
        } else {
            memset(loader_data, 0, file_bytes);
        }
        loader_data += file_bytes;

        /* Fill in the end of the frame with zereos */
        size_t trailing_zeroes = PAGE_SIZE_4K - (leading_zeroes + file_bytes);
        memset(loader_data, 0, trailing_zeroes);

        /* Flush the frame contents from loader caches out to memory. */
        seL4_ARM_Page_Clean_Data(lcptr, 0, BIT(seL4_PageBits));
        seL4_ARM_Page_Unify_Instruction(lcptr, 0, BIT(seL4_PageBits));

        unmap_vaddr_from_sos(cspace, lcptr);

        /* Invalidate the caches in the loadee forcing data to be loaded
         * from memory. */
        if (seL4_CapRights_get_capAllowWrite(permissions)) {
            seL4_ARM_Page_Invalidate_Data(loadee_frame, 0, PAGE_SIZE_4K);
        }
        seL4_ARM_Page_Unify_Instruction(loadee_frame, 0, PAGE_SIZE_4K);

        pos += segment_bytes;
        dst += segment_bytes;
        offset += segment_bytes;
    }
    return 0;
}

int elf_load(cspace_t *cspace, seL4_CPtr loadee_vspace, elf_t *elf_file, vnode_t *elf_vnode, addrspace_t *as, vaddr_t *end, coro_t coro) {
    *end = 0;

    int num_headers = elf_getNumProgramHeaders(elf_file);
    for (int i = 0; i < num_headers; i++) {

        /* Skip non-loadable segments (such as debugging data). */
        if (elf_getProgramHeaderType(elf_file, i) != PT_LOAD) {
            continue;
        }

        /* Fetch information about this segment. */
        size_t source_offset = elf_getProgramHeaderOffset(elf_file, i);
        size_t file_size = elf_getProgramHeaderFileSize(elf_file, i);
        size_t segment_size = elf_getProgramHeaderMemorySize(elf_file, i);
        uintptr_t vaddr = elf_getProgramHeaderVaddr(elf_file, i);
        seL4_Word flags = elf_getProgramHeaderFlags(elf_file, i);
        seL4_CapRights_t rights = get_sel4_rights_from_elf(flags);
        seL4_ARM_VMAttributes attr = seL4_ARM_Default_VMAttributes;

        if (!(flags & PF_X)) attr |= seL4_ARM_ExecuteNever;

        int err = as_define_region(as, vaddr, segment_size, rights, attr, NULL);
        if (err) {
            ZF_LOGE("Elf loading failed!");
            return -1;
        }

        /* Copy it across into the vspace. */
        ZF_LOGD(" * Loading segment %p-->%p\n", (void *) vaddr, (void *)(vaddr + segment_size));
        err = load_segment_into_vspace(as, cspace, loadee_vspace, elf_vnode, source_offset, segment_size, file_size, vaddr, rights, attr, coro);
        if (err) {
            ZF_LOGE("Elf loading failed!");
            return -1;
        }
        if (vaddr + segment_size > *end) *end = vaddr + segment_size;
        printf("elf load\n");
    }

    return 0;
}

static int elf32_getSectionNamed_v(elf_t *elfFile, vnode_t *vnode, const char *str, uintptr_t *result, coro_t coro) {
    ZF_LOGE("unimplemented");
    return -1;
}

static int elf64_getSectionNamed_v(elf_t *elfFile, vnode_t *vnode, const char *str, uintptr_t *result, coro_t coro) {
    Elf64_Shdr *sectionTable = NULL;
    char *shrtrtab_data = NULL;

    size_t strl = strlen(str);

    Elf64_Ehdr header = elf64_getHeader(elfFile);
    size_t str_table_idx = elf_getSectionStringTableIndex(elfFile);
    size_t numSections = elf_getNumSections(elfFile);
    size_t sectionTable_size = sizeof(Elf64_Shdr) * numSections;
    sectionTable = malloc(sectionTable_size);
    if (sectionTable == NULL) {
        ZF_LOGE("can't malloc");
        goto fail;
    }

    uio_t myuio;
    if (uio_kinit(&myuio, sectionTable, sectionTable_size, header.e_shoff, UIO_WRITE)) {
        ZF_LOGE("can't uio_kinit");
        goto fail;
    }
    if (VOP_PREAD(vnode, &myuio, coro) != sectionTable_size) {
        ZF_LOGE("can't read elf");
        goto fail;
    }
    uio_destroy(&myuio, NULL);

    Elf64_Shdr *shrtrtab_header = sectionTable + str_table_idx;

    shrtrtab_data = malloc(shrtrtab_header->sh_size);
    if (shrtrtab_data == NULL) {
        ZF_LOGE("can't malloc");
        goto fail;
    }
    if (uio_kinit(&myuio, shrtrtab_data, shrtrtab_header->sh_size, shrtrtab_header->sh_offset, UIO_WRITE)) {
        ZF_LOGE("can't uio_kinit");
        goto fail;
    }
    if (VOP_PREAD(vnode, &myuio, coro) != shrtrtab_header->sh_size) {
        ZF_LOGE("can't read elf");
        goto fail;
    }
    size_t i = 0;
    for (Elf64_Shdr *curr = sectionTable; i < numSections; i++, curr++) {
        if (curr->sh_name + strl >= shrtrtab_header->sh_size)
            continue;
        if (strncmp(shrtrtab_data + curr->sh_name, str, strl) == 0) {
            *result = curr->sh_offset;
            printf("i found coronavirus cure\n");
            free(sectionTable);
            free(shrtrtab_data);
            return 0;
        }
    }

    fail:
    if (sectionTable) free(sectionTable);
    if (shrtrtab_data) free(shrtrtab_data);
    return -1;
}

// TO_WORK || (!TO_WORK) = 1
int elf_getSectionNamed_v(elf_t *elfFile, vnode_t *vnode, const char *str, uintptr_t *result, coro_t coro) {
    if (elf_isElf32(elfFile)) {
        return elf32_getSectionNamed_v(elfFile, vnode, str, result, coro);
    }
    return elf64_getSectionNamed_v(elfFile, vnode, str, result, coro);
}

int elf_find_vsyscall(elf_t *elfFile, vnode_t *vnode, uintptr_t *result, coro_t coro) {
    uintptr_t offset;
    int res = elf_getSectionNamed_v(elfFile, vnode, "__vsyscall", &offset, coro);
    if (res < 0) return res;
    uio_t myuio;
    if (uio_kinit(&myuio, result, sizeof(uintptr_t), offset, UIO_WRITE)) {
        ZF_LOGE("can't uio_kinit");
        return -1;
    }
    if (VOP_PREAD(vnode, &myuio, coro) != sizeof(uintptr_t)) {
        ZF_LOGE("can't read elf");
        return -1;
    }
    uio_destroy(&myuio, NULL);
    return 0;
}
