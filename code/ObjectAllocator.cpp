/**
 * @file ObjectAllocator.cpp
 * @author Matthias Ong Si En (ong.s@digipen.edu)
 * @par Course: CSD2181
 * @par Assignment #1    
 * @brief This file implements the ObjectAllocator class, which is a memory pool allocator that
 * provides clients with fixed-sized memory blocks to use.
 * @date 2022-01-29
 * @copyright Copyright (C) 2022 DigiPen Institute of Technology.
 * Reproduction or disclosure of this file or its contents without the
 * prior written consent of DigiPen Institute of Technology is prohibited.
 */

#include "ObjectAllocator.h"
#include <cstring>

#define PTR_SIZE sizeof(void *)

/**
 * @brief Function that calculates the new size required after accounting for alignment
 * 
 * @param sz Current unaligned size
 * @param alignment Alignment required
 * @return New size after alignment
 */
size_t align(size_t sz, size_t alignment)
{
    if (alignment == 0)
        return sz; //no alignment
    size_t remainder = sz % alignment == 0 ? 0 : 1;
    return alignment * ((sz / alignment) + remainder); //return size after alignment
}

/**
 * @brief Construct a new Object Allocator:: Object Allocator object, initialising members,
 *  calculating sizes of the components and constructs a starting page
 * 
 * @param ObjectSize size of each object to be used in the allocator
 * @param config the information needed for the allocator 
 */
ObjectAllocator::ObjectAllocator(size_t ObjectSize, const OAConfig &config)
    : configuration{config}
{
    PageList_ = nullptr;
    FreeList_ = nullptr;
    //calculate and inits OAStats
    stats.ObjectSize_ = ObjectSize;
    size_t unalignedPageHeader = PTR_SIZE + config.HBlockInfo_.size_ + config.PadBytes_;
    pageHeader = align(unalignedPageHeader, config.Alignment_); //header of the page NOT blocks
    configuration.LeftAlignSize_ = static_cast<unsigned int>(pageHeader - unalignedPageHeader);
    dataSize = align(ObjectSize + config.PadBytes_ * 2 + config.HBlockInfo_.size_, config.Alignment_);
    stats.PageSize_ = pageHeader + dataSize * (config.ObjectsPerPage_ - 1) + ObjectSize + config.PadBytes_;
    totalDataSize = dataSize * (config.ObjectsPerPage_ - 1) + ObjectSize + config.PadBytes_;

    //Calculates interAlignment and inits config
    size_t midBlockSize = ObjectSize + config.PadBytes_ * 2 + static_cast<size_t>(config.HBlockInfo_.size_);
    configuration.InterAlignSize_ = static_cast<unsigned int>(align(midBlockSize, configuration.Alignment_) - midBlockSize);

    //Allocates a starting page
    AllocateNewPage(PageList_);
}

/**
 * @brief Constructs a new page
 * 
 * @param page Previous page
 * @exception OAException E_NO_PAGES Throws an exception if the construction fails. (Memory allocation problem)
 * @exception OAException E_NO_MEMORY No memory
 */
