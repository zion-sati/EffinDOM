#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace effindom::v2::ui {

// Slots addressed by handle index and allocated a page at a time, the first time an index in the page is used. Memory
// follows the highest index in use rather than the capacity, and a slot's address never changes: Yoga keeps node
// pointers as measure contexts.
template <typename Slot, std::size_t kCapacity, std::size_t kPageSize = 256U>
class PagedSlots {
public:
    static constexpr std::size_t Capacity() { return kCapacity; }

    // One past the highest index whose page is allocated.
    std::size_t Extent() const {
        const std::size_t extent = pages_.size() * kPageSize;
        return extent < kCapacity ? extent : kCapacity;
    }

    // The slot at `index`, or null when it lies past the capacity or its page was never allocated.
    Slot* Find(std::size_t index) {
        return const_cast<Slot*>(static_cast<const PagedSlots&>(*this).Find(index));
    }

    const Slot* Find(std::size_t index) const {
        const std::size_t page = index / kPageSize;
        if (index >= kCapacity || page >= pages_.size() || !pages_[page]) {
            return nullptr;
        }
        return &(*pages_[page])[index % kPageSize];
    }

    // The slot at `index`, allocating its page; null past the capacity.
    Slot* Ensure(std::size_t index) {
        if (index >= kCapacity) {
            return nullptr;
        }
        const std::size_t page = index / kPageSize;
        if (page >= pages_.size()) {
            pages_.resize(page + 1U);
        }
        if (!pages_[page]) {
            pages_[page] = std::make_unique<Page>();
        }
        return &(*pages_[page])[index % kPageSize];
    }

    // Calls `visit(index, slot)` for each slot of every allocated page, in index order.
    template <typename Visit>
    void ForEachAllocated(Visit&& visit) {
        for (std::size_t page = 0U; page < pages_.size(); page += 1U) {
            if (!pages_[page]) {
                continue;
            }
            for (std::size_t offset = 0U; offset < kPageSize; offset += 1U) {
                visit(static_cast<std::uint32_t>((page * kPageSize) + offset), (*pages_[page])[offset]);
            }
        }
    }

    // Frees every page.
    void Clear() {
        pages_.clear();
    }

private:
    static_assert(kCapacity % kPageSize == 0U, "the capacity must be a whole number of pages");

    using Page = std::array<Slot, kPageSize>;
    std::vector<std::unique_ptr<Page>> pages_{};
};

} // namespace effindom::v2::ui
