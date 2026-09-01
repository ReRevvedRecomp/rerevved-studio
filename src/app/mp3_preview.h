#pragma once

#include "mp3_document.h"

#include <string_view>

namespace rerevved::studio
{

void DrawMp3Preview(const Mp3Document* document, std::string_view error);

} // namespace rerevved::studio
