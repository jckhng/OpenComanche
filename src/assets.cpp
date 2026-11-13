#include "comanchecd/assets.hpp"
#include "resource_reader.hpp"
#include "cd_executable_layout.hpp"
#include "loading_work.hpp"
#include "mission_work.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace comanchecd {
namespace {
namespace layout = cd_executable;
using Bytes = std::vector<std::uint8_t>;
using layout::packed_bytes;
using layout::image_bytes;
using layout::protected_image;
constexpr std::size_t picture_header_bytes = 128;
constexpr std::size_t picture_width = line_stride * plane_count;
constexpr std::size_t picture_height = 240;
constexpr std::size_t campaign_header_bytes = 23;
constexpr std::size_t campaign_row_bytes = 32;
constexpr std::size_t campaign_limit = 10;
constexpr std::size_t maximum_file_bytes = 16 * 1024 * 1024;
using layout::supported_executable_identity;
constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;
constexpr std::array<const char*, 3> terrain_detail_files{"HIGH.DTL", "MEDIUM.DTL", "LOW.DTL"};

std::string mission_filename(const MissionFilename& field) {
    const auto end = std::find(field.begin(), field.end(), 0);
    auto name = std::string(field.begin(), end);
    name.erase(name.find_last_not_of(' ') + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char code) {
        return static_cast<char>(std::toupper(code));
    });
    return name;
}

std::uint16_t word(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(resource_reader::little_endian(bytes, offset, 2, "Truncated resource word"));
}

std::uint32_t integer(const Bytes& bytes, std::size_t offset) {
    return std::uint32_t(word(bytes, offset)) | (std::uint32_t(word(bytes, offset + 2)) << 16);
}

Bytes slice(const Bytes& bytes, std::size_t offset, std::size_t count) {
    if (offset > bytes.size() || count > bytes.size() - offset) {
        throw std::invalid_argument("Truncated embedded resource");
    }
    return Bytes(bytes.begin() + offset, bytes.begin() + offset + count);
}

Bytes table(const Bytes& bytes, std::size_t offset) {
    if (offset >= bytes.size()) {
        throw std::invalid_argument("Missing menu table");
    }
    const auto end = std::find(bytes.begin() + offset, bytes.end(), 255);
    if (end == bytes.end()) {
        throw std::invalid_argument("Unterminated menu table");
    }
    return Bytes(bytes.begin() + offset, end + 1);
}

class PackedReader {
public:
    PackedReader(const Bytes& file, std::size_t offset) : file_(file), offset_(offset), bits_(word(file, offset)) {
        offset_ += 2;
    }

    std::uint8_t byte() {
        if (offset_ >= file_.size()) {
            throw std::invalid_argument("Truncated LZ91 stream");
        }
        return file_[offset_++];
    }

    unsigned bit() {
        const auto value = bits_ & 1;
        bits_ >>= 1;
        if (--remaining_ == 0) {
            bits_ = word(file_, offset_);
            offset_ += 2;
            remaining_ = 16;
        }
        return value;
    }

private:
    const Bytes& file_;
    std::size_t offset_;
    std::uint16_t bits_;
    unsigned remaining_ = 16;
};

Bytes unpack(const Bytes& file) {
    // Version guard: FNV-1a identifies the supported packed CD executable.
    std::uint64_t identity = fnv_offset_basis;
    for (const auto byte : file) {
        identity = (identity ^ byte) * fnv_prime;
    }
    if (file.size() != packed_bytes || identity != supported_executable_identity) {
        throw std::invalid_argument("Unsupported COMANCHE.EXE version");
    }
    const auto code = std::size_t(word(file, 8) + word(file, 22)) * 16;
    const auto compressed_paragraphs = word(file, code + 8);
    if (code < std::size_t(compressed_paragraphs) * 16) {
        throw std::invalid_argument("Invalid LZ91 stream offset");
    }
    PackedReader input(file, code - std::size_t(compressed_paragraphs) * 16);
    Bytes output;
    output.reserve(image_bytes);
    while (output.size() <= image_bytes) {
        if (input.bit() != 0) {
            output.push_back(input.byte());
            continue;
        }
        unsigned length;
        int displacement;
        if (input.bit() == 0) {
            const auto high = input.bit();
            length = (high << 1) + input.bit() + 2;
            displacement = int(input.byte()) - 256;
        } else {
            const auto low = input.byte();
            const auto high = input.byte();
            displacement = int(low | ((high & 0xf8) << 5) | 0xe000) - 65536;
            length = (high & 7) + 2;
            if (length == 2) {
                length = input.byte();
                if (length == 0) {
                    if (output.size() != image_bytes) {
                        throw std::invalid_argument("Unexpected unpacked image size");
                    }
                    return output;
                }
                if (length == 1) {
                    continue;
                }
                ++length;
            }
        }
        const auto distance = std::size_t(-displacement);
        if (distance > output.size() || length > image_bytes - output.size()) {
            throw std::invalid_argument("Invalid LZ91 back reference");
        }
        for (unsigned index = 0; index < length; ++index) {
            output.push_back(output[output.size() - distance]);
        }
    }
    throw std::invalid_argument("Unterminated LZ91 image");
}

