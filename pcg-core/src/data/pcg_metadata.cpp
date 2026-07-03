#include "data/pcg_metadata.hpp"

namespace pcg::internal::data {

bool PcgMetadata::has(const std::string& key) const
{
    return attributes_.contains(key);
}

void PcgMetadata::set(const std::string& key, const nlohmann::json& value)
{
    attributes_[key] = value;
}

const nlohmann::json& PcgMetadata::get(const std::string& key) const
{
    return attributes_.at(key);
}

PcgMetadata PcgMetadata::from_json(const nlohmann::json& json)
{
    PcgMetadata meta;
    if (json.is_object())
        meta.attributes_ = json;
    return meta;
}

} // namespace pcg::internal::data
