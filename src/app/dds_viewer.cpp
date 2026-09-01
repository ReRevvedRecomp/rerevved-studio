#include "dds_viewer.h"

#include <imgui.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>

namespace rerevved::studio
{

DdsViewer::~DdsViewer()
{
    Reset();
}

void DdsViewer::Clear()
{
    Reset();
}

void DdsViewer::Draw(const DdsDocument* document,
                     std::string_view   source_id,
                     std::string_view   error)
{
    ImGui::Begin("Preview");
    if (document == nullptr)
    {
        Reset();
        ImGui::TextWrapped("%.*s", static_cast<int>(error.size()), error.data());
        ImGui::End();
        return;
    }

    if (loaded_source_ != source_id)
        Upload(*document, source_id);
    if (!upload_error_.empty())
    {
        ImGui::TextWrapped("%s", upload_error_.c_str());
        ImGui::End();
        return;
    }

    ImGui::Text("%u x %u", document->metadata.width, document->metadata.height);
    ImGui::TextUnformatted("Legacy uncompressed 32-bit RGB/RGBA");
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float  width     = static_cast<float>(document->metadata.width);
    const float  height    = static_cast<float>(document->metadata.height);
    const float  scale     = std::min(std::max(available.x, 1.0F) / width,
                                      std::max(available.y, 1.0F) / height);
    const ImVec2 image_size{ width * scale, height * scale };
    ImGui::Image(static_cast<ImTextureID>(texture_), image_size);
    ImGui::End();
}

void DdsViewer::Upload(const DdsDocument& document, std::string_view source_id)
{
    Reset();
    loaded_source_ = source_id;

    GLint maximum_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
    if (document.metadata.width > static_cast<std::uint32_t>(maximum_texture_size) ||
        document.metadata.height > static_cast<std::uint32_t>(maximum_texture_size))
    {
        upload_error_ = "DDS dimensions exceed the OpenGL texture limit.";
        return;
    }

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 static_cast<GLsizei>(document.metadata.width),
                 static_cast<GLsizei>(document.metadata.height),
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 document.rgba8.data());
}

void DdsViewer::Reset()
{
    if (texture_ != 0)
        glDeleteTextures(1, &texture_);
    texture_ = 0;
    loaded_source_.clear();
    upload_error_.clear();
}

} // namespace rerevved::studio