Bytes decode_mission(Bytes bytes) {
    if (bytes.empty()) {
        throw std::invalid_argument("Empty mission file");
    }
    if (bytes.front() == '{') {
        return bytes;
    }
    const std::array<std::uint32_t, 2> keys = bytes.front() == 'x' ?
        std::array<std::uint32_t, 2>{0x11120603, 0x11120603} :
        std::array<std::uint32_t, 2>{0x3aa2a6a3, 0x7eaa3d8a};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] ^= static_cast<std::uint8_t>(keys[(index / 4) % 2] >> ((index % 4) * 8));
    }
    return bytes;
}
}

FrontendPicture decode_frontend_picture(const std::vector<std::uint8_t>& file) {
    constexpr unsigned credits_height = 245;
    if (file.size() < picture_header_bytes || word(file, 8) + 1 != picture_width ||
            (word(file, 10) + 1 != picture_height && word(file, 10) + 1 != credits_height)) {
        throw std::invalid_argument("Unsupported frontend picture dimensions");
    }
    const auto decoded = decode_payload(file, PayloadKind::picture, PayloadPolicy::strict_frontend);
    FrontendPicture result{};
    std::size_t offset = 0;
    for (auto& color : result.palette) {
        for (auto& channel : color) {
            channel = decoded.palette[offset++];
        }
    }
    for (std::size_t plane = 0; plane < plane_count; ++plane) {
        result.background[plane].resize(background_bytes, 0);
        for (std::size_t pixel = plane; pixel < std::min(decoded.bytes.size(), background_bytes * plane_count); pixel += plane_count) {
            result.background[plane][pixel / plane_count] = decoded.bytes[pixel];
        }
    }
    return result;
}

Installation::Installation(std::filesystem::path directory, std::filesystem::path payload_directory)
    : directory_(std::move(directory)), payload_directory_(std::move(payload_directory)) {}

std::vector<std::uint8_t> Installation::read(const std::string& name) const {
    auto normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char code) {
        return static_cast<char>(std::toupper(code));
    });
    if (normalized.empty() || normalized.find_first_of("/\\:") != std::string::npos || normalized.find("..") != std::string::npos) {
        throw std::invalid_argument("Expected an installation resource filename");
    }
    auto path = directory_ / normalized;
    if (!std::filesystem::exists(path) && !payload_directory_.empty()) {
        path = payload_directory_ / normalized;
    }
    if (std::filesystem::file_size(path) > maximum_file_bytes) {
        throw std::invalid_argument("Resource exceeds supported file size: " + name);
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    return Bytes(std::istreambuf_iterator<char>(file), {});
}

