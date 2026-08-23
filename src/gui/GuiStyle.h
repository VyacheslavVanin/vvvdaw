#pragma once
// Tiny shared GUI style helpers.

#include <QString>

namespace guistyle {

// Combine the normal/checked fragments of a toggle button (S/M/arm/...) into a
// single stylesheet with consistent padding.
inline QString toggleButtonStyle(const QString& normal, const QString& checked) {
    return normal + "; padding: 0px; }"
         + checked + "; padding: 0px; }";
}

} // namespace guistyle