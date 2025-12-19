/* Copyright 2009, 2010 Brendan Tauras */

/* ssd.h is part of FlashSim. */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* ssd.h
 * Brendan Tauras 2010-07-16
 * Main SSD header file
 * 	Lists definitions of all classes, structures,
 * 		typedefs, and constants used in ssd namespace
 *		Controls options, such as debug asserts and test code insertions
 */

#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <queue>
#include <map>
#include <algorithm>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/random_access_index.hpp>
 
#ifndef _SSD_H
#define _SSD_H

namespace ssd {

#define MEM_ERR -1
#define FILE_ERR -2
#define NDEBUG

typedef unsigned int uint;
typedef unsigned long ulong;

void load_entry(char *name, double value, uint line_number);
void load_config(void);
void print_config(FILE *stream);

extern const double RAM_READ_DELAY;
extern const double RAM_WRITE_DELAY;
extern const double BUS_CTRL_DELAY;
extern const double BUS_DATA_DELAY;
extern const uint BUS_MAX_CONNECT;
extern const double BUS_CHANNEL_FREE_FLAG;
extern const uint BUS_TABLE_SIZE;
extern const uint SSD_SIZE;
extern const uint PACKAGE_SIZE;
extern const uint DIE_SIZE;
extern const uint PLANE_SIZE;
extern const double PLANE_REG_READ_DELAY;
extern const double PLANE_REG_WRITE_DELAY;
extern const uint BLOCK_SIZE;
extern const uint BLOCK_ERASES;
extern const double BLOCK_ERASE_DELAY;
extern const double PAGE_READ_DELAY;
extern const double PAGE_WRITE_DELAY;
extern const uint PAGE_SIZE;
extern const bool PAGE_ENABLE_DATA;
extern const uint MAP_DIRECTORY_SIZE;
extern const uint FTL_IMPLEMENTATION;
extern const uint BAST_LOG_BLOCK_LIMIT;
extern const uint FAST_LOG_BLOCK_LIMIT;
extern const uint CACHE_DFTL_LIMIT;
extern const uint PARALLELISM_MODE;
extern const uint VIRTUAL_BLOCK_SIZE;
extern const uint VIRTUAL_PAGE_SIZE;
extern const uint NUMBER_OF_ADDRESSABLE_BLOCKS;
extern const uint RAID_NUMBER_OF_PHYSICAL_SSDS;
extern void *page_data;
extern void *global_buffer;

enum page_state{EMPTY, VALID, INVALID};
enum block_state{FREE, ACTIVE, INACTIVE};
enum event_type{READ, WRITE, ERASE, MERGE, TRIM};
enum status{FAILURE, SUCCESS};
enum address_valid{NONE, PACKAGE, DIE, PLANE, BLOCK, PAGE};
enum block_type {LOG, DATA, LOG_SEQ};
enum ftl_implementation {IMPL_PAGE, IMPL_BAST, IMPL_FAST, IMPL_DFTL, IMPL_BIMODAL, IMPL_MNFTL};

#define BOOST_MULTI_INDEX_ENABLE_SAFE_MODE 1

class Address;
class Stats;
class Event;
class Channel;
class Bus;
class Page;
class Block;
class Plane;
class Die;
class Package;
class Garbage_Collector;
class Wear_Leveler;
class Block_manager;
class FtlParent;
class FtlImpl_Page;
class FtlImpl_Bast;
class FtlImpl_Fast;
class FtlImpl_DftlParent;
class FtlImpl_Dftl;
class FtlImpl_BDftl;
class FtlImpl_Mnftl;
class Ram;
class Controller;
class Ssd;

class Address
{
public:
    uint package;
    uint die;
    uint plane;
    uint block;
    uint page;
    ulong real_address;
    enum address_valid valid;
    Address(void);
    Address(const Address &address);
    Address(const Address *address);
    Address(uint package, uint die, uint plane, uint block, uint page, enum address_valid valid);
    Address(uint address, enum address_valid valid);
    ~Address();
    enum address_valid check_valid(uint ssd_size = SSD_SIZE, uint package_size = PACKAGE_SIZE, uint die_size = DIE_SIZE, uint plane_size = PLANE_SIZE, uint block_size = BLOCK_SIZE);
    enum address_valid compare(const Address &address) const;
    void print(FILE *stream = stdout);
    void operator+(int);
    void operator+(uint);
    Address &operator+=(const uint rhs);
    Address &operator=(const Address &rhs);
    void set_linear_address(ulong address, enum address_valid valid);
    void set_linear_address(ulong address);
    ulong get_linear_address() const;
};

class Stats
{
public:
    long numFTLRead;
    long numFTLWrite;
    long numFTLErase;
    long numFTLTrim;
    long numGCRead;
    long numGCWrite;
    long numGCErase;
    long numWLRead;
    long numWLWrite;
    long numWLErase;
    long numLogMergeSwitch;
    long numLogMergePartial;
    long numLogMergeFull;
    long numPageBlockToPageConversion;
    long numCacheHits;
    long numCacheFaults;
    long numMemoryTranslation;
    long numMemoryCache;
    long numMemoryRead;
    long numMemoryWrite;
    double translation_overhead() const;
    double variance_of_io() const;
    double cache_hit_ratio() const;
    Stats(void);
    void print_statistics();
    void reset_statistics();
    void write_statistics(FILE *stream);
    void write_header(FILE *stream);
private:
    void reset();
};

class LogPageBlock
{
public:
    LogPageBlock(void);
    ~LogPageBlock(void);
    int *pages;
    long *aPages;
    Address address;
    int numPages;
    LogPageBlock *next;
    bool operator() (const ssd::LogPageBlock& lhs, const ssd::LogPageBlock& rhs) const;
    bool operator() (const ssd::LogPageBlock*& lhs, const ssd::LogPageBlock*& rhs) const;
};

class Event 
{
public:
    Event(enum event_type type, ulong logical_address, uint size, double start_time);
    ~Event(void);
    void consolidate_metaevent(Event &list);
    ulong get_logical_address(void) const;
    const Address &get_address(void) const;
    const Address &get_merge_address(void) const;
    const Address &get_log_address(void) const;
    const Address &get_replace_address(void) const;
    uint get_size(void) const;
    enum event_type get_event_type(void) const;
    double get_start_time(void) const;
    double get_time_taken(void) const;
    double get_bus_wait_time(void) const;
    bool get_noop(void) const;
    Event *get_next(void) const;
    void set_address(const Address &address);
    void set_merge_address(const Address &address);
    void set_log_address(const Address &address);
    void set_replace_address(const Address &address);
    void set_next(Event &next);
    void set_payload(void *payload);
    void set_event_type(const enum event_type &type);
    void set_noop(bool value);
    void *get_payload(void) const;
    double incr_bus_wait_time(double time);
    double incr_time_taken(double time_incr);
    void print(FILE *stream = stdout);
private:
    double start_time;
    double time_taken;
    double bus_wait_time;
    enum event_type type;
    ulong logical_address;
    Address address;
    Address merge_address;
    Address log_address;
    Address replace_address;
    uint size;
    void *payload;
    Event *next;
    bool noop;
};

class Channel
{
public:
    Channel(double ctrl_delay = BUS_CTRL_DELAY, double data_delay = BUS_DATA_DELAY, uint table_size = BUS_TABLE_SIZE, uint max_connections = BUS_MAX_CONNECT);
    ~Channel(void);
    enum status lock(double start_time, double duration, Event &event);
    enum status connect(void);
    enum status disconnect(void);
    double ready_time(void);
private:
    void unlock(double current_time);
    struct lock_times {
        double lock_time;
        double unlock_time;
    };
    static bool timings_sorter(lock_times const& lhs, lock_times const& rhs);
    std::vector<lock_times> timings;
    uint table_entries;
    uint selected_entry;
    uint num_connected;
    uint max_connections;
    double ctrl_delay;
    double data_delay;
    double ready_at;
};

class Bus
{
public:
    Bus(uint num_channels = SSD_SIZE, double ctrl_delay = BUS_CTRL_DELAY, double data_delay = BUS_DATA_DELAY, uint table_size = BUS_TABLE_SIZE, uint max_connections = BUS_MAX_CONNECT);
    ~Bus(void);
    enum status lock(uint channel, double start_time, double duration, Event &event);
    enum status connect(uint channel);
    enum status disconnect(uint channel);
    Channel &get_channel(uint channel);
    double ready_time(uint channel);
private:
    uint num_channels;
    Channel * const channels;
};

class Page 
{
public:
    Page(const Block &parent, double read_delay = PAGE_READ_DELAY, double write_delay = PAGE_WRITE_DELAY);
    ~Page(void);
    enum status _read(Event &event);
    enum status _write(Event &event);
    const Block &get_parent(void) const;
    enum page_state get_state(void) const;
    void set_state(enum page_state state);
private:
    enum page_state state;
    const Block &parent;
    double read_delay;
    double write_delay;
};

class Block 
{
public:
    long physical_address;
    uint pages_invalid;
    Block(const Plane &parent, uint size = BLOCK_SIZE, ulong erases_remaining = BLOCK_ERASES, double erase_delay = BLOCK_ERASE_DELAY, long physical_address = 0);
    ~Block(void);
    enum status read(Event &event);
    enum status write(Event &event);
    enum status replace(Event &event);
    enum status _erase(Event &event);
    const Plane &get_parent(void) const;
    uint get_pages_valid(void) const;
    uint get_pages_invalid(void) const;
    enum block_state get_state(void) const;
    enum page_state get_state(uint page) const;
    enum page_state get_state(const Address &address) const;
    double get_last_erase_time(void) const;
    double get_modification_time(void) const;
    ulong get_erases_remaining(void) const;
    uint get_size(void) const;
    enum status get_next_page(Address &address) const;
    void invalidate_page(uint page);
    long get_physical_address(void) const;
    Block *get_pointer(void);
    block_type get_block_type(void) const;
    void set_block_type(block_type value);
private:
    uint size;
    Page * const data;
    const Plane &parent;
    uint pages_valid;
    enum block_state state;
    ulong erases_remaining;
    double last_erase_time;
    double erase_delay;
    double modification_time;
    block_type btype;
};

class Plane 
{
public:
    Plane(const Die &parent, uint plane_size = PLANE_SIZE, double reg_read_delay = PLANE_REG_READ_DELAY, double reg_write_delay = PLANE_REG_WRITE_DELAY, long physical_address = 0);
    ~Plane(void);
    enum status read(Event &event);
    enum status write(Event &event);
    enum status erase(Event &event);
    enum status replace(Event &event);
    enum status _merge(Event &event);
    const Die &get_parent(void) const;
    double get_last_erase_time(const Address &address) const;
    ulong get_erases_remaining(const Address &address) const;
    void get_least_worn(Address &address) const;
    uint get_size(void) const;
    enum page_state get_state(const Address &address) const;
    enum block_state get_block_state(const Address &address) const;
    void get_free_page(Address &address) const;
    ssd::uint get_num_free(const Address &address) const;
    ssd::uint get_num_valid(const Address &address) const;
    ssd::uint get_num_invalid(const Address &address) const;
    Block *get_block_pointer(const Address & address);
private:
    void update_wear_stats(void);
    enum status get_next_page(void);
    uint size;
    Block * const data;
    const Die &parent;
    uint least_worn;
    ulong erases_remaining;
    double last_erase_time;
    double reg_read_delay;
    double reg_write_delay;
    Address next_page;
    uint free_blocks;
};

class Die 
{
public:
    Die(const Package &parent, Channel &channel, uint die_size = DIE_SIZE, long physical_address = 0);
    ~Die(void);
    enum status read(Event &event);
    enum status write(Event &event);
    enum status erase(Event &event);
    enum status replace(Event &event);
    enum status merge(Event &event);
    enum status _merge(Event &event);
    const Package &get_parent(void) const;
    double get_last_erase_time(const Address &address) const;
    ulong get_erases_remaining(const Address &address) const;
    void get_least_worn(Address &address) const;
    enum page_state get_state(const Address &address) const;
    enum block_state get_block_state(const Address &address) const;
    void get_free_page(Address &address) const;
    ssd::uint get_num_free(const Address &address) const;
    ssd::uint get_num_valid(const Address &address) const;
    ssd::uint get_num_invalid(const Address &address) const;
    Block *get_block_pointer(const Address & address);
private:
    void update_wear_stats(const Address &address);
    uint size;
    Plane * const data;
    const Package &parent;
    Channel &channel;
    uint least_worn;
    ulong erases_remaining;
    double last_erase_time;
};

class Package 
{
public:
    Package (const Ssd &parent, Channel &channel, uint package_size = PACKAGE_SIZE, long physical_address = 0);
    ~Package ();
    enum status read(Event &event);
    enum status write(Event &event);
    enum status erase(Event &event);
    enum status replace(Event &event);
    enum status merge(Event &event);
    const Ssd &get_parent(void) const;
    double get_last_erase_time (const Address &address) const;
    ulong get_erases_remaining (const Address &address) const;
    void get_least_worn (Address &address) const;
    enum page_state get_state(const Address &address) const;
    enum block_state get_block_state(const Address &address) const;
    void get_free_page(Address &address) const;
    ssd::uint get_num_free(const Address &address) const;
    ssd::uint get_num_valid(const Address &address) const;
    ssd::uint get_num_invalid(const Address &address) const;
    Block *get_block_pointer(const Address & address);
private:
    void update_wear_stats (const Address &address);
    uint size;
    Die * const data;
    const Ssd &parent;
    uint least_worn;
    ulong erases_remaining;
    double last_erase_time;
};

class Garbage_collector 
{
public:
    Garbage_collector(FtlParent &ftl);
    ~Garbage_collector(void);
private:
    void clean(Address &address);
};

class Wear_leveler 
{
public:
    Wear_leveler(FtlParent &FTL);
    ~Wear_leveler(void);
    enum status insert(const Address &address);
};

class Block_manager
{
public:
    Block_manager(FtlParent *ftl);
    ~Block_manager(void);
    Address get_free_block(Event &event);
    Address get_free_block(block_type btype, Event &event);
    void invalidate(Address address, block_type btype);
    void print_statistics();
    void insert_events(Event &event);
    void promote_block(block_type to_type);
    bool is_log_full();
    void erase_and_invalidate(Event &event, Address &address, block_type btype);
    int get_num_free_blocks();
    void update_block(Block * b);
    static Block_manager *instance();
    static void instance_initialize(FtlParent *ftl);
    static Block_manager *inst;
    void cost_insert(Block *b);
    void print_cost_status();
private:
    void get_page_block(Address &address, Event &event);
    static bool block_comparitor_simple (Block const *x,Block const *y);
    FtlParent *ftl;
    ulong data_active;
    ulong log_active;
    ulong logseq_active;
    ulong max_log_blocks;
    ulong max_blocks;
    ulong max_map_pages;
    ulong map_space_capacity;
    typedef boost::multi_index_container<
            Block*,
            boost::multi_index::indexed_by<
                boost::multi_index::random_access<>,
                boost::multi_index::ordered_non_unique<BOOST_MULTI_INDEX_MEMBER(Block,uint,pages_invalid) >
          >
        > active_set;
    typedef active_set::nth_index<0>::type ActiveBySeq;
    typedef active_set::nth_index<1>::type ActiveByCost;
    active_set active_cost;
    std::vector<Block*> active_list;
    std::vector<Block*> free_list;
    std::vector<Block*> invalid_list;
    ulong directoryCurrentPage;
    ulong directoryCachedPage;
    ulong simpleCurrentFree;
    uint num_insert_events;
    uint current_writing_block;
    bool inited;
    bool out_of_blocks;
};

class FtlParent
{
public:
    FtlParent(Controller &controller);
    virtual ~FtlParent () {};
    virtual enum status read(Event &event) = 0;
    virtual enum status write(Event &event) = 0;
    virtual enum status trim(Event &event) = 0;
    virtual void cleanup_block(Event &event, Block *block);
    virtual void print_ftl_statistics();
    friend class Block_manager;
    ulong get_erases_remaining(const Address &address) const;
    void get_least_worn(Address &address) const;
    enum page_state get_state(const Address &address) const;
    enum block_state get_block_state(const Address &address) const;
    Block *get_block_pointer(const Address & address);
    Address resolve_logical_address(unsigned int logicalAddress);
protected:
    Controller &controller;
};

class FtlImpl_Page : public FtlParent
{
public:
    FtlImpl_Page(Controller &controller);
    ~FtlImpl_Page();
    enum status read(Event &event);
    enum status write(Event &event);
    enum status trim(Event &event);
private:
    ulong currentPage;
    ulong numPagesActive;
    bool *trim_map;
    long *map;
};

class FtlImpl_Bast : public FtlParent
{
public:
    FtlImpl_Bast(Controller &controller);
    ~FtlImpl_Bast();
    enum status read(Event &event);
    enum status write(Event &event);
    enum status trim(Event &event);
private:
    std::map<long, LogPageBlock*> log_map;
    long *data_list;
    void dispose_logblock(LogPageBlock *logBlock, long lba);
    void allocate_new_logblock(LogPageBlock *logBlock, long lba, Event &event);
    bool is_sequential(LogPageBlock* logBlock, long lba, Event &event);
    bool random_merge(LogPageBlock *logBlock, long lba, Event &event);
    void update_map_block(Event &event);
    void print_ftl_statistics();
    int addressShift;
    int addressSize;
};

class FtlImpl_Fast : public FtlParent
{
public:
    FtlImpl_Fast(Controller &controller);
    ~FtlImpl_Fast();
    enum status read(Event &event);
    enum status write(Event &event);
    enum status trim(Event &event);
private:
    void initialize_log_pages();
    std::map<long, LogPageBlock*> log_map;
    long *data_list;
    bool *pin_list;
    bool write_to_log_block(Event &event, long logicalBlockAddress);
    void switch_sequential(Event &event);
    void merge_sequential(Event &event);
    bool random_merge(LogPageBlock *logBlock, Event &event);
    void update_map_block(Event &event);
    void print_ftl_statistics();
    long sequential_logicalblock_address;
    Address sequential_address;
    uint sequential_offset;
    uint log_page_next;
    LogPageBlock *log_pages;
    int addressShift;
    int addressSize;
};

class FtlImpl_DftlParent : public FtlParent
{
public:
    FtlImpl_DftlParent(Controller &controller);
    ~FtlImpl_DftlParent();
    virtual enum status read(Event &event) = 0;
    virtual enum status write(Event &event) = 0;
    virtual enum status trim(Event &event) = 0;

protected:  
    struct MPage {
        long vpn;
        long ppn;
        double create_ts;
        double modified_ts;
        double last_visited_time;
        bool cached;
        MPage(long vpn);
    };

