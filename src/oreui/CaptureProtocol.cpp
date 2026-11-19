#include "oreui/CaptureProtocol.h"
#include "structure/capture/CaptureBounds.h"
#include "structure/capture/SelectionJson.h"
#include "nlohmann/json.hpp"
#include <limits>

namespace lholo::oreui {
std::optional<structure::capture::Request> parseCaptureIntent(std::string_view payload) {
    using namespace structure::capture;
    if (payload.size() > 262144) return std::nullopt;
    try {
        auto const value = nlohmann::json::parse(payload);
        auto validId = [](auto const& number) {
            return number.is_number_unsigned() && number.template get<std::uint64_t>() <= 9007199254740991ULL;
        };
        if (!value.is_object() || !validId(value.at("id")) || !validId(value.at("revision"))
            || value.at("id").get<std::uint64_t>() == 0) return std::nullopt;
        Request request;
        request.id = value.at("id").get<std::uint64_t>();
        request.revision = value.at("revision").get<std::uint64_t>();
        auto const action = value.at("action").get<std::string>();
        if (action == "exportEmpty") {
            auto const& size = value.at("size");
            std::array const axes{"x", "y", "z"};
            for (std::size_t index = 0; index < axes.size(); ++index) {
                auto const& extent = size.at(axes[index]);
                if (!extent.is_number_unsigned()) return std::nullopt;
                auto const length = extent.get<std::uint64_t>();
                if (length == 0 || length > std::numeric_limits<int>::max()) return std::nullopt;
                request.size[index] = static_cast<int>(length);
            }
            if (!emptyStructureVolume(request.size)) return std::nullopt;
            request.action = Action::ExportEmpty;
            return request;
        }
        if (action == "apply") request.action = Action::Apply;
        else if (action == "first") request.action = Action::FirstFromPlayer;
        else if (action == "second") request.action = Action::SecondFromPlayer;
        else if (action == "selection") request.action = Action::SelectionCommand;
        else if (action == "originPlayer") request.action = Action::OriginFromPlayer;
        else if (action == "clear") request.action = Action::Clear;
        else if (action == "export") request.action = Action::Export;
        else return std::nullopt;
        request.draft = selectionDraft(value.at("draft"));
        syncSelectedRegion(request.draft);
        if (request.action == Action::SelectionCommand) {
            request.command=value.at("command");request.value=value.value("value","");
            if (request.command.size()>32||request.value.size()>256) return {};
        }
        if (request.action == Action::Export && selectionBounds(request.draft).empty()) return {};
        return request;
    } catch (std::exception const&) { return std::nullopt; }
}
}
