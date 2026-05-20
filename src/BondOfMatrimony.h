#pragma once

namespace BondOfMatrimony {
void Load();
[[nodiscard]] RE::TESObjectARMO* Get();
[[nodiscard]] bool IsBond(const RE::TESObjectARMO* a_armor);
[[nodiscard]] bool IsBond(RE::FormID a_formID);
}
