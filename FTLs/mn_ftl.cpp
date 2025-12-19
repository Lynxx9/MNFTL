#include <cstddef>
#include <new>
#include <stdio.h>
#include "../ssd.h"

using namespace ssd;

FtlImpl_Mnftl::FtlImpl_Mnftl(Controller &controller):
    FtlImpl_DftlParent(controller)
{
    printf("DEBUG: Constructor Start\n");
    printf("Using MNFTL (Strict OOB-based Mapping).\n");
    
    uint total_logical_pages = ssd::NUMBER_OF_ADDRESSABLE_BLOCKS * ssd::BLOCK_SIZE;
    
    // Safety check: Initialize the translation map if the parent class hasn't done so.
    if (trans_map.size() < total_logical_pages) {
        printf("DEBUG: Initializing trans_map for %u pages...\n", total_logical_pages);
        for (ulong i = trans_map.size(); i < total_logical_pages; i++) {
            MPage m_page(i);
            m_page.ppn = -1;       // Initialize as unmapped
            m_page.cached = false; // Not in SRAM cache initially
            trans_map.push_back(m_page);
        }
    }
}

FtlImpl_Mnftl::~FtlImpl_Mnftl(void)
{
}

enum status FtlImpl_Mnftl::trim(Event &event)
{
    // MNFTL simulation treats TRIM as a no-op for performance stats
    event.set_noop(true);
    controller.stats.numFTLTrim++;
    return controller.issue(event);
}


 //checks SRAM cache or retrieves from OOB (simulated delay).
 
void FtlImpl_Mnftl::resolve_mapping_mn(Event &event, bool write)
{
    uint dlpn = event.get_logical_address();
    
    // Boundary check
    if (dlpn >= trans_map.size()) return;

    // Access via iterator (Random Access Index 0)
    trans_set::iterator it = trans_map.begin() + dlpn;
    
    // Create a local copy to modify state
    MPage m_page = *it; 

    if (m_page.cached) {
        //  Cache Hit: Check PMD_INDEX in RAM
        m_page.last_visited_time = event.get_start_time();
        event.incr_time_taken(RAM_READ_DELAY); 
        controller.stats.numCacheHits++;
    } else {
        //  Cache Miss: Retrieve PMT from OOB
        controller.stats.numCacheFaults++; 

        // Evict if cache is full (LRU)
        if (cmt >= CACHE_DFTL_LIMIT) {
            evict_page_from_cache(event);
        }


        // This is the distinct feature of MNFTL compared to DFTL
        event.incr_time_taken(1.7); 
        
        // Update state and load into Cache
        m_page.cached = true;
        m_page.last_visited_time = event.get_start_time();
        cmt++;
        
        controller.stats.numMemoryRead++;
        controller.stats.numMemoryWrite++;
    }

    // Write back the modified copy to the container
    trans_map.replace(it, m_page); 
}

/*
 MNFTL Read
 */
enum status FtlImpl_Mnftl::read(Event &event)
{
    // Resolve mapping (Simulate OOB lookup if needed)
    resolve_mapping_mn(event, false);

    uint dlpn = event.get_logical_address();
    
    if (dlpn < trans_map.size()) {
        trans_set::iterator it = trans_map.begin() + dlpn;
        MPage current = *it;
        
        // [Alg 2: Line 6] Retrieve data from PPN
        if (current.ppn != -1) {
            event.set_address(Address(current.ppn, PAGE));
        } else {
            // Unmapped address
            event.set_address(Address(0, PAGE));
            event.set_noop(true);
        }
    } else {
        event.set_address(Address(0, PAGE));
        event.set_noop(true);
    }

    controller.stats.numFTLRead++;
    return controller.issue(event);
}

/*
MNFTL Write
 */
enum status FtlImpl_Mnftl::write(Event &event)
{
    // Allocate PPN (Handled by Block_manager)
    long free_page = get_free_data_page(event);
    
    // Ensure mapping table is loaded
    resolve_mapping_mn(event, true);
    
    uint dlpn = event.get_logical_address();
    
    if (dlpn >= trans_map.size()) return FAILURE;

    trans_set::iterator it = trans_map.begin() + dlpn;
    MPage current = *it; // Create a local copy of the mapping entry

    // Invalidate old data if it exists
    if (current.ppn != -1)
        event.set_replace_address(Address(current.ppn, PAGE));

    // Update the mapping slot in the local copy
    update_translation_map(current, free_page);
    
    // Write the updated mapping back to the container.
    trans_map.replace(it, current);

    //Atomic Write
    // Write Data and OOB (Mapping) simultaneously.
    event.set_address(Address(free_page, PAGE));
    
    controller.stats.numFTLWrite++;
    return controller.issue(event);
}
