#pragma once

#include <type_traits>
#include <concepts>
#include <stdint.h>
#include <bit>


namespace gbr::arena
{
    struct use_safety {};
    struct no_safety {};
    struct use_multi_type {};
    struct use_single_type {};


    template<class T>
    concept Arena_Safety_Concept = std::same_as<T, use_safety> || std::same_as<T, no_safety>;


    template<class T>
    concept Arena_Single_Type_Concept = std::same_as<T, use_multi_type> || std::same_as<T, use_single_type>;


    struct ArenaHeader
    {
        size_t alignment;
        size_t totalSize;
        size_t userSize;
        size_t offset;
    };


    template<size_t alignment = alignof(ArenaHeader)>
    [[nodiscard]] inline void* createArena(size_t size) noexcept
    {
        constexpr static bool isPowerOf2 = (alignment & (alignment - 1)) == 0;
        static_assert(isPowerOf2, "gbr::arena::createArena alignment is not a power of 2");

        constexpr static bool lessThan8BytesAligned = alignment <= alignof(ArenaHeader);
        constexpr static size_t sizeOfHeader = lessThan8BytesAligned ? sizeof(ArenaHeader) : (alignment > sizeof(ArenaHeader) ? alignment : alignment * 2);
        constexpr static size_t alignmentWithHeader = lessThan8BytesAligned ? alignof(ArenaHeader) : alignment;

        void* arena = ::operator new(size + sizeOfHeader, std::align_val_t(alignmentWithHeader));
        const ArenaHeader header =
        {
            alignmentWithHeader,
            size + sizeOfHeader,
            size,
            0
        };
        reinterpret_cast<ArenaHeader*>(reinterpret_cast<unsigned char*>(arena) + sizeOfHeader)[-1] = header;

        return reinterpret_cast<unsigned char*>(arena) + sizeOfHeader;
    }


    inline void destroyArena(void* arena) noexcept
    {
        const ArenaHeader& header = reinterpret_cast<ArenaHeader*>(arena)[-1];
        void* basePointer = reinterpret_cast<unsigned char*>(arena) - (header.totalSize - header.userSize);

        ::operator delete(basePointer, header.totalSize, std::align_val_t(header.alignment));
    }


    inline void clearArena(void* arena) noexcept
    {
        reinterpret_cast<size_t*>(arena)[-1] = 0;
    }


    [[nodiscard]] inline ArenaHeader& getArenaHeader(void* arena) noexcept
    {
        return reinterpret_cast<ArenaHeader*>(arena)[-1];
    }


    [[nodiscard]] inline size_t getOffset(const void* arena) noexcept
    {
        return reinterpret_cast<const size_t*>(arena)[-1];
    }


    inline void setOffset(void* arena, size_t offset) noexcept
    {
        reinterpret_cast<size_t*>(arena)[-1] = offset;
    }


    template<class T>
    inline void stepBackwards(void* arena, size_t count) noexcept
    {
        const size_t bytes = count * sizeof(T);
        size_t& offset = reinterpret_cast<size_t*>(arena)[-1];

        if (bytes > offset)
        {
            offset = 0;
        }
        else
        {
            offset -= bytes;
        }
    }


    template<class T>
    inline void unsafeStepBackwards(void* arena, size_t count) noexcept
    {
        reinterpret_cast<size_t*>(arena)[-1] -= count * sizeof(T);
    }


