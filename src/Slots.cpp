#include "Slots.h"

#include "Settings.h"

namespace Slots {
RE::BIPED_OBJECTS::BIPED_OBJECT GetBipedObject(const DisplaySlot a_channel) {
    return Settings::GetSingleton()->GetBipedObject(a_channel);
}

RE::BGSBipedObjectForm::BipedObjectSlot GetArmorSlot(const DisplaySlot a_channel) {
    return ToArmorSlot(GetBipedObject(a_channel));
}

RE::BGSBipedObjectForm::FirstPersonFlag GetFirstPersonFlag(const DisplaySlot a_channel) {
    return GetArmorSlot(a_channel);
}

std::uint16_t GetDismemberPartID(const DisplaySlot a_channel) {
    return ToDismemberPartID(GetBipedObject(a_channel));
}
}
