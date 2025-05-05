#pragma once

#include <stdint.h>


struct use_safety {};
struct no_safety {};
struct use_multi_type {};
struct use_single_type {};


// "Arena_Safety_Concept" ensures that stdJunkAllocator recieves use_safety or no_safety
// no_safety will cause stdJunkAllocator<T>::allocate() to call JunkAllocator::unsafeAllocate<T>(), omiting the bounds check, intended to be used with JunkAllocator::getFreeSlots<T>()
template<class T>
concept Arena_Safety_Concept = std::same_as<T, use_safety> || std::same_as<T, no_safety>;

// Similarly, "Arena_Single_Type_Concept" determines whether the arena used with the allocator will be storing multiple types or a single type.
// Storing a single type omits aligning the pointer, responsibility is on the user to never pass in an arena meant for a single type into JunkAllocator::multiTypeAllocate<T>() and vice versa
template<class T>
concept Arena_Single_Type_Concept = std::same_as<T, use_multi_type> || std::same_as<T, use_single_type>;


// "arenaPointer" stores pointer to arena, size of the arena, and offset into the arena. The user owns this type, it is unique and should not be copied or moved around.
class arenaPointer
{
public:
    friend class JunkAllocator;

    template<class, Arena_Safety_Concept, Arena_Single_Type_Concept>
    friend class stdJunkAllocator;


    arenaPointer(const arenaPointer&) = delete;
    arenaPointer& operator=(const arenaPointer&) = delete;


    arenaPointer(arenaPointer&& other) noexcept
        : ptr(other.ptr), size(other.size), offset(other.offset)
    {
        other.ptr = 0;
        other.size = 0;
        other.offset = 0;
    }


    arenaPointer& operator=(arenaPointer&& other) = delete;

private:
    arenaPointer(uintptr_t ptr, size_t size, size_t offset) noexcept : ptr(ptr), size(size), offset(offset) {}


    uintptr_t ptr;
    size_t size;
    size_t offset;
};


class JunkAllocator
{
public:
    [[nodiscard]] static arenaPointer createArena(size_t size) noexcept
    {
        return { reinterpret_cast<uintptr_t>(::operator new(size)), size, 0 };
    }


    static void destroyArena(arenaPointer& arena) noexcept
    {
        ::operator delete(reinterpret_cast<void*>(arena.ptr), arena.size);
    }


    static void clearArena(arenaPointer& arena) noexcept
    {
        arena.offset = 0;
    }


    template<class T>
    static void stepBackwards(arenaPointer& arena, size_t count) noexcept
    {
        const size_t bytes = count * sizeof(T);
        if (bytes > arena.offset)
        {
            arena.offset = 0;
        }
        else
        {
            arena.offset -= bytes;
        }
    }


    template<class T>
    static void stepBackwardsUnsafe(arenaPointer& arena, size_t count) noexcept
    {
        arena.offset -= count * sizeof(T);
    }


    template<class T>
    [[nodiscard]] static size_t getFreeSlots(arenaPointer& arena) noexcept
    {
        constexpr static bool isPowerOf2 = (sizeof(T) & (sizeof(T) - 1)) == 0;
        constexpr static size_t shiftAmount = [](size_t s) constexpr noexcept -> size_t
        {
            size_t shift = 0;
            while (s > 1)
            {
                s >>= 1;
                ++shift;
            }
            return shift;
        }(sizeof(T));
        const size_t remainingSpace = arena.size - arena.offset;
        if constexpr (isPowerOf2)
        {
            return remainingSpace >> shiftAmount;
        }
        else
        {
            return remainingSpace / sizeof(T);
        }
    }


    template<class T>
    [[nodiscard]] static size_t multiTypeGetFreeSlots(arenaPointer& arena) noexcept
    {
        constexpr static bool isPowerOf2 = (sizeof(T) & (sizeof(T) - 1)) == 0;
        constexpr static size_t alignment = std::alignment_of_v<T>;
        constexpr static size_t shiftAmount = [](size_t s) constexpr noexcept -> size_t
        {
            size_t shift = 0;
            while (s > 1)
            {
                s >>= 1;
                ++shift;
            }
            return shift;
        }(sizeof(T));
        const uintptr_t alignedPointer = (arena.ptr + arena.offset + alignment - 1) & ~(alignment - 1);
        const size_t remainingSpace = arena.ptr + arena.size - alignedPointer;
        if constexpr (isPowerOf2)
        {
            return remainingSpace >> shiftAmount;
        }
        else
        {
            return remainingSpace / sizeof(T);
        }
    }


    template<class T>
    [[nodiscard]] static T* allocate(arenaPointer& arena, size_t count) noexcept
    {
        const size_t size = count * sizeof(T);
        if (arena.offset + size >= arena.size)
        {
            return nullptr;
        }
        const T* pointer = arena.ptr + arena.offset;
        arena.offset = arena.offset + size;
        return pointer;
    }


    template<class T>
    [[nodiscard]] static T* unsafeAllocate(arenaPointer& arena, size_t count) noexcept
    {
        const size_t size = count * sizeof(T);
        const uintptr_t pointer = arena.ptr + arena.offset;
        arena.offset = arena.offset + size;
        return reinterpret_cast<T*>(pointer);
    }