    template<class T>
    [[nodiscard]] inline size_t getFreeSlots(const void* arena) noexcept
    {
        constexpr static bool isPowerOf2 = (sizeof(T) & (sizeof(T) - 1)) == 0;
        constexpr static size_t shiftAmount = std::bit_width(sizeof(T)) - 1;

        const size_t size = reinterpret_cast<const size_t*>(arena)[-2];
        const size_t offset = reinterpret_cast<const size_t*>(arena)[-1];
        const size_t remainingSpace = size - offset;

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
    [[nodiscard]] inline size_t multiTypeGetFreeSlots(const void* arena) noexcept
    {
        constexpr static bool isPowerOf2 = (sizeof(T) & (sizeof(T) - 1)) == 0;
        constexpr static size_t alignment = std::alignment_of_v<T>;
        constexpr static size_t shiftAmount = std::bit_width(sizeof(T)) - 1;

        const size_t size = reinterpret_cast<const size_t*>(arena)[-2];
        const size_t offset = reinterpret_cast<const size_t*>(arena)[-1];
        const uintptr_t basePointer = reinterpret_cast<const uintptr_t>(arena);
        const uintptr_t alignedPointer = (basePointer + offset + alignment - 1) & ~(alignment - 1);
        const size_t remainingSpace = basePointer + size - alignedPointer;

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
    [[nodiscard]] inline T* allocate(void* arena, size_t count) noexcept
    {
        const size_t allocateSize = count * sizeof(T);
        const size_t size = reinterpret_cast<size_t*>(arena)[-2];
        size_t& offset = reinterpret_cast<size_t*>(arena)[-1];
        const uintptr_t basePointer = reinterpret_cast<uintptr_t>(arena);

        if (offset + allocateSize >= size)
        {
            return nullptr;
        }
        const uintptr_t pointer = basePointer + offset;
        offset += allocateSize;

        return reinterpret_cast<T*>(pointer);
    }


    template<class T>
    [[nodiscard]] inline T* unsafeAllocate(void* arena, size_t count) noexcept
    {
        const size_t allocateSize = count * sizeof(T);
        size_t& offset = reinterpret_cast<size_t*>(arena)[-1];
        const uintptr_t basePointer = reinterpret_cast<uintptr_t>(arena);
        const uintptr_t pointer = basePointer + offset;
        offset += allocateSize;

        return reinterpret_cast<T*>(pointer);
    }


    template<class T>
    [[nodiscard]] inline T* multiTypeAllocate(void* arena, size_t count) noexcept
    {
        constexpr static size_t alignment = std::alignment_of_v<T>;

        const size_t allocateSize = count * sizeof(T);
        const size_t size = reinterpret_cast<size_t*>(arena)[-2];
        size_t& offset = reinterpret_cast<size_t*>(arena)[-1];
        const uintptr_t basePointer = reinterpret_cast<uintptr_t>(arena);
        const uintptr_t alignedPointer = (basePointer + offset + alignment - 1) & ~(alignment - 1);

        if (alignedPointer + allocateSize >= basePointer + size)
        {
            return nullptr;
        }
        offset = alignedPointer + allocateSize - basePointer;

        return reinterpret_cast<T*>(alignedPointer);
    }


    template<class T>
    [[nodiscard]] inline T* unsafeMultiTypeAllocate(void* arena, size_t count) noexcept
    {
        constexpr static size_t alignment = std::alignment_of_v<T>;

        const size_t allocateSize = count * sizeof(T);
        size_t& offset = reinterpret_cast<size_t*>(arena)[-1];
        const uintptr_t basePointer = reinterpret_cast<uintptr_t>(arena);
        const uintptr_t alignedPointer = (basePointer + offset + alignment - 1) & ~(alignment - 1);
        offset = alignedPointer + allocateSize - basePointer;

        return reinterpret_cast<T*>(alignedPointer);
    }


    template<class T, class... Args>
    [[nodiscard]] inline T* construct(void* arena, size_t count, Args&&... args) noexcept
    {
        T* data = allocate<T>(arena, count);

        for (size_t i = 0; i < count; ++i)
        {
            ::new(data + i) T(std::forward<Args>(args)...);
        }

        return data;
    }


    template<class T, class... Args>
    [[nodiscard]] inline T* unsafeConstruct(void* arena, size_t count, Args&&... args) noexcept
    {
        T* data = unsafeAllocate<T>(arena, count);

        for (size_t i = 0; i < count; ++i)
        {
            ::new(data + i) T(std::forward<Args>(args)...);
        }

        return data;
    }


    template<class T, class... Args>
    [[nodiscard]] inline T* multiTypeConstruct(void* arena, size_t count, Args&&... args) noexcept
    {
        T* data = multiTypeAllocate<T>(arena, count);

        for (size_t i = 0; i < count; ++i)
        {
            ::new(data + i) T(std::forward<Args>(args)...);
        }

        return data;
    }


    template<class T, class... Args>
    [[nodiscard]] inline T* unsafeMultiTypeConstruct(void* arena, size_t count, Args&&... args) noexcept
    {
        T* data = unsafeMultiTypeAllocate<T>(arena, count);

        for (size_t i = 0; i < count; ++i)
        {
            ::new(data + i) T(std::forward<Args>(args)...);
        }

        return data;
    }


    template<class T>
    inline void destroy(T* ptr, size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            ptr[i].~T();
        }
    }


    // Usage, inherit like this:
    // template<class T>
    // class UnsafeArena : public gbr::arena::stdAllocator<T, gbr::arena::no_safety, gbr::arena::use_multi_type> {};


    template<class T, Arena_Safety_Concept safety_flag = no_safety, Arena_Single_Type_Concept single_type_flag = use_multi_type>
    class stdAllocator
    {
    public:
        using value_type = T;
        using is_always_equal = std::false_type;
        using propagate_on_container_copy_assignment = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;

        void* arena;


        stdAllocator(void* arena) noexcept : arena(arena) {}


        template<class U>
        stdAllocator(const stdAllocator<U, safety_flag, single_type_flag>& other) noexcept : arena(other.arena) {}


        inline void clearArena() noexcept
        {
            gbr::arena::clearArena(arena);
        }


        [[nodiscard]] inline ArenaHeader& getArenaHeader() noexcept
        {
            return gbr::arena::getArenaHeader(arena);
        }


        [[nodiscard]] inline size_t getOffset() const noexcept
        {
            return gbr::arena::getOffset(arena);
        }


        inline void setOffset(size_t offset) noexcept
        {
            gbr::arena::setOffset(arena, offset);
        }


        inline void stepBackwards(size_t count) noexcept
        {
            if constexpr (std::same_as<safety_flag, use_safety>)
            {
                return gbr::arena::stepBackwards<T>(arena, count);
            }
            else if constexpr (std::same_as<safety_flag, no_safety>)
            {
                return gbr::arena::unsafeStepBackwards<T>(arena, count);
            }
        }


        [[nodiscard]] inline size_t getFreeSlots() const noexcept
        {
            if constexpr (std::same_as<single_type_flag, use_single_type>)
            {
                return gbr::arena::getFreeSlots<T>(arena);
            }
            else if constexpr (std::same_as<single_type_flag, use_multi_type>)
            {
                return gbr::arena::multiTypeGetFreeSlots<T>(arena);
            }
        }


        [[nodiscard]] inline T* allocate(size_t count) noexcept
        {
            if constexpr (std::same_as<safety_flag, use_safety> && std::same_as<single_type_flag, use_single_type>)
            {
                return gbr::arena::allocate<T>(arena, count);
            }
            else if constexpr (std::same_as<safety_flag, no_safety> && std::same_as<single_type_flag, use_single_type>)
            {
                return gbr::arena::unsafeAllocate<T>(arena, count);
            }
            else if constexpr (std::same_as<safety_flag, use_safety> && std::same_as<single_type_flag, use_multi_type>)
            {
                return gbr::arena::multiTypeAllocate<T>(arena, count);
            }
            else if constexpr (std::same_as<safety_flag, no_safety> && std::same_as<single_type_flag, use_multi_type>)
            {
                return gbr::arena::unsafeMultiTypeAllocate<T>(arena, count);
            }
        }


        inline void deallocate(T*, size_t) const noexcept {}


        [[nodiscard]] inline bool operator==(const stdAllocator& other) const noexcept
        {
            return arena == other.arena;
        }


        [[nodiscard]] inline bool operator!=(const stdAllocator& other) const noexcept
        {
            return arena != other.arena;
        }
    };


    // Usage, inherit like this:
    // template<class T>
    // class UnsafeArena : public gbr::arena::staticAllocator<T, 0, gbr::arena::no_safety, gbr::arena::use_multi_type> {};
    // UID used to create multiple instances.


    template<class T, size_t UID, Arena_Safety_Concept safety_flag = no_safety, Arena_Single_Type_Concept single_type_flag = use_multi_type>
    class staticAllocator
    {
    public:
        using value_type = T;
        using is_always_equal = std::true_type;
        using propagate_on_container_copy_assignment = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;

        static void* arena;


        staticAllocator() noexcept = default;


        template<class U>
        staticAllocator(const staticAllocator<U, UID, safety_flag, single_type_flag>& other) noexcept
        {
            arena = other.arena;
        }


        [[nodiscard]] inline static size_t getUID() noexcept
        {
            return UID;
        }


        static void createArena(size_t count) noexcept
        {
            arena = gbr::arena::createArena<alignof(T)>(count * sizeof(T));
        }


        static void destroyArena() noexcept
        {
            gbr::arena::destroyArena(arena);
        }


        inline static void clearArena() noexcept
        {
            gbr::arena::clearArena(arena);
        }


        [[nodiscard]] inline static ArenaHeader& getArenaHeader() noexcept
        {
            return gbr::arena::getArenaHeader(arena);
        }


        [[nodiscard]] inline static size_t getOffset() noexcept
        {
            return gbr::arena::getOffset(arena);
        }


        inline static void setOffset(size_t offset) noexcept
        {
            gbr::arena::setOffset(arena, offset);
        }


        inline static void stepBackwards(size_t count) noexcept
        {
            if constexpr (std::same_as<safety_flag, use_safety>)
            {
                return gbr::arena::stepBackwards<T>(arena, count);
            }
            else if constexpr (std::same_as<safety_flag, no_safety>)
            {
                return gbr::arena::unsafeStepBackwards<T>(arena, count);
            }
        }


        [[nodiscard]] inline static size_t getFreeSlots() noexcept
        {
            if constexpr (std::same_as<single_type_flag, use_single_type>)
            {
                return gbr::arena::getFreeSlots<T>(arena);
            }
            else if constexpr (std::same_as<single_type_flag, use_multi_type>)
            {
                return gbr::arena::multiTypeGetFreeSlots<T>(arena);
            }
        }


        [[nodiscard]] inline static T* allocate(size_t count) noexcept
        {
            if constexpr (std::same_as<safety_flag, use_safety> && std::same_as<single_type_flag, use_single_type>)
            {
                return gbr::arena::allocate<T>(arena, count);
            }
            else if constexpr (std::same_as<safety_flag, no_safety> && std::same_as<single_type_flag, use_single_type>)
            {
                return gbr::arena::unsafeAllocate<T>(arena, count);
            }
            else if constexpr (std::same_as<safety_flag, use_safety> && std::same_as<single_type_flag, use_multi_type>)
            {
                return gbr::arena::multiTypeAllocate<T>(arena, count);
            }
            else if constexpr (std::same_as<safety_flag, no_safety> && std::same_as<single_type_flag, use_multi_type>)
            {
                return gbr::arena::unsafeMultiTypeAllocate<T>(arena, count);
            }
        }


        inline static void deallocate(T*, size_t) noexcept {}


        [[nodiscard]] constexpr inline bool operator==(const staticAllocator&) const noexcept
        {
            return true;
        }


        [[nodiscard]] constexpr inline bool operator!=(const staticAllocator&) const noexcept
        {
            return false;
        }
    };

    template<class T, size_t UID, Arena_Safety_Concept safety_flag, Arena_Single_Type_Concept single_type_flag>
    inline void* staticAllocator<T, UID, safety_flag, single_type_flag>::arena;
}


// To use, define GBR_ARENA_VIRTUAL_IMPLEMENTATION before the header is included:


#ifdef GBR_ARENA_VIRTUAL_IMPLEMENTATION

#include <Windows.h>


namespace gbr::arena
{
    template<size_t alignment>
    [[nodiscard]] inline void* createVirtualArena(size_t reserveSize, size_t initialSize) noexcept
    {
        constexpr static bool isPowerOf2 = (alignment & (alignment - 1)) == 0;
        static_assert(isPowerOf2, "gbr::arena::createVirtualArena alignment is not a power of 2");
        static_assert(sizeof(size_t) >= 8, "gbr::arena::createVirtualArena requires size_t to be at least 64 bits");

        constexpr static bool lessThan8BytesAligned = alignment <= alignof(ArenaHeader);
        constexpr static size_t sizeOfHeader = lessThan8BytesAligned ? sizeof(ArenaHeader) : (alignment > sizeof(ArenaHeader) ? alignment : alignment * 2);
        constexpr static size_t alignmentWithHeader = lessThan8BytesAligned ? alignof(ArenaHeader) : alignment;

        SYSTEM_INFO systemInfo;
        GetSystemInfo(&systemInfo);

        const size_t allocGranularity = static_cast<size_t>(systemInfo.dwAllocationGranularity);
        initialSize = (initialSize + sizeOfHeader + allocGranularity - 1) & ~(allocGranularity - 1);
        reserveSize = (reserveSize + allocGranularity - 1) & ~(allocGranularity - 1);
        const size_t reserveSizeOrAlignment = (reserveSize << 24) | allocGranularity;

        void* arena = VirtualAlloc(nullptr, reserveSize, MEM_RESERVE, PAGE_READWRITE);
        VirtualAlloc(arena, initialSize, MEM_COMMIT, PAGE_READWRITE);
        const ArenaHeader header =
        {
            reserveSizeOrAlignment,
            initialSize,
            initialSize - sizeOfHeader,
            0
        };
        reinterpret_cast<ArenaHeader*>(reinterpret_cast<unsigned char*>(arena) + sizeOfHeader)[-1] = header;

        return reinterpret_cast<unsigned char*>(arena) + sizeOfHeader;
    }


