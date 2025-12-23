#include <new>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <map>
#include <unordered_set>
#include "../ssd.h"

using namespace ssd;

FtlImpl_MNftl::FtlImpl_MNftl(Controller &controller)
    : FtlParent(controller)
{
    P = BLOCK_SIZE;
    Q = MNFTL_OOB_SIZE / MNFTL_ENTRY_SIZE;
    num_pmd = (P + Q - 1) / Q;

    has_current_block = false;
    current_page_offset = 0;
    current_block = Address(0, NONE);

    printf("Using MNFTL\n");
    printf("P=%u Q=%u NUM_PMD=%u\n", P, Q, num_pmd);
}

FtlImpl_MNftl::~FtlImpl_MNftl(void) {}


// =====================
// Postponed GC hook
// =====================
bool FtlImpl_MNftl::is_current_block(long pbn) const
{
    return (current_blocks.find(pbn) != current_blocks.end());
}


// =====================
// Allocate new current block
// =====================
void FtlImpl_MNftl::allocate_new_current_block(Event &event)
{
    Address blk = Block_manager::instance()->get_free_block(event);

    current_block = blk;
    current_block.valid = BLOCK;
    current_page_offset = 0;
    has_current_block = true;

    current_blocks.insert(current_block.block);   
    BML.push_back(current_block.block);
}


// =====================
// Allocate page
// =====================
ulong FtlImpl_MNftl::alloc_page_in_current_block(Event &event, Address &outAddr)
{
    assert(has_current_block);
    Address addr = current_block;
    controller.get_free_page(addr);
    outAddr = addr;
    return addr.get_linear_address();
}


// =====================
// READ
// =====================
enum status FtlImpl_MNftl::read(Event &event)
{
    controller.stats.numFTLRead++;

    ulong lpn = event.get_logical_address();
    uint lbn  = lpn / P;
    uint bo   = lpn % P;

    uint pmd_index = bo / Q;
    uint map_slot  = bo % Q;

    if (PMD.find(lbn) == PMD.end()) {
        event.set_noop(true);
        event.set_address(Address(0, PAGE));
        return controller.issue(event);
    }

    long anchor = PMD[lbn][pmd_index];
    if (anchor == -1) {
        event.set_noop(true);
        event.set_address(Address(0, PAGE));
        return controller.issue(event);
    }

    event.incr_time_taken(OOB_READ_DELAY);

    long ppn = PMT[lbn][pmd_index][map_slot];
    if (ppn == -1) {
        event.set_noop(true);
        event.set_address(Address(0, PAGE));
        return controller.issue(event);
    }

    event.set_address(Address(ppn, PAGE));
    return controller.issue(event);
}


// =====================
// WRITE
// =====================
enum status FtlImpl_MNftl::write(Event &event)
{
    controller.stats.numFTLWrite++;

    ulong lpn = event.get_logical_address();
    uint lbn  = lpn / P;
    uint bo   = lpn % P;

    if (!has_current_block || current_page_offset == P) {
        allocate_new_current_block(event);
    }

    uint pmd_index = bo / Q;
    uint map_slot  = bo % Q;

    if (PMD.find(lbn) == PMD.end())
        PMD[lbn] = std::vector<long>(num_pmd, -1);

    if (PMT.find(lbn) == PMT.end()) {
        PMT[lbn] = std::vector<std::vector<long>>(num_pmd,
                    std::vector<long>(Q, -1));
    }

    if (PMD[lbn][pmd_index] != -1)
        event.incr_time_taken(OOB_READ_DELAY);

    Address newAddr;
    ulong new_ppn = alloc_page_in_current_block(event, newAddr);
    current_page_offset++;

    long old_ppn = PMT[lbn][pmd_index][map_slot];
    if (old_ppn != -1) {
        event.set_replace_address(Address((ulong)old_ppn, PAGE));
        rmap.erase((ulong)old_ppn);                 // 舊ppn不再代表該LPN
    }

    PMT[lbn][pmd_index][map_slot] = (long)new_ppn;
    PMD[lbn][pmd_index] = (long)new_ppn;
    rmap[new_ppn] = { lbn, pmd_index, map_slot };

    event.set_address(newAddr);
    return controller.issue(event);
}

/* ---------- MNFTL trim ---------- */
enum status FtlImpl_MNftl::trim(Event &event)
{
    controller.stats.numFTLTrim++;

    ulong lpn = event.get_logical_address();
    uint lbn = lpn / P;
    uint bo  = lpn % P;

    if (PMD.find(lbn) == PMD.end())
        return SUCCESS;

    // Invalidate mapping slot (simple approach)
    // Need to know PMD index and map slot; can't just index by bo (page-level).
    uint pmd_index = bo / Q;
    uint map_slot  = bo % Q;
    if (pmd_index < PMD[lbn].size())
    {
        long old_ppn = PMT[lbn][pmd_index][map_slot];
        if (old_ppn != -1) {
            rmap.erase((ulong)old_ppn);
            PMT[lbn][pmd_index][map_slot] = -1;
        }
    }

    event.set_noop(true);
    event.set_address(Address(0, PAGE));

    return controller.issue(event);
}

// =====================
// CLEANUP BLOCK (Algorithm 3)
// =====================
void FtlImpl_MNftl::cleanup_block(Event &event, Block *block)
{
    // Postponed GC cost
    event.incr_time_taken(num_pmd * OOB_READ_DELAY);

    for (uint i = 0; i < BLOCK_SIZE; i++) {
        if (block->get_state(i) != VALID)
            continue;

        ulong old_ppn = block->get_physical_address() + i;

        Event r(READ, event.get_logical_address(), 1, event.get_start_time());
        r.set_address(Address(old_ppn, PAGE));
        controller.issue(r);

        if (!has_current_block || current_page_offset == P)
            allocate_new_current_block(event);

        Address newAddr;
        ulong new_ppn = alloc_page_in_current_block(event, newAddr);
        current_page_offset++;

        Event w(WRITE, event.get_logical_address(), 1,
                event.get_start_time() + r.get_time_taken());
        w.set_address(newAddr);
        w.set_replace_address(Address(old_ppn, PAGE));
        controller.issue(w);

        controller.stats.valid_page_copies++;
        event.incr_time_taken(r.get_time_taken() + w.get_time_taken());

        auto it = rmap.find(old_ppn);
        if (it != rmap.end()) {
            RmapEntry e = it->second;

            PMT[e.lbn][e.pmd][e.slot] = (long)new_ppn;
            PMD[e.lbn][e.pmd]         = (long)new_ppn;

            rmap.erase(it);
            rmap[new_ppn] = e;
        }
 
        controller.stats.numWLRead++;
        controller.stats.numWLWrite++;
    }
    current_blocks.erase(block->get_physical_address() / BLOCK_SIZE);
}
