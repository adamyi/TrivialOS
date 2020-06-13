#include "process.h"
#include "vmem_layout.h"

static seL4_Word shared_buffer_curr = SOS_SHARED_BUFFER;

seL4_Word get_new_shared_buffer_vaddr() {
    seL4_Word ret = shared_buffer_curr;
    shared_buffer_curr += PAGE_SIZE_4K;
    return ret;
}