    template<class T>
    [[nodiscard]] static T* multiTypeAllocate(arenaPointer& arena, size_t count) noexcept
    {
        constexpr static size_t alignment = std::alignment_of_v<T>;
        const size_t size = count * sizeof(T);
        const uintptr_t alignedPointer = (arena.ptr + arena.offset + alignment - 1) & ~(alignment - 1);
        if (alignedPointer + size >= arena.ptr + arena.size)
        {
            return nullptr;
        }
        arena.offset = alignedPointer + size - arena.ptr;
        return reinterpret_cast<T*>(alignedPointer);
    }


    template<class T>
    [[nodiscard]] static T* unsafeMultiTypeAllocate(arenaPointer& arena, size_t count) noexcept
    {
        constexpr static size_t alignment = std::alignment_of_v<T>;
        const size_t size = count * sizeof(T);
        const uintptr_t alignedPointer = (arena.ptr + arena.offset + alignment - 1) & ~(alignment - 1);
        arena.offset = alignedPointer + size - arena.ptr;
        return reinterpret_cast<T*>(alignedPointer);
    }


    template<class T, class... Args>
    [[nodiscard]] static T* construct(arenaPointer& arena, size_t count, Args&&... args) noexcept
    {
        T* data = allocate<T>(arena, count);
        for (size_t i = 0; i < count; ++i)
        {
            new(data + i) T(std::forward<Args>(args)...);
        }
        return data;
    }


    template<class T, class... Args>
    [[nodiscard]] static T* unsafeConstruct(arenaPointer& arena, size_t count, Args&&... args) noexcept
    {
        T* data = unsafeAllocate<T>(arena, count);
        for (size_t i = 0; i < count; ++i)
        {
            new(data + i) T(std::forward<Args>(args)...);
        }
        return data;
    }


    template<class T, class... Args>
    [[nodiscard]] static T* multiTypeConstruct(arenaPointer& arena, size_t count, Args&&... args) noexcept
    {
        T* data = multiTypeAllocate<T>(arena, count);
        for (size_t i = 0; i < count; ++i)
        {
            new(data + i) T(std::forward<Args>(args)...);
        }
        return data;
    }


    template<class T, class... Args>
    [[nodiscard]] static T* unsafeMultiTypeConstruct(arenaPointer& arena, size_t count, Args&&... args) noexcept
    {
        T* data = unsafeMultiTypeAllocate<T>(arena, count);
        for (size_t i = 0; i < count; ++i)
        {
            new(data + i) T(std::forward<Args>(args)...);
        }
        return data;
    }


    template<class T>
    static void destroy(T* ptr, size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            ptr[i].~T();
        }
    }
};


// Usage, inherit like this:
// template<class T>
// struct UnsafeArena : public stdJunkAllocator<T, no_safety, use_multi_type> {};


template<class T, Arena_Safety_Concept safety_flag = no_safety, Arena_Single_Type_Concept single_type_flag = use_multi_type>
class stdJunkAllocator
{
public:
    using value_type = T;
    using is_always_equal = std::true_type;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    
    arenaPointer& arena;


    stdJunkAllocator(arenaPointer& arena) noexcept : arena(arena) {}


    template<class U>
    stdJunkAllocator(const stdJunkAllocator<U, safety_flag, single_type_flag>& other) noexcept : arena(other.arena) {}


    [[nodiscard]] static arenaPointer createArena(size_t size) noexcept
    {
        return JunkAllocator::createArena(size);
    }


    static void destroyArena(arenaPointer& arena) noexcept
    {
        JunkAllocator::destroyArena(arena);
    }


    static void clearArena(arenaPointer& arena) noexcept
    {
        JunkAllocator::clearArena(arena);
    }


    [[nodiscard]] static void stepBackwards(arenaPointer& arena, size_t count) noexcept
    {
        if constexpr (std::same_as<safety_flag, use_safety>)
        {
            return JunkAllocator::stepBackwards<T>(arena, count);
        }
        else if constexpr (std::same_as<safety_flag, no_safety>)
        {
            return JunkAllocator::stepBackwardsUnsafe<T>(arena, count);
        }
    }


    [[nodiscard]] static size_t getFreeSlots(arenaPointer& arena) noexcept
    {
        if constexpr (std::same_as<single_type_flag, use_single_type>)
        {
            return JunkAllocator::getFreeSlots<T>(arena);
        }
        else if constexpr (std::same_as<single_type_flag, use_multi_type>)
        {
            return JunkAllocator::multiTypeGetFreeSlots<T>(arena);
        }
    }


    [[nodiscard]] T* allocate(size_t count) noexcept
    {
        if constexpr (std::same_as<safety_flag, use_safety> && std::same_as<single_type_flag, use_single_type>)
        {
            return JunkAllocator::allocate<T>(arena, count);
        }
        else if constexpr (std::same_as<safety_flag, no_safety> && std::same_as<single_type_flag, use_single_type>)
        {
            return JunkAllocator::unsafeAllocate<T>(arena, count);
        }
        else if constexpr (std::same_as<safety_flag, use_safety> && std::same_as<single_type_flag, use_multi_type>)
        {
            return JunkAllocator::multiTypeAllocate<T>(arena, count);
        }
        else if constexpr (std::same_as<safety_flag, no_safety> && std::same_as<single_type_flag, use_multi_type>)
        {
            return JunkAllocator::unsafeMultiTypeAllocate<T>(arena, count);
        }
    }


    void deallocate(T*, size_t) const noexcept {}


    [[nodiscard]] bool operator==(const stdJunkAllocator& other) const noexcept
    {
        return &arena == &other.arena;
    }


    [[nodiscard]] bool operator!=(const stdJunkAllocator& other) const noexcept
    {
        return &arena != &other.arena;
    }
};
