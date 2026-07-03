#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace pcg::internal::data {

/** Per-point or per-instance attribute bag (prefab, scale, mesh, …). */
class PcgMetadata {
public:
    bool has(const std::string& key) const;
    void set(const std::string& key, const nlohmann::json& value);
    const nlohmann::json& get(const std::string& key) const;
    const nlohmann::json& raw() const { return attributes_; }

    static PcgMetadata from_json(const nlohmann::json& json);

private:
    nlohmann::json attributes_ = nlohmann::json::object();
};

} // namespace pcg::internal::data
