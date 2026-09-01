#pragma once

#include "map_document.h"

#include <string_view>

namespace rerevved::studio
{

void DrawMapPreview(const MapDocument* document, std::string_view error);

} // namespace rerevved::studio
