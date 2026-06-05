#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>

#include "SKSE/SKSE.h"

#include <xbyak/xbyak.h>

namespace HookUtil {
enum class HookWriteResult {
    kSuccess,
    kInsufficientTrampoline,
    kBranchOutOfRange,
};

struct ExistingBranch {
    std::uintptr_t target {0};
    std::size_t patchSize {0};
    std::string_view name;
};

[[nodiscard]] inline std::string_view GetFailureReason(const HookWriteResult a_result) {
    switch (a_result) {
        case HookWriteResult::kInsufficientTrampoline: return "trampolineUnavailable"sv;
        case HookWriteResult::kBranchOutOfRange:       return "branchOutOfRange"sv;
        case HookWriteResult::kSuccess:                return "none"sv;
    }

    return "unknown"sv;
}

[[nodiscard]] inline std::optional<std::int32_t> GetRelativeDisplacement(
    const std::uintptr_t a_srcEnd,
    const std::uintptr_t a_dst
) {
    constexpr auto kMin = static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max()) + 1;
    constexpr auto kMax = static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max());

    if (a_dst >= a_srcEnd) {
        const auto delta = a_dst - a_srcEnd;
        if (delta > kMax) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(delta);
    }

    const auto delta = a_srcEnd - a_dst;
    if (delta > kMin) {
        return std::nullopt;
    }
    if (delta == kMin) {
        return std::numeric_limits<std::int32_t>::min();
    }

    return -static_cast<std::int32_t>(delta);
}

template <class T>
[[nodiscard]] T ReadUnaligned(const std::uintptr_t a_address) {
    T value;
    std::memcpy(std::addressof(value), reinterpret_cast<const void*>(a_address), sizeof(T));
    return value;
}

[[nodiscard]] inline std::optional<ExistingBranch> DecodeBranch(const std::uintptr_t a_src) {
    const auto opcode = ReadUnaligned<std::uint8_t>(a_src);
    if (opcode == 0xE9) {
        const auto disp = ReadUnaligned<std::int32_t>(a_src + 1);
        return ExistingBranch {
            .target = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(a_src + 5) + disp),
            .patchSize = 5,
            .name = "E9"sv,
        };
    }

    const auto modrm = ReadUnaligned<std::uint8_t>(a_src + 1);
    if (opcode == 0xFF && modrm == 0x25) {
        const auto disp = ReadUnaligned<std::int32_t>(a_src + 2);
        const auto pointerAddress = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(a_src + 6) + disp);
        return ExistingBranch {
            .target = ReadUnaligned<std::uintptr_t>(pointerAddress),
            .patchSize = 6,
            .name = "FF25"sv,
        };
    }

    return std::nullopt;
}

template <std::size_t BYTES>
[[nodiscard]] HookWriteResult WriteBranch(const std::uintptr_t a_src, const std::uintptr_t a_dst) {
    static_assert(BYTES == 5 || BYTES == 6);

    auto& trampoline = SKSE::GetTrampoline();
    if constexpr (BYTES == 5) {
#pragma pack(push, 1)
        struct SrcAssembly {
            std::uint8_t opcode;
            std::int32_t disp;
        };

        struct DstAssembly {
            std::uint8_t opcode;
            std::uint8_t modrm;
            std::int32_t disp;
            std::uint64_t address;
        };
#pragma pack(pop)

        if (trampoline.free_size() < sizeof(DstAssembly)) {
            return HookWriteResult::kInsufficientTrampoline;
        }

        auto* const dstAssembly = static_cast<DstAssembly*>(trampoline.allocate(sizeof(DstAssembly)));
        const auto disp = GetRelativeDisplacement(
            a_src + sizeof(SrcAssembly),
            reinterpret_cast<std::uintptr_t>(dstAssembly)
        );
        if (!disp) {
            return HookWriteResult::kBranchOutOfRange;
        }

        dstAssembly->opcode = 0xFF;
        dstAssembly->modrm = 0x25;
        dstAssembly->disp = 0;
        dstAssembly->address = static_cast<std::uint64_t>(a_dst);

        const SrcAssembly srcAssembly {
            .opcode = 0xE9,
            .disp = *disp,
        };
        REL::safe_write(a_src, std::addressof(srcAssembly), sizeof(srcAssembly));
        return HookWriteResult::kSuccess;
    } else {
#pragma pack(push, 1)
        struct SrcAssembly {
            std::uint8_t opcode;
            std::uint8_t modrm;
            std::int32_t disp;
        };
#pragma pack(pop)

        if (trampoline.free_size() < sizeof(std::uintptr_t)) {
            return HookWriteResult::kInsufficientTrampoline;
        }

        auto* const dstAddress = static_cast<std::uintptr_t*>(trampoline.allocate(sizeof(std::uintptr_t)));
        const auto disp = GetRelativeDisplacement(
            a_src + sizeof(SrcAssembly),
            reinterpret_cast<std::uintptr_t>(dstAddress)
        );
        if (!disp) {
            return HookWriteResult::kBranchOutOfRange;
        }

        *dstAddress = a_dst;

        const SrcAssembly srcAssembly {
            .opcode = 0xFF,
            .modrm = 0x25,
            .disp = *disp,
        };
        REL::safe_write(a_src, std::addressof(srcAssembly), sizeof(srcAssembly));
        return HookWriteResult::kSuccess;
    }
}

template <class T>
[[nodiscard]] HookWriteResult HookExistingBranch(const std::uintptr_t a_src, const ExistingBranch& a_branch) {
    HookWriteResult result;
    if (a_branch.patchSize == 5) {
        result = WriteBranch<5>(a_src, stl::unrestricted_cast<std::uintptr_t>(T::thunk));
    } else {
        result = WriteBranch<6>(a_src, stl::unrestricted_cast<std::uintptr_t>(T::thunk));
    }

    if (result == HookWriteResult::kSuccess) {
        T::func = a_branch.target;
    }
    return result;
}

template <class T, std::size_t BYTES>
[[nodiscard]] HookWriteResult HookFunctionPrologue(const std::uintptr_t a_src, const std::byte* a_originalBytes) {
    static_assert(BYTES == 5 || BYTES == 6);

    struct Patch : Xbyak::CodeGenerator {
        Patch(
            const std::uintptr_t a_originalFuncAddr,
            const std::byte* a_originalBytes,
            const std::size_t a_originalByteLength
        ) {
            for (::std::size_t i = 0; i < a_originalByteLength; ++i) {
                db(::std::to_integer<::std::uint8_t>(a_originalBytes[i]));
            }

            jmp(ptr[rip]);
            dq(a_originalFuncAddr + a_originalByteLength);
        }
    };

    Patch patch(a_src, a_originalBytes, BYTES);
    patch.ready();

    auto& trampoline = SKSE::GetTrampoline();
    constexpr auto kBranchTrampolineSize = BYTES == 5 ? 14 : sizeof(std::uintptr_t);
    if (trampoline.free_size() < patch.getSize() + kBranchTrampolineSize) {
        return HookWriteResult::kInsufficientTrampoline;
    }

    auto* const alloc = trampoline.allocate(patch.getSize());
    std::memcpy(alloc, patch.getCode(), patch.getSize());

    const auto result = WriteBranch<BYTES>(a_src, stl::unrestricted_cast<std::uintptr_t>(T::thunk));
    if (result != HookWriteResult::kSuccess) {
        return result;
    }

    T::func = reinterpret_cast<std::uintptr_t>(alloc);
    return HookWriteResult::kSuccess;
}
}