FrontendAssets Installation::load_frontend() const {
    const auto image = unpack(read("COMANCHE.EXE"));
    const auto resource = [&](std::size_t offset, std::size_t count) {
        return slice(image, protected_image + offset, count);
    };
    FrontendAssets assets{};
    assets.font = resource(layout::frontend::font, layout::frontend::font_end - layout::frontend::font);
    assets.pilot.menu_header = resource(layout::frontend::pilot_header, layout::frontend::pilot_header_bytes);
    assets.pilot.editor_prompt = resource(layout::frontend::editor_prompt, layout::frontend::editor_prompt_bytes);
    const auto copy_resource = [&](std::size_t offset, auto& target) {
        const auto bytes = resource(offset, target.size());
        std::copy(bytes.begin(), bytes.end(), target.begin());
    };
    copy_resource(layout::frontend::row_prefix, assets.pilot.row_prefix);
    copy_resource(layout::frontend::row_tail, assets.pilot.row_tail);
    copy_resource(layout::frontend::glyph_widths, assets.pilot.glyph_widths);
    copy_resource(layout::frontend::normal_keys, assets.pilot.normal_keys);
    copy_resource(layout::frontend::shifted_keys, assets.pilot.shifted_keys);
    copy_resource(layout::transfer, assets.transfer);
    assets.pilot_record = read("COMANCHE.NAM");
    PilotStorage checked(assets.pilot_record);
    assets.main_menu = table(image, protected_image + layout::frontend::main_menu);
    assets.mission_menu = table(image, protected_image + layout::frontend::mission_menu);
    assets.briefing_choices = table(image, protected_image + layout::frontend::briefing_choices);
    assets.loading_prompt = table(image, protected_image + layout::frontend::loading_prompt);
    assets.statistics = resource(layout::frontend::statistics, layout::frontend::statistics_bytes);
    assets.locked_prompt = table(image, protected_image + layout::frontend::locked_prompt);
    assets.settings.header = table(image, protected_image + layout::settings::header);
    const auto& settings_menus = layout::settings::category_menus;
    for (std::size_t category = 0; category < settings_menus.size(); ++category) {
        auto& menu = assets.settings.menus[category];
        menu = table(image, protected_image + settings_menus[category]);
        unsigned width = 0, maximum = 0;
        // 29998 skips the initial marker before measuring the first row.
        for (std::size_t index = 1; index < menu.size(); ++index) {
            const auto code = menu[index];
            if (code == 0 || code == 255) {
                maximum = std::max(maximum, width);
                width = 0;
                ++assets.settings.rows[category];
            } else {
                width += assets.pilot.glyph_widths[code];
            }
        }
        assets.settings.widths[category] = static_cast<std::uint8_t>((maximum + 18) >> 2);
    }
    assets.settings.about = table(image, protected_image + layout::settings::about);
    assets.settings.saved = table(image, protected_image + layout::settings::saved);
    assets.debrief.text = resource(layout::frontend::debrief_text, layout::frontend::debrief_text_bytes);
    for (std::size_t index = 0; index < assets.debrief.messages.size(); ++index) {
        assets.debrief.messages[index] = resource(layout::frontend::debrief_messages + index * layout::frontend::debrief_message_bytes,
            layout::frontend::debrief_message_bytes);
    }
    for (std::size_t index = 0; index < assets.gameplay_presets.size(); ++index) {
        const auto offset = protected_image + layout::frontend::gameplay_presets + index * 4;
        assets.gameplay_presets[index] = integer(image, offset);
    }
    for (std::size_t index = 0; index < assets.mission_resources.size(); ++index) {
        const auto pointer = protected_image + layout::frontend::mission_resources + index * 4;
        const std::size_t address = integer(image, pointer);
        copy_resource(address, assets.mission_resources[index]);
    }
    const auto campaign = table(image, protected_image + layout::frontend::campaign_menu);
    assets.campaign_menu = slice(campaign, 0, campaign_header_bytes);
    for (std::size_t index = 0; index < campaign_limit; ++index) {
        const auto name = std::to_string(index) + ".MIS";
        if (!std::filesystem::exists(directory_ / name)) {
            break;
        }
        const auto mission = decode_mission(read(name));
        const auto start = std::find(mission.begin(), mission.end(), '{');
        const auto end = std::find(start, mission.end(), '}');
        if (start == mission.end() || end == mission.end() || end - start - 1 > 29) {
            throw std::invalid_argument("Invalid campaign title: " + name);
        }
        auto row = slice(campaign, campaign_header_bytes + index * campaign_row_bytes, campaign_row_bytes);
        std::copy(start + 1, end, row.begin() + 2);
        assets.campaign_menu.insert(assets.campaign_menu.end(), row.begin(), row.end());
        ++assets.campaign_count;
    }
    if (assets.campaign_count == 0) {
        throw std::invalid_argument("No supported campaigns found");
    }
    assets.campaign_menu.insert(assets.campaign_menu.end(), {0, 0, 255});
    return assets;
}

