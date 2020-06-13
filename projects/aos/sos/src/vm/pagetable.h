#pragma once

// #include "addrspace.h"
#include "../ut.h"

#define PAGE_TABLE_LEVELS (4)
#define PAGE_TABLE_LEVEL_BITS (9)
#define PAGE_TABLE_LEVEL_SIZE ((1)<<(PAGE_TABLE_LEVEL_BITS))
#define PAGE_TABLE_LEVEL_MASK ((PAGE_TABLE_LEVEL_SIZE) - (1))
#define PTE_BITS (12)
#define PTE_SIZE ((1)<<(PTE_BITS))


static int level_to_offset[4] = {PTE_BITS + PAGE_TABLE_LEVEL_BITS * 0,
                                 PTE_BITS + PAGE_TABLE_LEVEL_BITS * 1,
                                 PTE_BITS + PAGE_TABLE_LEVEL_BITS * 2,
                                 PTE_BITS + PAGE_TABLE_LEVEL_BITS * 3};

typedef struct pte {
} pte_t;

typedef struct page_table {
  int level;
  void *entries[PAGE_TABLE_LEVEL_SIZE]; // pte_t if level==0 else page_table_t
  seL4_CPtr cap;
  ut_t *ut;
} page_table_t;