    long int cmt;
    static double mpage_last_visited_time_compare(const MPage& mpage);

    typedef boost::multi_index_container<
        FtlImpl_DftlParent::MPage,
            boost::multi_index::indexed_by<
                boost::multi_index::random_access<>,
                boost::multi_index::ordered_non_unique<boost::multi_index::global_fun<const FtlImpl_DftlParent::MPage&,double,&FtlImpl_DftlParent::mpage_last_visited_time_compare> >
          >
        > trans_set;

    typedef trans_set::nth_index<0>::type MpageByID;
    typedef trans_set::nth_index<1>::type MpageByLastVisited;

    trans_set trans_map;
    long *reverse_trans_map;

    void consult_GTD(long dppn, Event &event);
    void reset_MPage(FtlImpl_DftlParent::MPage &mpage);
    void resolve_mapping(Event &event, bool isWrite);
    void update_translation_map(FtlImpl_DftlParent::MPage &mpage, long ppn);
    bool lookup_CMT(long dlpn, Event &event);
    long get_free_data_page(Event &event);
    long get_free_data_page(Event &event, bool insert_events);
    void evict_page_from_cache(Event &event);
    void evict_specific_page_from_cache(Event &event, long lba);

    int addressPerPage;
    int addressSize;
    uint totalCMTentries;