MenuSettings Installation::load_menu_settings() const {
    if (std::filesystem::exists(directory_ / "COMANCHE.MNU")) {
        return MenuSettings(read("COMANCHE.MNU"));
    }
    const auto image = unpack(read("COMANCHE.EXE"));
    return MenuSettings(slice(image, protected_image + layout::settings::record, MenuSettings::record_bytes));
}

KdvMovie Installation::load_overview() const {
    return KdvMovie(load_payload("HANGER.KDV", PayloadKind::resource).bytes);
}

DemoRecording Installation::load_demo(std::uint8_t index) const {
    constexpr std::uint8_t demo_count = 4;
    if (index >= demo_count) {
        throw std::invalid_argument("Demo index exceeds the original attract cycle");
    }
    return DemoRecording(read("DEMO" + std::to_string(index) + ".SCR"));
}

MusicPlaylist Installation::load_menu_music() const {
    return {read("MENUMUS1.MS3"), read("MENUMUS2.MS3")};
}

PilotMenuAssets Installation::load_pilot_menu() const {
    const auto resource = [&](const char* name) {
        return StartupResource{read(name), std::filesystem::exists(directory_ / name) ?
            StartupLocation::local : StartupLocation::cdrom};
    };
    return {{resource("MENUMUS1.MS3"), resource("MENUMUS2.MS3")}, resource("RAH66.DTA")};
}

DemoCinematics Installation::load_demo_cinematics() const {
    const auto payload = [&](const char* name) { return load_payload(name, PayloadKind::resource).bytes; };
    return {KdvMovie(payload("TANIM.KDV")), KdvMovie(payload("TITLE.KDV")), load_overview(),
            decode_frontend_picture(read("BACK.DTA")), decode_frontend_picture(read("HANGER.DTA")),
            payload("FANFARE.RLE"), payload("M_CANYON.RLE"), read("MENUMUS2.MS3"),
            payload("SND1.RLE"), read("TANIM.SND")};
}

FanfareAssets Installation::load_fanfare() const {
    FanfareAssets result;
    result.score = read("FANFARE.RLE");
    result.picture = read("BACK.DTA");
    result.score_location = std::filesystem::exists(directory_ / "FANFARE.RLE") ? StartupLocation::local : StartupLocation::cdrom;
    result.picture_location = std::filesystem::exists(directory_ / "BACK.DTA") ? StartupLocation::local : StartupLocation::cdrom;
    const auto image = unpack(read("COMANCHE.EXE"));
    const auto transfer = slice(image, protected_image + layout::transfer, result.transfer.size());
    std::copy(transfer.begin(), transfer.end(), result.transfer.begin());
    return result;
}

StartupMovieAssets Installation::load_startup_movie() const {
    const auto resource = [&](const char* name) {
        return StartupResource{read(name), std::filesystem::exists(directory_ / name) ?
            StartupLocation::local : StartupLocation::cdrom};
    };
    return {resource("TANIM.SND"), resource("M_CANYON.RLE"), resource("SND1.RLE"), resource("TANIM.KDV")};
}

MusicPlaylist Installation::load_debrief_music(const MissionResources& resources, const MissionResult& result) const {
    constexpr std::size_t success_music = 8, failure_music = 10;
    const auto& field = resources[result.survived && result.completed ? success_music : failure_music];
    return {{}, load_payload(mission_filename(field), PayloadKind::resource).bytes};
}

FrontendPicture Installation::load_mission_picture(const MissionFilename& field) const {
    return decode_frontend_picture(read(mission_filename(field)));
}

