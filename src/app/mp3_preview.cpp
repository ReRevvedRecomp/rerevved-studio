#include "mp3_preview.h"

#include <imgui.h>

#include <cstdint>

namespace rerevved::studio
{

void DrawMp3Preview(const Mp3Document* document, std::string_view error)
{
    ImGui::Begin("Preview");
    if (!document)
    {
        if (error.empty())
            ImGui::TextWrapped("The MP3 audio could not be previewed.");
        else
            ImGui::TextWrapped("%.*s", static_cast<int>(error.size()), error.data());
        ImGui::End();
        return;
    }

    ImGui::Text("%u Hz, %u channel%s",
                static_cast<unsigned int>(document->sample_rate),
                static_cast<unsigned int>(document->channels),
                document->channels == 1 ? "" : "s");
    ImGui::Text("Waveform window: %.2f seconds%s",
                static_cast<double>(document->preview_frame_count) / document->sample_rate,
                document->preview_complete ? "" : " (limited)");
    ImGui::PlotLines("##mp3-waveform",
                     document->waveform.data(),
                     static_cast<int>(document->waveform.size()),
                     0,
                     nullptr,
                     -1.0F,
                     1.0F,
                     ImVec2(-1.0F, 180.0F));
    ImGui::TextUnformatted("Read-only decoded amplitude preview; playback is not supported.");
    ImGui::End();
}

} // namespace rerevved::studio
