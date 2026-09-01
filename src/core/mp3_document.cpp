#include "mp3_document.h"

#include <dr_mp3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

namespace rerevved::studio
{
namespace
{

// Decoder API from dr_mp3 0.7.3, pinned at:
// https://github.com/mackron/dr_libs/tree/5690d4671d7ad07ae6021756d7222eb159745f06

constexpr std::uint64_t kPreviewSeconds  = 10;
constexpr std::uint32_t kPointsPerSecond = 100;
constexpr std::size_t   kMaximumChannels = 2;

bool HasCompleteFrameStream(std::span<const std::byte> bytes, const drmp3& decoder)
{
    if (decoder.streamStartOffset >= decoder.streamLength ||
        decoder.streamLength > bytes.size())
        return false;

    drmp3dec frame_decoder{};
    drmp3dec_init(&frame_decoder);
    auto       offset = static_cast<std::size_t>(decoder.streamStartOffset);
    const auto end    = static_cast<std::size_t>(decoder.streamLength);
    while (offset < end)
    {
        const auto remaining = end - offset;
        if (remaining < 2 || bytes[offset] != std::byte{ 0xFF } ||
            (std::to_integer<unsigned int>(bytes[offset + 1]) & 0xE0U) != 0xE0U)
            return false;

        drmp3dec_frame_info info{};
        const auto          input_size = static_cast<int>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const auto          frames     = drmp3dec_decode_frame(
            &frame_decoder,
            reinterpret_cast<const drmp3_uint8*>(bytes.data() + offset),
            input_size,
            nullptr,
            &info);
        if (frames <= 0 || info.frame_bytes <= 0 ||
            static_cast<std::size_t>(info.frame_bytes) > remaining)
            return false;
        offset += static_cast<std::size_t>(info.frame_bytes);
    }
    return offset == end;
}

} // namespace

std::string_view Mp3DocumentErrorMessage(Mp3DocumentError error)
{
    switch (error)
    {
        case Mp3DocumentError::decode_failed:
            return "The MP3 audio could not be decoded.";
    }
    return "The MP3 audio is invalid.";
}

std::expected<Mp3Document, Mp3DocumentError>
ParseMp3Document(std::span<const std::byte> bytes)
{
    drmp3 decoder{};
    if (!drmp3_init_memory(&decoder, bytes.data(), bytes.size(), nullptr))
        return std::unexpected(Mp3DocumentError::decode_failed);
    if (!HasCompleteFrameStream(bytes, decoder))
    {
        drmp3_uninit(&decoder);
        return std::unexpected(Mp3DocumentError::decode_failed);
    }

    Mp3Document document{
        .sample_rate         = decoder.sampleRate,
        .channels            = decoder.channels,
        .preview_frame_count = 0,
        .preview_complete    = false,
        .waveform            = {},
    };
    if (document.sample_rate == 0 || document.channels == 0 ||
        document.channels > kMaximumChannels)
    {
        drmp3_uninit(&decoder);
        return std::unexpected(Mp3DocumentError::decode_failed);
    }

    const auto maximum_frames =
        static_cast<std::uint64_t>(document.sample_rate) * kPreviewSeconds;
    document.waveform.reserve(kPointsPerSecond * kPreviewSeconds);

    std::array<float, DRMP3_MAX_PCM_FRAMES_PER_MP3_FRAME * kMaximumChannels> pcm{};
    std::uint64_t                                                            frames_since_point = 0;
    std::uint64_t                                                            next_point_frame =
        (document.sample_rate + kPointsPerSecond - 1U) / kPointsPerSecond;
    float bucket_peak  = 0.0F;
    float bucket_value = 0.0F;
    while (document.preview_frame_count < maximum_frames)
    {
        const auto frames_to_read = std::min<std::uint64_t>(
            DRMP3_MAX_PCM_FRAMES_PER_MP3_FRAME,
            maximum_frames - document.preview_frame_count);
        const auto frames_read =
            drmp3_read_pcm_frames_f32(&decoder, frames_to_read, pcm.data());
        if (frames_read == 0)
        {
            document.preview_complete = true;
            break;
        }

        for (std::uint64_t frame = 0; frame < frames_read; ++frame)
        {
            for (std::uint32_t channel = 0; channel < document.channels; ++channel)
            {
                const auto value =
                    pcm[static_cast<std::size_t>(frame) * document.channels + channel];
                if (!std::isfinite(value))
                {
                    drmp3_uninit(&decoder);
                    return std::unexpected(Mp3DocumentError::decode_failed);
                }
                if (std::abs(value) > bucket_peak)
                {
                    bucket_peak  = std::abs(value);
                    bucket_value = value;
                }
            }
            ++document.preview_frame_count;
            ++frames_since_point;
            if (document.preview_frame_count == next_point_frame)
            {
                document.waveform.push_back(bucket_value);
                frames_since_point = 0;
                bucket_peak        = 0.0F;
                bucket_value       = 0.0F;
                next_point_frame =
                    ((document.waveform.size() + 1) *
                         static_cast<std::uint64_t>(document.sample_rate) +
                     kPointsPerSecond - 1U) /
                    kPointsPerSecond;
            }
        }
    }
    drmp3_uninit(&decoder);

    if (frames_since_point != 0)
        document.waveform.push_back(bucket_value);
    if (document.preview_frame_count == 0)
        return std::unexpected(Mp3DocumentError::decode_failed);
    return document;
}

std::expected<Mp3Document, std::string>
LoadMp3Document(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return std::unexpected("Could not open MP3 file for reading.");

    const auto end = input.tellg();
    if (end < 0)
        return std::unexpected("Could not read MP3 file size.");
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("MP3 file exceeds the supported size range.");

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
        return std::unexpected("Could not read MP3 file data.");
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
        return std::unexpected("MP3 file ended before its reported size.");

    auto document = ParseMp3Document(bytes);
    if (!document)
        return std::unexpected(std::string(Mp3DocumentErrorMessage(document.error())));
    return std::move(*document);
}

} // namespace rerevved::studio