FrontendPicture Installation::load_debrief(const MissionResources& resources, FrontendScene scene, MissionAudio& audio) const {
    if (scene != FrontendScene::debrief && scene != FrontendScene::debrief_success) {
        throw std::invalid_argument("Debrief resources require a mission result scene");
    }
    constexpr unsigned filename_bytes = 13;
    const auto picture_slot = scene == FrontendScene::debrief_success ? 7u : 9u;
    const auto name = mission_filename(resources[picture_slot]);
    const auto encoded = read(name);
    auto picture = decode_frontend_picture(encoded);
    auto& clock = audio.execution_clock();
    clock.execute(CpuOperation::instructions, 2); // Picture destination and CALL.
    const auto payload = audio.load_mission_payload({encoded, std::filesystem::exists(directory_ / name) ?
        StartupLocation::local : StartupLocation::cdrom}, PayloadKind::picture, filename_bytes);
    clock.execute(CpuOperation::instructions, 1);
    auto score = load_payload(mission_filename(resources[picture_slot + 1]), PayloadKind::resource, audio, filename_bytes);
    clock.execute(CpuOperation::instructions, scene == FrontendScene::debrief_success ? 1 : 2);
    audio.present_debrief({{}, std::move(score.bytes)}, payload.height);
    picture.presentation = PicturePresentation::complete;
    return picture;
}

FrontendPicture Installation::load_picture(FrontendScene scene) const {
    return decode_frontend_picture(read(scene == FrontendScene::operations ? "RAH66.DTA" :
        scene == FrontendScene::campaign ? "MISSION.DTA" : scene == FrontendScene::credits ? "CREDIT1.DTA" :
        scene == FrontendScene::overview ? "HANGER.DTA" : "LOSE4.DTA"));
}

StartupCampaign Installation::campaign_file(std::uint8_t campaign) const {
    if (campaign >= campaign_limit) {
        throw std::invalid_argument("Campaign file index out of range");
    }
    const auto name = std::to_string(campaign) + ".MIS";
    StartupCampaign file;
    file.encoded = read(name);
    file.decoded = decode_mission(file.encoded);
    file.find_location = !payload_directory_.empty() && std::filesystem::exists(payload_directory_ / name) ?
        StartupLocation::cdrom : StartupLocation::local;
    file.read_location = std::filesystem::exists(directory_ / name) ? StartupLocation::local : StartupLocation::cdrom;
    return file;
}

CampaignMissions Installation::load_missions(std::uint8_t campaign) const {
    return parse_campaign(campaign_file(campaign).decoded);
}

CampaignMissions Installation::load_missions(std::uint8_t campaign, MissionAudio& audio, CampaignLoad phase) const {
    const auto file = campaign_file(campaign);
    auto result = parse_campaign(file.decoded);
    audio.load_campaign(file);
    if (phase == CampaignLoad::catalog) {
        mission_work::catalog(result, [&](CpuOperation op, unsigned n) { audio.execution_clock().execute(op, n); });
    }
    return result;
}

GameplayPayload Installation::load_payload(const std::string& name, PayloadKind kind) const {
    return decode_payload(read(name), kind);
}

GameplayPayload Installation::load_payload(const std::string& name, PayloadKind kind, MissionAudio& audio, unsigned filename_bytes) const {
    return audio.load_mission_payload({read(name), std::filesystem::exists(directory_ / name) ?
        StartupLocation::local : StartupLocation::cdrom}, kind, filename_bytes);
}

FrontendPicture Installation::load_campaign_picture(MissionAudio& audio) const {
    const auto encoded = read("MISSION.DTA");
    auto result = decode_frontend_picture(encoded);
    audio.execution_clock().execute(CpuOperation::instructions, 2); // Destination and 2c422 CALL.
    audio.load_menu_picture({encoded, std::filesystem::exists(directory_ / "MISSION.DTA") ?
        StartupLocation::local : StartupLocation::cdrom}, 12);
    result.presentation = PicturePresentation::complete;
    return result;
}

GameplayPayload Installation::load_picture_payload(const std::string& name, ExecutionClock& clock) const {
    return loading_work::picture(read(name), clock);
}

SceneEntry Installation::load_scene_entry() const {
    const auto image = unpack(read("COMANCHE.EXE"));
    SceneEntry result;
    for (std::size_t slot = 0; slot < scene_slots; ++slot) {
        auto& entry = result.entities[slot];
        entry.previous_x = integer(image, protected_image + layout::scene::previous_x + slot * 4);
        entry.previous_y = integer(image, protected_image + layout::scene::previous_y + slot * 4);
        entry.previous_height = image.at(protected_image + layout::scene::previous_height + slot);
        entry.animation_frame = image.at(protected_image + layout::scene::animation_frame + slot);
        entry.rotor_phase = image.at(protected_image + layout::scene::rotor_phase + slot);
    }
    return result;
}

