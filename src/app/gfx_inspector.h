#pragma once

#include "gfx_document.h"

#include <string_view>

namespace rerevved::studio
{

void DrawGfxInspector(const GfxDocument* document, std::string_view error);

} // namespace rerevved::studio
