#include <new>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <map>
#include "../ssd.h"

using namespace ssd;

FtlImpl_MNftl::FtlImpl_MNftl(Controller &controller): 
    FtlParent(controller)
{
    P = BLOCK_SIZE;
    Q = MNFTL_OOB_SIZE / MNFTL_ENTRY_SIZE;
    num_pmd = (P + Q - 1) / Q;

    has_current_block = false;
    current_page_offset = 0;
    current_block = Address(0, NONE);

    printf("Using MNFTL\n");
    printf("P (pages per block) = %u\n", P);
    printf("Q (entries per PMT) = %u\n", Q);
    printf("NUM_PMD = %u\n", num_pmd);
}

FtlImpl_MNftl::~FtlImpl_MNftl(void)
{
    return;
}

// allocate a new physical block and set it as current_block
void FtlImpl_MNftl::allocate_new_current_block(Event &event)
{
    // Step 6 in Algorithm 1: Allocate new block as PBN
    Address blk = Block_manager::instance()->get_free_block(event);
    // store full address
    current_block = blk;
    current_block.valid = BLOCK;
    has_current_block = true;
    current_page_offset = 0;

    // Update BML: append block index
    BML.push_back(current_block.block);
}

// allocate next free page within current block
// return linear ppn; outAddr is full Address of allocated page
ulong FtlImpl_MNftl::alloc_page_in_current_block(Event &event, Address &outAddr)
{
    assert(has_current_block);
    // start from block address
    Address addr = current_block;
    // ask controller for next free page in that block
    controller.get_free_page(addr);   // modifies addr to page-level linear address
    // keep track
    outAddr = addr;
    // compute relative page offset inside block (optional)
    // current_page_offset can be incremented by caller
    return addr.get_linear_address();
}

enum status FtlImpl_MNftl::read(Event &event)
{
    controller.stats.numFTLRead++;

    ulong lpn = event.get_logical_address();
    uint lbn  = lpn / P;
    uint bo   = lpn % P;

    // Step 2: PMD_INDEX, MAP_SLOT
    uint pmd_index = bo / Q;
    uint map_slot  = bo % Q;

    // No mapping exists
    if (PMD.find(lbn) == PMD.end())
    {
        event.set_noop(true);
        event.set_address(Address(0, PAGE));
        return controller.issue(event);
    }

    // Step 2: tempPPN ← PPN_<PMD_INDEX>
    long anchor_ppn = PMD[lbn][pmd_index];
    if (anchor_ppn == -1)
    {
        event.set_noop(true);
        event.set_address(Address(0, PAGE));
        return controller.issue(event);
    }

    // Step 3: Retrieve PMT_<PMD_INDEX> from OOB of tempPPN
    event.incr_time_taken(OOB_READ_DELAY);

    // Step 5: PPN ← PMT_<PMD_INDEX>[MAP_SLOT]
    long ppn = PMT[lbn][pmd_index][map_slot];
    if (ppn == -1)
    {
        event.set_noop(true);
        event.set_address(Address(0, PAGE));
        return controller.issue(event);
    }

    // Step 6: Retrieve data from the PPN
    event.set_address(Address((ulong)ppn, PAGE));
    return controller.issue(event);
}


enum status FtlImpl_MNftl::write(Event &event)
{
    controller.stats.numFTLWrite++;

    ulong lpn = event.get_logical_address();
    uint lbn = lpn / P;
    uint bo  = lpn % P;

    // Step 2~14: check current block, allocate if full / none
    if (!has_current_block || current_page_offset == P)
    {
        // In the paper: if no usable blocks trigger GC, else allocate.
        // FlashSim/Block_manager handles GC internally when free block is needed.
        allocate_new_current_block(event);
    }

    // Step 16 & 20: compute PMD_INDEX and MAP_SLOT
    uint pmd_index = bo / Q;
    uint map_slot  = bo % Q;

    // Step 15: ensure PMD / PMT structures exist for this LBN
    if (PMD.find(lbn) == PMD.end())
    {
        PMD[lbn] = std::vector<long>(num_pmd, -1);
    }
    if (PMT.find(lbn) == PMT.end())
    {
        PMT[lbn] = std::vector< std::vector<long> >(num_pmd);
        for (uint i = 0; i < num_pmd; i++)
            PMT[lbn][i] = std::vector<long>(Q, -1);
    }

    // Step 17~19: if previous anchor exists, read PMT from its OOB (simulate)
    long anchor_ppn = PMD[lbn][pmd_index];
    if (anchor_ppn != -1)
    {
        event.incr_time_taken(OOB_READ_DELAY);
        // Actual PMT content already in PMT[lbn][pmd_index]
    }