void ObjectAllocator::AllocateNewPage(GenericObject *&page)
{
    if (stats.PagesInUse_ >= configuration.MaxPages_)
        throw OAException(OAException::OA_EXCEPTION::E_NO_PAGES, "Exceeded max pages!");
    else
    {
        // Allocate new page.
        GenericObject *newPage = nullptr;
        try
        {
            newPage = reinterpret_cast<GenericObject *>(new unsigned char[stats.PageSize_ + PTR_SIZE]());
            ++stats.PagesInUse_;
            memset(newPage, 0, stats.PageSize_); //Avoid memory error
        }
        catch (std::bad_alloc &exception)
        {
            throw OAException(OAException::OA_EXCEPTION::E_NO_MEMORY, "Out of memory!");
        }

        if (configuration.DebugOn_)
        {
            memset(newPage, ALIGN_PATTERN, stats.PageSize_); //Initialise everything as alignment first
        }
        newPage->Next = page; //newPage next points to the prev page (newPage is now at the front)
        PageList_ = newPage;  //update pageList

        unsigned char *pageStartAddress = reinterpret_cast<unsigned char *>(newPage);
        //memset(pageStartAddress + PTR_SIZE, ALIGN_PATTERN, configuration.LeftAlignSize_);//after pointer
        unsigned char *dataStartAddress = pageStartAddress + pageHeader; //Start of the DATA

        // For each start of the data...
        for (; static_cast<unsigned int>(dataStartAddress - pageStartAddress) < stats.PageSize_; //Loop until the whole page is initialised
             dataStartAddress += dataSize)
        {

            GenericObject *dataAddress = reinterpret_cast<GenericObject *>(dataStartAddress); //Casting each data block to GenericObject *
            //TODO initialize the actual header block to zeros when you create a page (faq)
            unsigned char *headerStart = reinterpret_cast<unsigned char *>(dataAddress) - configuration.PadBytes_ - configuration.HBlockInfo_.size_; //before padding block
            memset(headerStart, 0, configuration.HBlockInfo_.size_);

            //std::cout << "TEST!\n";
            AddToFreeList(dataAddress); // Put to free list.
            if (this->configuration.DebugOn_)
            {
                // Update padding sig
                memset(reinterpret_cast<unsigned char *>(dataAddress) + PTR_SIZE, UNALLOCATED_PATTERN, stats.ObjectSize_ - PTR_SIZE);
                memset(reinterpret_cast<unsigned char *>(dataAddress) - configuration.PadBytes_, PAD_PATTERN, configuration.PadBytes_);
                memset(reinterpret_cast<unsigned char *>(dataAddress) + stats.ObjectSize_, PAD_PATTERN, configuration.PadBytes_);
            }
        }
    }
}

/**
 * @brief Adds obj to front of freelist
 * 
 * @param obj Object to add to freelist to show that it is free for allocation.
 */
void ObjectAllocator::AddToFreeList(GenericObject *obj)
{
    GenericObject *temp = FreeList_;
    FreeList_ = obj;
    obj->Next = temp;
    stats.FreeObjects_++;
}

/**
 * @brief Allocates memory to the client by taking an object from the FreeList_
 * 
 * @param label Label for external header if required
 * @return void* Pointer to memory for client
 * @exception OAException E_NO_MEMORY No memory
 */
void *ObjectAllocator::Allocate(const char *label)
{
    //std::cout << stats.Allocations_ << std::endl;
    if (configuration.UseCPPMemManager_) //Use new
    {
        try
        {
            unsigned char *newObj = new unsigned char[stats.ObjectSize_];
            ++stats.ObjectsInUse_;
            if (stats.ObjectsInUse_ > stats.MostObjects_)
                stats.MostObjects_ = stats.ObjectsInUse_;
            ++stats.Allocations_;
            --stats.FreeObjects_;
            return reinterpret_cast<void *>(newObj);
        }
        catch (std::bad_alloc &)
        {
            throw OAException(OAException::E_NO_MEMORY, "Out of memory!");
        }
    }

    //Use our allocator with pages
    if (!FreeList_) //If ran out of free space/nullptr
    {

        AllocateNewPage(PageList_);
    }

    void *startAddressOfObject = FreeList_; // Give address of available free space.
    FreeList_ = FreeList_->Next;            //Update next available space

    if (configuration.DebugOn_)
    {
        memset(startAddressOfObject, ALLOCATED_PATTERN, stats.ObjectSize_);
    }

    ++stats.ObjectsInUse_;
    ++stats.Allocations_;
    --stats.FreeObjects_;
    if (stats.ObjectsInUse_ > stats.MostObjects_)
        stats.MostObjects_ = stats.ObjectsInUse_;

    //Update header blocks to client
    if (configuration.HBlockInfo_.type_ != OAConfig::HBLOCK_TYPE::hbNone)
    {
        unsigned char *headerStart = reinterpret_cast<unsigned char *>(startAddressOfObject) - configuration.PadBytes_ - configuration.HBlockInfo_.size_; //before padding block
        if (configuration.HBlockInfo_.type_ == OAConfig::HBLOCK_TYPE::hbBasic)
        {
            unsigned int *allocationNumber = reinterpret_cast<unsigned int *>(headerStart);
            *allocationNumber = stats.Allocations_;                                        //Allocation number
            unsigned char *flag = reinterpret_cast<unsigned char *>(allocationNumber + 1); //Move pointer after allocation number
            *flag = true;                                                                  //Flag value is not free
        }
        else if (configuration.HBlockInfo_.type_ == OAConfig::HBLOCK_TYPE::hbExtended)
        {
            headerStart += static_cast<unsigned char>(configuration.HBlockInfo_.additional_); //User-defined field of x bytes
            ++(*headerStart);                                                                 //Increase block use count
            headerStart += (sizeof(char) * 2);                                                //Go to allocation number
            unsigned int *allocationNumber = reinterpret_cast<unsigned int *>(headerStart);
            *allocationNumber = stats.Allocations_;
            headerStart += sizeof(unsigned int);
            *headerStart = 1; //Flag value is not free
        }
        else if (configuration.HBlockInfo_.type_ == OAConfig::HBLOCK_TYPE::hbExternal)
        {
            MemBlockInfo **externalHeader = reinterpret_cast<MemBlockInfo **>(headerStart);
            try
            {
                *externalHeader = new MemBlockInfo(true, label, stats.Allocations_); //Allocate new memory block struct object for external header to the headerStart
            }
            catch (std::bad_alloc &e)
            {
                throw(OAException(OAException::E_NO_MEMORY, "External Header: Not enough memory available!"));
            }
        }
    }

    return startAddressOfObject;
}