    long currentDataPage;
    long currentTranslationPage;
};

class FtlImpl_Dftl : public FtlImpl_DftlParent
{
public:
    FtlImpl_Dftl(Controller &controller);
    ~FtlImpl_Dftl();
    enum status read(Event &event);
    enum status write(Event &event);
    enum status trim(Event &event);
    void cleanup_block(Event &event, Block *block);
    void print_ftl_statistics();
};

class FtlImpl_BDftl : public FtlImpl_DftlParent
{
public:
    FtlImpl_BDftl(Controller &controller);
    ~FtlImpl_BDftl();
    enum status read(Event &event);
    enum status write(Event &event);
    enum status trim(Event &event);
    void cleanup_block(Event &event, Block *block);
private:
    struct BPage {
        uint pbn;
        unsigned char nextPage;
        bool optimal;
        BPage();
    };
    BPage *block_map;
    bool *trim_map;
    std::queue<Block*> blockQueue;
    Block* inuseBlock;
    bool block_next_new();
    long get_free_biftl_page(Event &event);
    void print_ftl_statistics();
};

class Ram 
{
public:
    Ram(double read_delay = RAM_READ_DELAY, double write_delay = RAM_WRITE_DELAY);
    ~Ram(void);
    enum status read(Event &event);
    enum status write(Event &event);
private:
    double read_delay;
    double write_delay;
};

class Controller 
{
public:
    Controller(Ssd &parent);
    ~Controller(void);
    enum status event_arrive(Event &event);
    friend class FtlParent;
    friend class FtlImpl_Page;
    friend class FtlImpl_Bast;
    friend class FtlImpl_Fast;
    friend class FtlImpl_DftlParent;
    friend class FtlImpl_Dftl;
    friend class FtlImpl_BDftl;
    friend class FtlImpl_Mnftl;
    friend class Block_manager;
    Stats stats;
    void print_ftl_statistics();
    const FtlParent &get_ftl(void) const;
private:
    enum status issue(Event &event_list);
    void translate_address(Address &address);
    ssd::ulong get_erases_remaining(const Address &address) const;
    void get_least_worn(Address &address) const;
    double get_last_erase_time(const Address &address) const;
    enum page_state get_state(const Address &address) const;
    enum block_state get_block_state(const Address &address) const;
    void get_free_page(Address &address) const;
    ssd::uint get_num_free(const Address &address) const;
    ssd::uint get_num_valid(const Address &address) const;
    ssd::uint get_num_invalid(const Address &address) const;
    Block *get_block_pointer(const Address & address);
    Ssd &ssd;
    FtlParent *ftl;
};

class Ssd 
{
public:
    Ssd (uint ssd_size = SSD_SIZE);
    ~Ssd(void);
    double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time);
    double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer);
    void *get_result_buffer();
    friend class Controller;
    void print_statistics();
    void reset_statistics();
    void write_statistics(FILE *stream);
    void write_header(FILE *stream);
    const Controller &get_controller(void) const;
    void print_ftl_statistics();
    double ready_at(void);
