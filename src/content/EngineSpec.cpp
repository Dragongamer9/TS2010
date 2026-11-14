#include "EngineSpec.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace trainz::content {

namespace {

std::optional<double> number(const ConfigNode* n) {
    if (n == nullptr) {
        return std::nullopt;
    }
    if (!n->floats.empty() && std::isfinite(n->floats[0])) {
        return n->floats[0];
    }
    if (!n->ints.empty()) {
        return n->ints[0];
    }
    return std::nullopt;
}

std::optional<int> nonnegativeCount(const ConfigNode* node) {
    const auto value = number(node);
    if (!value || !std::isfinite(*value) || *value < 0 ||
        *value > static_cast<double>(std::numeric_limits<int>::max()) || std::floor(*value) != *value) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

bool numericKey(const std::string& k, double& v) {
    if (k.empty() || k.find_first_not_of("0123456789.-") != std::string::npos) {
        return false;
    }
    try {
        std::size_t used = 0;
        v = std::stod(k, &used);
        return used == k.size() && std::isfinite(v);
    } catch (const std::exception&) {
        return false;
    }
}

// notch -> { speed key -> force value }
std::vector<std::vector<std::pair<double, double>>> curves(const ConfigNode* group) {
    std::vector<std::vector<std::pair<double, double>>> out;
    if (group == nullptr || group->type != ConfigType::Group) {
        return out;
    }
    for (const auto& notch : group->children) {
        double nv = 0;
        if (!numericKey(notch.key, nv) || nv < 0 || nv > 64 || nv != std::floor(nv) || notch.type != ConfigType::Group) {
            continue;
        }
        const auto n = static_cast<std::size_t>(nv);
        if (out.size() <= n) {
            out.resize(n + 1);
        }
        for (const auto& point : notch.children) {
            double speed = 0;
            const auto force = number(&point);
            if (numericKey(point.key, speed) && force && speed >= 0) {
                out[n].push_back({speed, *force});
            }
        }
        std::sort(out[n].begin(), out[n].end());
    }
    return out;
}

}  // namespace

double interpolateCurve(const std::vector<std::pair<double, double>>& c, double x) {
    if (c.empty() || !std::isfinite(x)) {
        return 0;
    }
    if (x <= c.front().first) {
        return c.front().second;
    }
    if (x >= c.back().first) {
        return x == c.back().first || c.size() == 1 ? c.back().second : 0;  // 0 past the end of the curve
    }
    const auto it = std::upper_bound(c.begin(), c.end(), x, [](double v, const auto& p) { return v < p.first; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double f = b.first > a.first ? (x - a.first) / (b.first - a.first) : 0;
    return a.second + f * (b.second - a.second);
}

double EngineSpec::tractiveForceN(std::size_t notch, double speed) const {
    if (notch >= throttlePower.size()) {
        return 0;
    }
    return 1000.0 * std::max(0.0, interpolateCurve(throttlePower[notch], std::fabs(speed)));
}

std::vector<std::pair<double, double>> EngineSpec::fullThrottleCurveN() const {
    std::vector<std::pair<double, double>> out;
    for (auto it = throttlePower.rbegin(); it != throttlePower.rend(); ++it) {
        if (!it->empty()) {
            for (const auto& [s, f] : *it) {
                out.push_back({s, 1000.0 * f});
            }
            break;
        }
    }
    return out;
}

EngineSpec parseEngineSpec(const ConfigNode& root) {
    EngineSpec e;
    if (const ConfigNode* m = root.child("motor"); m != nullptr && m->type == ConfigType::Group) {
        e.maxAccel = number(m->child("max-accel"));
        e.maxDecel = number(m->child("max-decel"));
        e.adhesion = number(m->child("adhesion"));
        e.resistance = number(m->child("resistance"));
        e.maxSpeed = number(m->child("maxspeed"));
        e.brakeRatio = number(m->child("brakeratio"));
        e.axleCount = nonnegativeCount(m->child("axle-count"));
        e.throttleNotches = nonnegativeCount(m->child("throttle-notches"));
    }
    if (const ConfigNode* p = root.child("pressure"); p != nullptr && p->type == ConfigType::Group) {
        double scale = 1;
        if (const auto sc = number(p->child("scale")); sc && *sc > 0 && *sc < 100) {
            scale = *sc;
        }
        auto read = [&](const char* key) -> std::optional<double> {
            const auto v = number(p->child(key));
            return v && *v >= 0 && *v < 1 ? std::optional<double>(*v * scale) : std::nullopt;
        };
        e.brakePipe = read("brakepipe");
        e.brakeInitial = read("brakeinitial");
        e.brakeFull = read("brakefull");
        e.mainReservoir = read("mainreservoir");
    }
    e.throttlePower = curves(root.child("throttle-power"));
    e.dynamicBrake = curves(root.child("dynamic-brake"));
    return e;
}

}  // namespace trainz::content
