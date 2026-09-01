#pragma once

#include "nif_document.h"

#include <string_view>

namespace rerevved::studio
{

void DrawNifInspector(const NifDocument* document, std::string_view error);

} // namespace rerevved::studio
