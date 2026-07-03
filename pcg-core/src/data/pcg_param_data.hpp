#pragma once

#include <nlohmann/json.hpp>

namespace pcg::internal::data {

/** Param / settings payload (UE PCGParamData analogue). */
class PcgParamData {
public:
    explicit PcgParamData(nlohmann::json params = nlohmann::json::object());

    const nlohmann::json& params() const { return params_; }
    nlohmann::json& params() { return params_; }

    nlohmann::json to_json() const { return params_; }
    static PcgParamData from_json(const nlohmann::json& json);

private:
    nlohmann::json params_;
};

} // namespace pcg::internal::data