/**
 * @brief Destroy the Object Allocator, frees up memory
 * 
 */
ObjectAllocator::~ObjectAllocator()
{
    GenericObject *page = PageList_; //first page
    while (page != nullptr)          //loop through all pages
    {
        GenericObject *nextPage = page->Next;
        unsigned char *obj = reinterpret_cast<unsigned char *>(page) + pageHeader;

        if (configuration.HBlockInfo_.type_ == OAConfig::hbExternal) //free any active external header in case Free() was not called when page is deleted
        {

            unsigned char *headerStart = reinterpret_cast<unsigned char *>(obj) - configuration.PadBytes_ - configuration.HBlockInfo_.size_;
            MemBlockInfo **externalHeader = reinterpret_cast<MemBlockInfo **>(headerStart);
            if (externalHeader)
            {
                delete *externalHeader; //label will be freed in destructor
                *externalHeader = nullptr;
                externalHeader = nullptr;
            }
        }
        delete[] reinterpret_cast<unsigned char *>(page); //delete whole page
        page = nextPage;
    }
}

/**
 * @brief Constructor for external header block
 * 
 * @param use Bool to indicate whether the header block is used
 * @param str Name of the null terminated string to be stored in the external header if any
 * @param allocs Allocation count of this block
 * @exception OAException E_NO_MEMORY No memory
 */
MemBlockInfo::MemBlockInfo(bool use, const char *str, unsigned allocs)
    : in_use{use}, label{nullptr}, alloc_num{allocs}
{
    if (str != nullptr)
    {
        try
        {
            label = new char[strlen(str) + 1](); //Allocate mem to store the string
            strcpy(label, str);
        }
        catch (std::bad_alloc &e)
        {
            throw(OAException(OAException::E_NO_MEMORY, "MemBlock Label: No memory available."));
        }
    }
}

/**
 * @brief Destroy the MemBlockInfo block and free any dynamic memory
 * 
 */
MemBlockInfo::~MemBlockInfo()
{
    delete[] label;
}

/**
 * @brief Sets DebugState
 * 
 * @param State Boolean, to use debugState or not
 */
void ObjectAllocator::SetDebugState(bool State)
{
    configuration.DebugOn_ = State;
}

/**
 * @brief Get FreeList
 * 
 * @return const void* Returns pointer to the first freeList object
 */
const void *ObjectAllocator::GetFreeList() const
{
    return FreeList_;
}

/**
 * @brief Get Page List
 * 
 * @return const void* Returns pointer to the first freeList object
 */
