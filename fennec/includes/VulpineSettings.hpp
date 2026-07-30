#pragma once
#include <string_view>
#include <array>
#include <algorithm>
#include <iterator>

namespace VulpineSettings {
    namespace VulpineVersions {
        constexpr std::string_view v1_0_0 = "1.0.0";
        constexpr std::string_view v1_0_0u = "1.0.0u";
        constexpr std::string_view v1_0_0e = "1.0.0e";

        constexpr std::string_view latest_stable = "latest_stable";
        constexpr std::string_view latest_unstable = "latest_unstable";
        constexpr std::string_view latest_experimental = "latest_experimental";

        constexpr auto valid_versions = std::to_array<std::string_view>({
            v1_0_0, v1_0_0u, v1_0_0e
        });

        inline int getVersionIndex(std::string_view version) {
            auto it = std::find(valid_versions.begin(), valid_versions.end(), version);
            if (it != valid_versions.end()) {
                return static_cast<int>(std::distance(valid_versions.begin(), it));
            }
            return -1; // Not found
        }

        inline bool isVersionInAllowedRange(std::string_view target, 
                                            std::string_view min, 
                                            std::string_view max) {
            int x = getVersionIndex(target);
            int y = getVersionIndex(min);
            int z = getVersionIndex(max);

            if (x == -1 || y == -1 || z == -1) return false;
            return (x > y) && (x <= z);
        }

        inline bool isValidVersion(std::string_view version) {
            return std::find(valid_versions.begin(), valid_versions.end(), version) != valid_versions.end();
        }

        inline std::string_view resolveVersionAlias(std::string_view version) {
            if (version == "latest_stable") return v1_0_0;
            if (version == "latest_unstable") return v1_0_0u;
            if (version == "latest_experimental") return v1_0_0e;
            return version;
        }
    }
}