CockpitTables Installation::load_cockpit_tables() const {
    const auto image = unpack(read("COMANCHE.EXE"));
    CockpitTables result;
    for (std::size_t index = 0; index < result.panel_y.size(); ++index) {
        result.panel_y[index] = image.at(protected_image + layout::cockpit::panel_y + index);
        result.left_panel_x[index] = image.at(protected_image + layout::cockpit::left_panel_x + index);
        result.right_panel_x[index] = image.at(protected_image + layout::cockpit::right_panel_x + index);
        result.weapon_flash[index] = image.at(protected_image + layout::cockpit::weapon_flash + index);
    }
    for (std::size_t index = 0; index < result.shake_displacement.size(); ++index) {
        result.shake_displacement[index] = integer(image, protected_image + layout::cockpit::shake_displacement + index * 4);
    }
    result.map_scale = integer(image, protected_image + layout::cockpit::map_scale);
    result.chaff = image.at(protected_image + layout::cockpit::chaff);
    result.flares = image.at(protected_image + layout::cockpit::flares);
    result.center_marker = image.at(protected_image + layout::settings::center_marker) != layout::settings::disabled_flag;
    result.heading_tape = image.at(protected_image + layout::settings::heading_tape) != layout::settings::disabled_flag;
    result.flight_hud = image.at(protected_image + layout::settings::flight_hud) != layout::settings::disabled_flag;
    return result;
}

TerrainTables Installation::load_terrain_tables(TerrainDetail detail) const {
    const auto image = unpack(read("COMANCHE.EXE"));
    const auto index = static_cast<std::size_t>(detail);
    if (index >= terrain_detail_files.size()) {
        throw std::invalid_argument("Unsupported terrain detail setting");
    }
    const auto distances = read(terrain_detail_files[index]);
    constexpr std::size_t detail_bytes = 4 + 5 * terrain_distance_capacity * 4;
    if (distances.size() != detail_bytes) {
        throw std::invalid_argument("Invalid terrain detail resource size");
    }
    TerrainTables result;
    result.levels = integer(distances, 0);
    for (std::size_t sample = 0; sample < result.sine.size(); ++sample) {
        result.sine[sample] = static_cast<std::int32_t>(integer(image, protected_image + layout::terrain::sine + sample * 4));
    }
    for (std::size_t table = 0; table < result.distance.size(); ++table) {
        for (std::size_t level = 0; level < terrain_distance_capacity; ++level) {
            result.distance[table][level] = integer(distances, 4 + (table * terrain_distance_capacity + level) * 4);
        }
    }
    for (std::size_t octant = 0; octant < result.angle_octants.size(); ++octant) {
        result.angle_octants[octant] = integer(image, protected_image + layout::terrain::angle_octants + octant * 4);
    }
    for (std::size_t ratio = 0; ratio < result.angle_ratio.size(); ++ratio) {
        result.angle_ratio[ratio] = image.at(protected_image + layout::terrain::angle_ratio + ratio);
    }
    for (std::size_t scale = 0; scale < result.sprite_offsets.size(); ++scale) {
        result.sprite_offsets[scale] = integer(image, protected_image + layout::terrain::sprite_offsets + scale * 4);
    }
    result.sprite_steps = slice(image, protected_image + layout::terrain::sprite_steps, layout::terrain::sprite_step_bytes);
    result.initial_height_gap = image.at(protected_image + layout::terrain::initial_height_gap);
    for (std::size_t index = 0; index < result.gust.size(); ++index) {
        result.gust[index] = static_cast<std::int8_t>(image.at(protected_image + layout::terrain::gust + index));
    }
    for (std::size_t index = 0; index < result.transfer.size(); ++index) {
        result.transfer[index] = image.at(protected_image + layout::transfer + index);
    }
    return result;
}