const void *ObjectAllocator::GetPageList() const
{
    return PageList_;
}

/**
 * @brief Get Config of the Object allocator
 * 
 * @return OAConfig Returns the config instance of the ObjectAllocator
 */
OAConfig ObjectAllocator::GetConfig() const
{
    return configuration;
}

/**
 * @brief Returns the object allocator's stats
 * 
 * @return OAStats Returns the stats of the ObjectAllocator
 */
OAStats ObjectAllocator::GetStats() const
{
    return stats;
}

/**
 * @brief Free a pointer from the client and returns it to the freelist
    Throws an exception if the the object can't be freed. (Invalid object)
 * 
 * @param obj Pointer to be freed
 * @exception OAException E_BAD_BOUNDARY Out of page boundary
 * @exception OAException E_CORRUPTED_BLOCK Corrupted block
 * @exception OAException E_MULTIPLE_FREE Multiple free
 */
void ObjectAllocator::Free(void *obj)
{
    ++stats.Deallocations_;
    --stats.ObjectsInUse_;
    if (configuration.UseCPPMemManager_)
    {
        delete[] reinterpret_cast<unsigned char *>(obj);
        return;
    }
    if (configuration.DebugOn_)
    {
        CheckPageBoundary(reinterpret_cast<unsigned char *>(obj)); //check if obj is within pages
        CheckPadding(reinterpret_cast<unsigned char *>(obj));      //check for memory overruns and underruns.
        //check for double free
        if (*(reinterpret_cast<unsigned char *>(obj) + PTR_SIZE) == FREED_PATTERN) //reason for + PTR_SIZE, in AddToFreeList(), i replaced the FREED_PATTERN ENUM with pointer data
        {
            throw OAException(OAException::E_MULTIPLE_FREE, "Multiple free!");
        }
        memset(reinterpret_cast<GenericObject *>(obj), FREED_PATTERN, stats.ObjectSize_); //Set the table as freed if no issues
    }
    if (configuration.HBlockInfo_.type_ != OAConfig::HBLOCK_TYPE::hbNone)
    {
        //free headers
        unsigned char *headerStart = reinterpret_cast<unsigned char *>(obj) - configuration.PadBytes_ - configuration.HBlockInfo_.size_;
        if (configuration.HBlockInfo_.type_ == OAConfig::HBLOCK_TYPE::hbBasic)
        {
            memset(headerStart, 0, OAConfig::BASIC_HEADER_SIZE); //Set basic block to 0
        }
        else if (configuration.HBlockInfo_.type_ == OAConfig::HBLOCK_TYPE::hbExtended)
        {
            memset(headerStart + configuration.HBlockInfo_.additional_ + sizeof(unsigned short), 0, OAConfig::BASIC_HEADER_SIZE); // Reset the basic header part of extended block to 0
        }
        else if (configuration.HBlockInfo_.type_ == OAConfig::HBLOCK_TYPE::hbExternal)
        {
            MemBlockInfo **externalHeader = reinterpret_cast<MemBlockInfo **>(headerStart);
            delete *externalHeader; //label will be freed in destructor
            *externalHeader = nullptr;
            externalHeader = nullptr;
        }
    }
    AddToFreeList(reinterpret_cast<GenericObject *>(obj)); //add back to freelist
}

/**
 * @brief Helper function to check if pointer exist inside the pages
 * 
 * @param obj Pointer to be checked
 * @exception OAException E_BAD_BOUNDARY Out of page boundary
 */
void ObjectAllocator::CheckPageBoundary(const unsigned char *obj)
{
    GenericObject *page = PageList_;
    while (page)
    {
        if ((obj >= reinterpret_cast<unsigned char *>(page) && obj < reinterpret_cast<unsigned char *>(page) + stats.PageSize_))
        {
            break; //within page
        }
        else
        {
            page = page->Next;
            if (page == nullptr)
            {
                throw OAException(OAException::E_BAD_BOUNDARY, "OUT OF PAGE BOUNDARY");
            }
        }
    }
}