    inline void destroyVirtualArena(void* arena) noexcept
    {
        VirtualFree(arena, 0, MEM_RELEASE);
    }


    inline size_t virtualGrow(void* arena, size_t size) noexcept
    {
        ArenaHeader& header = reinterpret_cast<ArenaHeader*>(arena)[-1];

        const size_t allocGranularity = header.alignment & 0b11111111'11111111;
        const size_t newSize = (header.totalSize + size + allocGranularity - 1) & ~(allocGranularity - 1);

        if (newSize > (header.alignment >> 24))
        {
            return 0UI64;
        }
        VirtualAlloc(arena, newSize, MEM_COMMIT, PAGE_READWRITE);

        const size_t bytesGrown = newSize - header.totalSize;

        header.totalSize += bytesGrown;
        header.userSize += bytesGrown;

        return bytesGrown;
    }


    template<class T>
    [[nodiscard]] inline T* virtualAllocate(void* arena, size_t count) noexcept
    {
        const size_t allocateSize = count * sizeof(T);
        const size_t totalSize = reinterpret_cast<size_t*>(arena)[-3];
        const size_t size = reinterpret_cast<size_t*>(arena)[-2];
        size_t& offset = reinterpret_cast<size_t*>(arena)[-1];
        const uintptr_t basePointer = reinterpret_cast<uintptr_t>(arena);

        if (offset + allocateSize >= size)
        {
            const bool allocationSuccess = allocateSize > totalSize ? virtualGrow(arena, allocateSize) : virtualGrow(arena, totalSize);

            if (!allocationSuccess)
            {
                return nullptr;
            }
        }
        const uintptr_t pointer = basePointer + offset;
        offset += allocateSize;

        return reinterpret_cast<T*>(pointer);
    }
}

#endif
