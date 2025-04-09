#pragma once

#include <algorithm>
#include <vector>
#include <ranges>


template<class T>
struct allocInfo
{
    T* p;
    size_t count;
};


template <class T, size_t blockSize>
struct  allocatorState
{
    size_t blockIndex;
    size_t blockOffset;
    std::vector<allocInfo<T>> blocks;
    std::vector<allocInfo<T>*> sortedBlocks;
};


template <class T, size_t blockSize>
struct BlockAllocator
{
    using value_type = T;
    using is_always_equal = std::true_type;
    static constexpr size_t blockBytes = blockSize * sizeof(T);
    static size_t& blockIndex;
    static size_t& blockOffset;
    static std::vector<allocInfo<T>>& blocks;
    static std::vector<allocInfo<T>*>& sortedBlocks;


    constexpr BlockAllocator() = default;


    template <class U>
    BlockAllocator(const BlockAllocator<U, blockSize>&) {}


    [[nodiscard]] T* allocate(size_t elements)
    {
        if (!elements)
        {
            return nullptr;
        }
        if (elements > blockSize)
        {
            return nullptr;
        }
        if (!blocks.size())
        {
            T* pointer = allocateBlock();
            blocks.emplace_back(pointer, 1);
            sortBlocks();
            blockOffset = elements;
            blockIndex = 0;
            return pointer;
        }
        if (elements > blockSize - blockOffset)
        {
            T* pointer = allocateBlock();
            blocks.emplace_back(pointer, 1);
            sortBlocks();
            blockOffset = elements;
            blockIndex += 1;
            return pointer;
        }
        T* pointer = blocks[blockIndex].p + blockOffset;
        blockOffset += elements;
        ++blocks[blockIndex].count;
        return pointer;
    }


    [[nodiscard]] T* allocateBlock()
    {
        return static_cast<T*>(::operator new(blockBytes));
    }


    void deallocate(T* pointer, size_t)
    {
        auto iterator = std::ranges::lower_bound(sortedBlocks, pointer, {}, [](const allocInfo<T>* pointer) { return pointer->p; });

        allocInfo<T>* currentBlock;
        if (iterator == sortedBlocks.end())
        {
            currentBlock = *(iterator - 1);
        }
        else if ((*iterator)->p == pointer)
        {
            currentBlock = *iterator;
        }
        else
        {
            currentBlock = *(iterator - 1);
        }
        --currentBlock->count;

        if (!currentBlock->count)
        {
            ::operator delete(currentBlock->p, blockBytes);

            std::erase_if(blocks, [&](const allocInfo<T>& info) { return currentBlock == &info; });
            sortBlocks();
            --blockIndex;
        }
    }


    void sortBlocks()
    {
        sortedBlocks = blocks | std::ranges::views::transform([](allocInfo<T>& element) { return &element; }) | std::ranges::to<std::vector<allocInfo<T>*>>();
        std::ranges::sort(sortedBlocks, {}, [](const allocInfo<T>* pointer) { return pointer->p; });
    }


    [[nodiscard]] bool operator==(const BlockAllocator&) const
    {
        return true;
    }


    [[nodiscard]] bool operator!=(const BlockAllocator&) const
    {
        return false;
    }


    [[nodiscard]] static allocatorState<T, blockSize>& getState()
    {
        alignas(allocatorState<T, blockSize>) static std::byte storage[sizeof(allocatorState<T, blockSize>)];

        [[maybe_unused]] static const bool _ = []()
        {
            new(storage) allocatorState<T, blockSize>{};
            return true;
        }();

        return *std::launder(reinterpret_cast<allocatorState<T, blockSize>*>(storage));
    }
};

template <class T>
struct StringAllocator : public BlockAllocator<T, 512'000> {};

template <class T, size_t blockSize>
inline size_t& BlockAllocator<T, blockSize>::blockIndex = BlockAllocator<T, blockSize>::getState().blockIndex;

template <class T, size_t blockSize>
inline size_t& BlockAllocator<T, blockSize>::blockOffset = BlockAllocator<T, blockSize>::getState().blockOffset;

template <class T, size_t blockSize>
inline std::vector<allocInfo<T>>& BlockAllocator<T, blockSize>::blocks = BlockAllocator<T, blockSize>::getState().blocks;

template <class T, size_t blockSize>
inline std::vector<allocInfo<T>*>& BlockAllocator<T, blockSize>::sortedBlocks = BlockAllocator<T, blockSize>::getState().sortedBlocks;