/**
 * @brief Helper function to check if padding of obj data's block is corrupted
 * 
 * @param obj Object to be checked
 * @exception OAException E_CORRUPTED_BLOCK Corrupted block
 */
void ObjectAllocator::CheckPadding(const unsigned char *obj)
{
    const unsigned char *leftPadAddress = reinterpret_cast<const unsigned char *>(obj) - configuration.PadBytes_;
    const unsigned char *rightPadAddress = reinterpret_cast<const unsigned char *>(obj) + stats.ObjectSize_;
    for (unsigned int i = 0; i < configuration.PadBytes_; ++i)
    {
        if (*(leftPadAddress + i) != PAD_PATTERN) //LEFT PAD CHECK
        {
            throw OAException(OAException::E_CORRUPTED_BLOCK, "LEFT PAD CHECK FAILED: CORRUPTED BLOCK");
        }
        else
        {
            if (i == configuration.PadBytes_ - 1)
            {
                for (unsigned int j = 0; j < configuration.PadBytes_; ++j)
                {
                    if (*(rightPadAddress + j) != PAD_PATTERN) //RIGHT PAD CHECK
                    {
                        throw OAException(OAException::E_CORRUPTED_BLOCK, "RIGHT PAD CHECK FAILED: CORRUPTED BLOCK");
                    }
                }
            }
        }
    }
}

/**
 * @brief Calls the callback fn for each block still in use
 * 
 * @param fn Callback function
 * @return unsigned Number of leaks
 */
unsigned ObjectAllocator::DumpMemoryInUse(DUMPCALLBACK fn) const
{
    GenericObject *page = PageList_;
    unsigned int leaks = 0;
    while (page)
    {
        unsigned char *obj = reinterpret_cast<unsigned char *>(page) + pageHeader;
        for (unsigned int i = 0; i < configuration.ObjectsPerPage_; ++i)
        {
            //unsigned char *headerStart = reinterpret_cast<unsigned char *>(obj) - configuration.PadBytes_ - configuration.HBlockInfo_.size_;
            if (configuration.HBlockInfo_.type_ == OAConfig::HBLOCK_TYPE::hbNone)
            {
                //NOT IN TEST CASE
            }
            else if (configuration.HBlockInfo_.type_ == OAConfig::HBLOCK_TYPE::hbBasic || configuration.HBlockInfo_.type_ == OAConfig::HBLOCK_TYPE::hbExtended)
            {
                unsigned char *flag = reinterpret_cast<unsigned char *>(obj) - configuration.PadBytes_ - 1;
                //std::cout << *flag << std::endl;
                if (*flag) //If block is used, it will be leaked
                {
                    ++leaks;
                    fn(obj, stats.ObjectSize_);
                }
            }
            else if (configuration.HBlockInfo_.type_ == OAConfig::HBLOCK_TYPE::hbExternal)
            {
                //NOT IN TEST CASE
            }
            obj += dataSize;
        }
        page = page->Next; //Next page
    }
    return leaks;
}

/**
 * @brief Calls the callback fn for each block that is corrupted
 * 
 * @param fn Callback fn
 * @return unsigned Number of corrupted blocks
 */
unsigned ObjectAllocator::ValidatePages(VALIDATECALLBACK fn) const
{
    unsigned int count = 0;
    if (!configuration.DebugOn_ || configuration.PadBytes_ == 0)
        return 0;

    GenericObject *page = PageList_;
    while (page)
    {
        unsigned char *obj = reinterpret_cast<unsigned char *>(page) + pageHeader;
        for (unsigned int i = 0; i < configuration.ObjectsPerPage_; ++i)
        {
            const unsigned char *leftPadStart = reinterpret_cast<const unsigned char *>(obj) - configuration.PadBytes_;
            const unsigned char *rightPadStart = reinterpret_cast<const unsigned char *>(obj) + stats.ObjectSize_;
            for (unsigned int i = 0; i < configuration.PadBytes_; ++i)
            {
                if (*(leftPadStart + i) != PAD_PATTERN) //LEFT PAD CHECK
                {
                    count++;
                    fn(obj, stats.ObjectSize_);
                    break; //Stop checking the block, go to next object
                }
                else
                {
                    if (i == configuration.PadBytes_ - 1)
                    {
                        for (unsigned int j = 0; j < configuration.PadBytes_; ++j)
                        {
                            if (*(rightPadStart + j) != PAD_PATTERN) //RIGHT PAD CHECK
                            {
                                count++;
                                fn(obj, stats.ObjectSize_);
                                break; //Stop checking the block
                            }
                        }
                    }
                }
            }

            obj += dataSize;
        }
        page = page->Next;
    }
    return count;
}

