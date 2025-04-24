#pragma once

#include <algorithm>
#include <ranges>
#include <vector>
#include <mutex>


// Usage, inherit like this:
// template <class T>
// struct StringAllocator : public BlockAllocator<T, 512'000, use_thread_safety> {};


struct use_thread_safety {};
struct no_thread_safety {};


template<class T>
concept Thread_Safe_Concept = std::same_as<T, use_thread_safety> || std::same_as<T, no_thread_safety>;


template<class T>
struct allocInfo
{
    T* p;
    size_t count;
    size_t savedOffset;
};


template <class T, size_t blockSize, Thread_Safe_Concept thread_safe_tag>
struct allocatorState
{
    size_t blockIndex;
    size_t blockOffset;
    std::vector<allocInfo<T>> blocks;
    std::vector<allocInfo<T>*> sortedBlocks;
    std::conditional_t<std::is_same_v<thread_safe_tag, use_thread_safety>, std::mutex, std::monostate> mutex;
};


template <class T, size_t blockSize, Thread_Safe_Concept thread_safe_tag = use_thread_safety>
struct BlockAllocator
{
    using value_type = T;
    using is_always_equal = std::true_type;
    static constexpr size_t blockBytes = blockSize * sizeof(T);
    static size_t& blockIndex;
    static size_t& blockOffset;
    static std::vector<allocInfo<T>>& blocks;
    static std::vector<allocInfo<T>*>& sortedBlocks;
    static std::conditional_t<std::is_same_v<thread_safe_tag, use_thread_safety>, std::mutex&, std::monostate> mutex;


    BlockAllocator() noexcept = default;


    template <class U>
    BlockAllocator(const BlockAllocator<U, blockSize, thread_safe_tag>&) noexcept {}


    [[nodiscard]] T* allocate(size_t elements) noexcept
    {
        [[maybe_unused]] const std::conditional_t<std::is_same_v<thread_safe_tag, use_thread_safety>, std::scoped_lock<std::mutex>, std::monostate> lock(mutex);

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
            blocks.emplace_back(pointer, 1, 0);
            sortBlocks();
            blockOffset = elements;
            blockIndex = 0;
            return pointer;
        }
        if (elements > blockSize - blockOffset)
        {
            T* pointer = allocateBlock();
            blocks.back().savedOffset = blockOffset;
            blocks.emplace_back(pointer, 1, 0);
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


    void deallocate(T* pointer, size_t) noexcept
    {
        [[maybe_unused]] const std::conditional_t<std::is_same_v<thread_safe_tag, use_thread_safety>, std::scoped_lock<std::mutex>, std::monostate> lock(mutex);

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

            std::erase_if(blocks, [&](const allocInfo<T>& info) noexcept { return currentBlock == &info; });
            sortBlocks();
            --blockIndex;
            if (blockIndex != std::numeric_limits<size_t>::max())
            {
                blockOffset = blocks[blockIndex].savedOffset;
            }
        }
    }


    [[nodiscard]] constexpr bool operator==(const BlockAllocator&) const noexcept
    {
        return true;
    }


    [[nodiscard]] constexpr bool operator!=(const BlockAllocator&) const noexcept
    {
        return false;
    }

private:
    [[nodiscard]] T* allocateBlock() noexcept
    {
        return static_cast<T*>(::operator new(blockBytes));
    }


    void sortBlocks() noexcept
    {
        sortedBlocks = blocks | std::ranges::views::transform([](allocInfo<T>& element) noexcept { return &element; }) | std::ranges::to<std::vector<allocInfo<T>*>>();
        std::ranges::sort(sortedBlocks, {}, [](const allocInfo<T>* pointer) noexcept { return pointer->p; });
    }


    [[nodiscard]] static allocatorState<T, blockSize, thread_safe_tag>& getState() noexcept
    {
        alignas(allocatorState<T, blockSize, thread_safe_tag>) static std::byte storage[sizeof(allocatorState<T, blockSize, thread_safe_tag>)];

        [[maybe_unused]] static const bool _ = []() noexcept
            {
                new(storage) allocatorState<T, blockSize, thread_safe_tag>{};
                return true;
            }();

        return *std::launder(reinterpret_cast<allocatorState<T, blockSize, thread_safe_tag>*>(storage));
    }
};

template <class T, size_t blockSize, Thread_Safe_Concept thread_safe_tag>
inline size_t& BlockAllocator<T, blockSize, thread_safe_tag>::blockIndex = BlockAllocator<T, blockSize, thread_safe_tag>::getState().blockIndex;

template <class T, size_t blockSize, Thread_Safe_Concept thread_safe_tag>
inline size_t& BlockAllocator<T, blockSize, thread_safe_tag>::blockOffset = BlockAllocator<T, blockSize, thread_safe_tag>::getState().blockOffset;

template <class T, size_t blockSize, Thread_Safe_Concept thread_safe_tag>
inline std::vector<allocInfo<T>>& BlockAllocator<T, blockSize, thread_safe_tag>::blocks = BlockAllocator<T, blockSize, thread_safe_tag>::getState().blocks;

template <class T, size_t blockSize, Thread_Safe_Concept thread_safe_tag>
inline std::vector<allocInfo<T>*>& BlockAllocator<T, blockSize, thread_safe_tag>::sortedBlocks = BlockAllocator<T, blockSize, thread_safe_tag>::getState().sortedBlocks;

template <class T, size_t blockSize, Thread_Safe_Concept thread_safe_tag>
inline std::conditional_t<std::is_same_v<thread_safe_tag, use_thread_safety>, std::mutex&, std::monostate> BlockAllocator<T, blockSize, thread_safe_tag>::mutex = BlockAllocator<T, blockSize, thread_safe_tag>::getState().mutex;