    // Step 7 or 13: allocate next free page in current block
    Address newPageAddr;
    ulong new_ppn = alloc_page_in_current_block(event, newPageAddr);
    current_page_offset++;

    // If this logical page had been previously mapped (old ppn), mark replace
    long old_ppn = PMT[lbn][pmd_index][map_slot];
    if (old_ppn != -1)
    {
        // mark the old ppn page as to be replaced (invalidated)
        // event later uses replace_address; mimic BD-DFTL style
        event.set_replace_address(Address((ulong)old_ppn, PAGE));
    }

    // Step 21: Update PMT slot
    PMT[lbn][pmd_index][map_slot] = (long)new_ppn;
    PMD[lbn][pmd_index] = (long)new_ppn;

    // Step 22: write data to new_ppn
    event.set_address(newPageAddr);
    // rely on controller.issue to perform write
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
        PMT[lbn][pmd_index][map_slot] = -1;
    }

    event.set_noop(true);
    event.set_address(Address(0, PAGE));

    return controller.issue(event);
}

/* ---------- MNFTL garbage collection: cleanup_block (Algorithm 3) ---------- */
void FtlImpl_MNftl::cleanup_block(Event &event, Block *block)
{
    /*
     * For each valid page in victim block:
     *   1) read old data
     *   2) allocate new page in current block (if needed)
     *   3) write data to new page, set replace_address to old page
     *   4) update mapping (PMT entry + PMD anchor) accordingly
     * Finally erase victim block.
     *
     * Note: Mapping update requires scanning PMD/PMT to locate the old_ppn.
     *       Could be optimized with reverse lookup, but here kept simple.
     */
    
    // Postponed GC (Section 3.3.1):
    // cost = N * T_rdoob + S * (T_rdpg + T_wrpg) + T_er
    event.incr_time_taken(num_pmd * OOB_READ_DELAY);

    for (uint i = 0; i < BLOCK_SIZE; i++)
    {
        if (block->get_state(i) != VALID)
            continue;

        ulong old_ppn = block->get_physical_address() + i;

        /* 1. Read old data */
        Event readEv = Event(READ, event.get_logical_address(), 1, event.get_start_time());
        readEv.set_address(Address(old_ppn, PAGE));
        controller.issue(readEv);

        /* 2. Ensure current block exists & not full */
        if (!has_current_block || current_page_offset == P)
        {
            allocate_new_current_block(event);
        }

        /* 2b. Allocate new page in current block */
        Address newPageAddr;
        ulong new_ppn = alloc_page_in_current_block(event, newPageAddr);
        current_page_offset++;

        /* 3. Write data to new page */
        Event writeEv = Event(WRITE, event.get_logical_address(), 1,
                              event.get_start_time() + readEv.get_time_taken());
        writeEv.set_address(newPageAddr);
        writeEv.set_replace_address(Address(old_ppn, PAGE));
        // copy payload from old_ppn
        writeEv.set_payload((char*)page_data + old_ppn * PAGE_SIZE);
        controller.issue(writeEv);
        
        controller.stats.valid_page_copies++;
        event.incr_time_taken(readEv.get_time_taken() + writeEv.get_time_taken());

        /* 4. Update mapping table: find where old_ppn appears */
        // scan all LBNs / PMDs / PMTs to find old_ppn
        for (auto &lbn_pair : PMT)
        {
            uint lbn_scan = lbn_pair.first;
            auto &pmts = lbn_pair.second;          // vector< vector<long> >

            for (uint idx = 0; idx < pmts.size(); idx++)
            {
                auto &pmt = pmts[idx];
                for (uint slot = 0; slot < pmt.size(); slot++)
                {
                    if (pmt[slot] == (long)old_ppn)
                    {
                        // update mapping entry to new_ppn
                        pmt[slot] = (long)new_ppn;

                        // update PMD anchor for this PMT index
                        PMD[lbn_scan][idx] = (long)new_ppn;

                        // once found, break inner loops
                        goto mapping_updated;
                    }
                }
            }
        }
mapping_updated: ;

        // statistics
        controller.stats.numFTLRead++;
        controller.stats.numFTLWrite++;
        controller.stats.numWLRead++;
        controller.stats.numWLWrite++;
    }

    /* 5. Erase victim block */
    Event eraseEv = Event(ERASE, event.get_logical_address(), 1,
                          event.get_start_time() + event.get_time_taken());
    eraseEv.set_address(Address(block->get_physical_address(), PAGE));
    controller.issue(eraseEv);
    controller.stats.numFTLErase++;
}