StartupFiles Installation::load_startup_files() const {
    StartupFiles result;
    if (std::filesystem::exists(directory_ / "COMANCHE.CDR")) {
        result.cdrom = read("COMANCHE.CDR");
    }
    result.configuration = read("COMANCHE.CFG");
    result.campaigns.emplace();
    for (unsigned index = 0; index < campaign_limit; ++index) {
        const auto name = std::to_string(index) + ".MIS";
        const bool local = std::filesystem::exists(directory_ / name);
        const bool cdrom = !payload_directory_.empty() && std::filesystem::exists(payload_directory_ / name);
        if (!local && !cdrom) {
            result.campaigns->emplace_back();
            continue;
        }
        result.campaigns->push_back(campaign_file(static_cast<std::uint8_t>(index)));
    }
    result.loading.emplace();
    auto& loading = *result.loading;
    if (std::filesystem::exists(directory_ / "COMANCHE.NAM")) { loading.pilots = read("COMANCHE.NAM"); }
    if (std::filesystem::exists(directory_ / "COMANCHE.MNU")) { loading.settings = read("COMANCHE.MNU"); }
    const auto image = unpack(read("COMANCHE.EXE"));
    loading.active_settings = loading.settings ? *loading.settings : slice(image, protected_image + layout::settings::record, MenuSettings::record_bytes);
    const MenuSettings settings(loading.active_settings);
    const auto name = terrain_detail_files.at(settings.detail());
    loading.detail = read(name);
    loading.detail_location = std::filesystem::exists(directory_ / name) ? StartupLocation::local : StartupLocation::cdrom;
    const auto& menu_addresses = layout::settings::loading_menus;
    const auto& flag_addresses = layout::settings::flag_addresses;
    const auto& flag_fields = layout::settings::flag_fields;
    auto menu_image = image;
    for (std::size_t index = 0; index < flag_fields.size(); ++index) {
        menu_image[protected_image + flag_addresses[index]] = loading.active_settings[flag_fields[index]];
    }
    for (std::size_t index = 0; index < menu_addresses.size(); ++index) {
        loading.menus[index] = table(menu_image, protected_image + menu_addresses[index]);
    }
    const auto widths = slice(image, protected_image + layout::frontend::glyph_widths, loading.glyph_widths.size());
    std::copy(widths.begin(), widths.end(), loading.glyph_widths.begin());
    return result;
}

AudioTables Installation::load_audio() const {
    const auto image = unpack(read("COMANCHE.EXE"));
    AudioTables result;
    for (std::size_t index = 0; index < result.instruments.size(); ++index) {
        const auto data = slice(image, protected_image + layout::audio::instruments + index * layout::audio::instrument_bytes,
            layout::audio::instrument_bytes);
        std::copy(data.begin(), data.end(), result.instruments[index].begin());
        result.pitch[index] = image.at(protected_image + layout::audio::pitch + index);
        result.volume[index] = image.at(protected_image + layout::audio::volume + index);
        const auto pointer = protected_image + layout::audio::envelopes + index * 4;
        auto position = protected_image + integer(image, pointer);
        for (std::size_t count = 0; count < layout::audio::envelope_scan_limit; ++count) {
            const auto value = image.at(position++);
            result.envelopes[index].push_back(value);
            if (value == 0x80 || value == 0x81) {
                break;
            }
        }
        if (result.envelopes[index].back() != 0x80 && result.envelopes[index].back() != 0x81) {
            throw std::invalid_argument("Unterminated audio envelope");
        }
    }
    for (std::size_t index = 0; index < result.frequencies.size(); ++index) {
        result.frequencies[index] = word(image, protected_image + layout::audio::frequencies + index * 2);
    }
    const auto conversion = slice(image, protected_image + layout::audio::sample_conversion, result.sample_conversion.size());
    std::copy(conversion.begin(), conversion.end(), result.sample_conversion.begin());
    const auto soft_conversion = slice(image, protected_image + layout::audio::soft_sample_conversion, result.soft_sample_conversion.size());
    std::copy(soft_conversion.begin(), soft_conversion.end(), result.soft_sample_conversion.begin());
    return result;
}

void Installation::save_pilots(const std::vector<std::uint8_t>& record) const {
    if (record.size() != pilot_record_bytes) {
        throw std::invalid_argument("Expected complete pilot persistence record");
    }
    std::ofstream file(directory_ / "COMANCHE.NAM", std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(record.data()), static_cast<std::streamsize>(record.size()));
    if (!file) {
        throw std::runtime_error("Cannot save COMANCHE.NAM");
    }
}

} // namespace comanchecd