/**
 * @brief This function frees all empty pages
 * 
 * @return unsigned Number of pages freed
 */
unsigned ObjectAllocator::FreeEmptyPages()
{
    if (!PageList_)
        return 0;

    GenericObject *page = PageList_, *prev = nullptr;
    unsigned pagesFreed = 0;
    while (page) //case if head node of pagelist contains empty page
    {
        if (IsPageEmpty(page))
        {
            PageList_ = page->Next;
            FreePage(page);
            page = PageList_;
            pagesFreed++;
        }
        else
            break;
    }
    while (page) //every other node other than head node containing empty page
    {
        if (!IsPageEmpty(page))
        {
            prev = page;
            page = page->Next;
        }
        else
        {
            prev->Next = page->Next;
            FreePage(page);
            page = prev->Next;
            pagesFreed++;
        }
    }
    return pagesFreed;
}

/**
 * @brief This function checks if every data block inside the page is empty
 * 
 * @param page Page to be checked
 * @return true Empty page
 * @return false Not empty
 */
bool ObjectAllocator::IsPageEmpty(GenericObject *page)
{
    GenericObject *freeBlock = FreeList_;
    unsigned i = 0;
    while (freeBlock)
    {
        if (reinterpret_cast<unsigned char *>(freeBlock) >= reinterpret_cast<unsigned char *>(page) && reinterpret_cast<unsigned char *>(freeBlock) < reinterpret_cast<unsigned char *>(page) + stats.PageSize_) //Check if freeBlock is within current page boundary
        {
            ++i;
            if (i >= configuration.ObjectsPerPage_) //Whole page is empty
                return true;
        }
        freeBlock = freeBlock->Next; //Next freeBlock
    }
    return false;
}

/**
 * @brief Frees all the data blocks in a given page from the free list
 * 
 * @param page Page to be freed
 */
void ObjectAllocator::FreePage(GenericObject *page)
{
    GenericObject *freeBlock = FreeList_, *prev = nullptr;
    while (freeBlock) //Deletes head node if its within the page
    {
        if ((reinterpret_cast<unsigned char *>(freeBlock) >= reinterpret_cast<unsigned char *>(page) && reinterpret_cast<unsigned char *>(freeBlock) < reinterpret_cast<unsigned char *>(page) + stats.PageSize_)) //If freeblock is within page
        {
            FreeList_ = freeBlock->Next;
            freeBlock = FreeList_;
            --stats.FreeObjects_;
        }
        else
            break;
    }

    while (freeBlock) // Delete everything other than head if its within page
    {
        if (!(reinterpret_cast<unsigned char *>(freeBlock) >= reinterpret_cast<unsigned char *>(page)                       // Search for the freeBlock to be deleted
              && reinterpret_cast<unsigned char *>(freeBlock) < reinterpret_cast<unsigned char *>(page) + stats.PageSize_)) //If freeblock is NOT within page
        {
            prev = freeBlock;            //Keep track of prev as it is needed for unlinking nodes
            freeBlock = freeBlock->Next; //Go to the next freeBlock on freeList
        }
        else
        {
            if (freeBlock == nullptr)
                break;
            prev->Next = freeBlock->Next; //Unlink node, prev node now points to the nextnextFreeBlock, unlinking the node in the middle
            freeBlock = prev->Next;       //Update current freeBlock to be the nextnextFreeBlock
            --stats.FreeObjects_;
        }
    }

    delete[] reinterpret_cast<unsigned char *>(page);
    stats.PagesInUse_--;
}