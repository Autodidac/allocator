#pragma once

#include <algorithm>
#include <vector>
#include <ranges>
#include <mutex>


// Usage, inherit like this:
// template<class T>
// struct StringAllocator : public BlockAllocator<T, 512'000, use_thread_safety, use_construction> {};


struct use_thread_safety {};
struct no_thread_safety {};
struct use_construction {};
struct no_construction {};


template<class T>
concept Thread_Safe_Concept = std::same_as<T, use_thread_safety> || std::same_as<T, no_thread_safety>;


template<class T>
concept Explicit_Construction_Concept = std::same_as<T, use_construction> || std::same_as<T, no_construction>;


template<class T>
struct allocInfo
{
    T* p;
    size_t count;
    size_t savedOffset;
    size_t allocatedCount;
};


template<class T, size_t blockSize, Thread_Safe_Concept thread_safe_tag>
class allocatorState
{
    template<class, size_t, Thread_Safe_Concept, Explicit_Construction_Concept>
    friend class BlockAllocator;

    size_t blockIndex;
    size_t blockOffset;
    std::vector<allocInfo<T>> blocks;
    std::vector<allocInfo<T>*> sortedBlocks;
    std::conditional_t<std::is_same_v<thread_safe_tag, use_thread_safety>, std::mutex, std::monostate> mutex;

    allocatorState() noexcept = default;
    allocatorState(const allocatorState&) = delete;
    allocatorState& operator=(const allocatorState&) = delete;
    allocatorState(allocatorState&&) = delete;
    allocatorState& operator=(const allocatorState&&) = delete;
};


template<class T, size_t blockSize, Thread_Safe_Concept thread_safe_tag = no_thread_safety, Explicit_Construction_Concept construction_tag = no_construction>
class BlockAllocator
{
public:
    using value_type = T;
    using is_always_equal = std::true_type;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    constexpr static size_t blockBytes = blockSize * sizeof(T);
    static allocatorState<T, blockSize, thread_safe_tag>* state;


    BlockAllocator() noexcept = default;


    template<class U>
    BlockAllocator(const BlockAllocator<U, blockSize, thread_safe_tag, construction_tag>&) noexcept {}


    [[nodiscard]] static T* allocate(size_t count) noexcept
    {
        [[maybe_unused]] static const bool _ = []() noexcept -> bool
        {
            state = getState();
            return false;
        }();


        [[maybe_unused]] std::conditional_t<std::is_same_v<thread_safe_tag, use_thread_safety>, std::scoped_lock<std::mutex>, std::monostate> lock(state->mutex);


        if (!count)
        {
            return nullptr;
        }
        if (count > blockSize)
        {
            T* pointer = static_cast<T*>(::operator new(count * sizeof(T)));
            allocInfo info{ pointer, 1, count, count };
            if (!state->blocks.size())
            {
                state->blocks.emplace_back(info);
            }
            else
            {
                std::swap(state->blocks.back(), info);
                state->blocks.emplace_back(info);
            }
            sortBlocks();
            ++state->blockIndex;
            return pointer;
        }
        if (!state->blocks.size())
        {
            T* pointer = allocateBlock();
            state->blocks.emplace_back(pointer, 1, 0, blockSize);
            sortBlocks();
            state->blockOffset = count;
            state->blockIndex = 0;
            return pointer;
        }
        if (count > state->blocks.back().allocatedCount - state->blockOffset)
        {
            T* pointer = allocateBlock();
            state->blocks.back().savedOffset = state->blockOffset;
            state->blocks.emplace_back(pointer, 1, 0, blockSize);
            sortBlocks();
            state->blockOffset = count;
            ++state->blockIndex;
            return pointer;
        }
        T* pointer = state->blocks[state->blockIndex].p + state->blockOffset;
        state->blockOffset += count;
        ++state->blocks[state->blockIndex].count;
        return pointer;
    }


    template<class... Args>
    [[nodiscard]] static T* construct(size_t count, Args&&... args) noexcept requires std::same_as<construction_tag, use_construction>
    {
        T* data = allocate(count);
        for (size_t i = 0; i < count; ++i)
        {
            new(data) T(std::forward<Args>(args)...);
        }
        return data;
    }


    static void deallocate(T* pointer, size_t count) noexcept
    {
        [[maybe_unused]] std::conditional_t<std::is_same_v<thread_safe_tag, use_thread_safety>, std::scoped_lock<std::mutex>, std::monostate> lock(state->mutex);

        const auto iterator = std::ranges::lower_bound(state->sortedBlocks, pointer, {}, [](const allocInfo<T>* pointer) { return pointer->p; });

        allocInfo<T>* currentBlock;
        if (iterator == state->sortedBlocks.end())
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
        if constexpr (std::same_as<construction_tag, use_construction>)
        {
            for (size_t i = 0; i < count; ++i)
            {
                pointer[i].~T();
            }
        }
        --currentBlock->count;

        if (!currentBlock->count)
        {
            ::operator delete(currentBlock->p, currentBlock->allocatedCount * sizeof(T));

            std::erase_if(state->blocks, [&](const allocInfo<T>& info) noexcept { return currentBlock == &info; });
            sortBlocks();
            --state->blockIndex;
            if (state->blockIndex != std::numeric_limits<size_t>::max())
            {
                state->blockOffset = state->blocks[state->blockIndex].savedOffset;
            }
        }
    }


    [[nodiscard]] constexpr bool operator==(const BlockAllocator&) const noexcept
    {
        return true;
    }


    [[nodiscard]] static allocatorState<T, blockSize, thread_safe_tag>* getState() noexcept
    {
        alignas(allocatorState<T, blockSize, thread_safe_tag>) static std::byte storage[sizeof(allocatorState<T, blockSize, thread_safe_tag>)];

        [[maybe_unused]] static const bool _ = []() noexcept -> bool
        {
            new(storage) allocatorState<T, blockSize, thread_safe_tag>{};
            return true;
        }();

        return std::launder(reinterpret_cast<allocatorState<T, blockSize, thread_safe_tag>*>(storage));
    }

private:
    [[nodiscard]] constexpr bool operator!=(const BlockAllocator&) const noexcept
    {
        return false;
    }


    [[nodiscard]] static T* allocateBlock() noexcept
    {
        return static_cast<T*>(::operator new(blockBytes));
    }


    static void sortBlocks() noexcept
    {
        state->sortedBlocks = state->blocks | std::ranges::views::transform([](allocInfo<T>& element) noexcept { return &element; }) | std::ranges::to<std::vector<allocInfo<T>*>>();
        std::ranges::sort(state->sortedBlocks, {}, [](const allocInfo<T>* pointer) noexcept { return pointer->p; });
    }
};

template<class T, size_t blockSize, Thread_Safe_Concept thread_safe_tag, Explicit_Construction_Concept construction_tag>
inline allocatorState<T, blockSize, thread_safe_tag>* BlockAllocator<T, blockSize, thread_safe_tag, construction_tag>::state;
