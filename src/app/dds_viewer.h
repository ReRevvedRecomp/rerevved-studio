#pragma once

#include "dds_document.h"

#include <string>
#include <string_view>

namespace rerevved::studio
{

class DdsViewer
{
public:
    DdsViewer() = default;
    ~DdsViewer();

    DdsViewer(const DdsViewer&)            = delete;
    DdsViewer& operator=(const DdsViewer&) = delete;

    void Clear();
    void Draw(const DdsDocument* document, std::string_view source_id, std::string_view error);

private:
    void Upload(const DdsDocument& document, std::string_view source_id);
    void Reset();

    unsigned int texture_ = 0;
    std::string  loaded_source_;
    std::string  upload_error_;
};

} // namespace rerevved::studio
