#include "addrspace.h"

seL4_Word get_sos_vaddr(addrspace_t *as, vaddr_t paddr) {
    seL4_Word page = PAGE_ALIGN_4K(paddr);
    pte_t *pte = 
}