private:
    enum status read(Event &event);
    enum status write(Event &event);
    enum status erase(Event &event);
    enum status merge(Event &event);
    enum status replace(Event &event);
    enum status merge_replacement_block(Event &event);
    ulong get_erases_remaining(const Address &address) const;
    void update_wear_stats(const Address &address);
    void get_least_worn(Address &address) const;
    double get_last_erase_time(const Address &address) const;   
    Package &get_data(void);
    enum page_state get_state(const Address &address) const;
    enum block_state get_block_state(const Address &address) const;
    void get_free_page(Address &address) const;
    ssd::uint get_num_free(const Address &address) const;
    ssd::uint get_num_valid(const Address &address) const;
    ssd::uint get_num_invalid(const Address &address) const;
    Block *get_block_pointer(const Address & address);
    uint size;
    Controller controller;
    Ram ram;
    Bus bus;
    Package * const data;
    ulong erases_remaining;
    ulong least_worn;
    double last_erase_time;
};

class RaidSsd
{
public:
    RaidSsd (uint ssd_size = SSD_SIZE);
    ~RaidSsd(void);
    double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time);
    double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer);
    void *get_result_buffer();
    friend class Controller;
    void print_statistics();
    void reset_statistics();
    void write_statistics(FILE *stream);
    void write_header(FILE *stream);
    const Controller &get_controller(void) const;
    void print_ftl_statistics();
private:
    uint size;
    Ssd *Ssds;
};

class FtlImpl_Mnftl : public FtlImpl_DftlParent
{
public:
    FtlImpl_Mnftl(Controller &controller);
    ~FtlImpl_Mnftl();
    enum status read(Event &event);
    enum status write(Event &event);
    enum status trim(Event &event);
    void resolve_mapping_mn(Event &event, bool write);
};

} /* end namespace ssd */

#endif
