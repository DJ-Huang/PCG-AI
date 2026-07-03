#include "data/pcg_param_data.hpp"

namespace pcg::internal::data {

PcgParamData::PcgParamData(nlohmann::json params)
    : params_(std::move(params))
{
    if (!params_.is_object())
        params_ = nlohmann::json::object();
}

PcgParamData PcgParamData::from_json(const nlohmann::json& json)
{
    if (json.is_object())
        return PcgParamData(json);
    return PcgParamData();
}

} // namespace pcg::internal::data
