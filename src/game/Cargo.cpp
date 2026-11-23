#include "game/Cargo.h"

#include <algorithm>
#include <cmath>

namespace trainz::game {
namespace {
double number(const trainz::content::ConfigNode* n) {
    if (n == nullptr) {
        return 0;
    }
    const double v = !n->ints.empty() ? n->ints[0] : !n->floats.empty() ? static_cast<double>(n->floats[0]) : 0.0;
    return std::isfinite(v) ? v : 0.0;
}
std::vector<std::pair<std::string, double>> flows(const trainz::content::ConfigNode* list) {
    std::vector<std::pair<std::string, double>> out;
    if (list != nullptr) {
        for (const auto& f : list->children) {
            const auto* q = f.child("queue");
            const double amount = number(f.child("amount"));
            if (q != nullptr && amount > 0) {
                out.emplace_back(q->text, amount);
            }
        }
    }
    return out;
}
const CargoQueue* find(const std::vector<CargoQueue>& queues, const std::string& name) {
    const auto it = std::find_if(queues.begin(), queues.end(), [&](const CargoQueue& q) { return q.name == name; });
    return it == queues.end() ? nullptr : &*it;
}
// Whether a process could run now or after waiting: enabled, timed, inputs there or still coming.
bool canRun(const Process& p, const std::vector<CargoQueue>& queues) {
    return p.enabled && p.duration > 0 && std::all_of(p.in.begin(), p.in.end(), [&](const auto& f) {
               const auto* q = find(queues, f.first);
               return q != nullptr && q->count >= f.second;
           });
}
}  // namespace

bool CargoQueue::takes(const Product& p) const {
    if (p.id.empty()) {
        return false;
    }
    return p.id == product.id || std::find(products.begin(), products.end(), p.id) != products.end() ||
           (!p.category.empty() && std::find(categories.begin(), categories.end(), p.category) != categories.end());
}

std::vector<CargoQueue> parseQueues(const trainz::content::ConfigNode& config, const ProductLookup& product) {
    std::vector<CargoQueue> out;
    const auto* list = config.child("queues");
    if (list == nullptr) {
        return out;
    }
    for (const auto& n : list->children) {
        CargoQueue q;
        q.name = n.key;
        q.size = std::max(0.0, number(n.child("size")));
        q.count = std::clamp(number(n.child("initial-count")), 0.0, q.size);
        if (const auto* p = n.child("product-kuid")) {
            if (const auto k = p->asKuid()) {
                q.product = product(*k);
            }
        }
        if (const auto* l = n.child("allowed-products")) {
            for (const auto& p : l->children) {
                if (const auto k = p.asKuid()) {
                    q.products.push_back(product(*k).id);
                }
            }
        }
        if (const auto* l = n.child("allowed-categories")) {
            for (const auto& c : l->children) {
                if (const auto k = c.asKuid()) {
                    q.categories.push_back(trainz::content::formatKuid(*k));
                }
            }
        }
        out.push_back(std::move(q));
    }
    return out;
}

Industry Industry::parse(const trainz::content::ConfigNode& config, const ProductLookup& product) {
    Industry out;
    out.queues = parseQueues(config, product);
    if (const auto* list = config.child("processes")) {
        for (const auto& n : list->children) {
            Process p;
            p.enabled = number(n.child("start-enabled")) != 0;
            p.duration = number(n.child("duration"));
            p.in = flows(n.child("inputs"));
            p.out = flows(n.child("outputs"));
            out.processes.push_back(std::move(p));
        }
    }
    return out;
}

CargoQueue* Industry::queue(const std::string& name) {
    const auto it = std::find_if(queues.begin(), queues.end(), [&](const CargoQueue& q) { return q.name == name; });
    return it == queues.end() ? nullptr : &*it;
}

void Industry::step(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) {
        return;
    }
    for (auto& p : processes) {
        if (!p.enabled || !(p.duration > 0) || !std::isfinite(p.duration)) {
            continue;
        }
        p.clock += seconds;
        for (int runs = 0; p.clock >= p.duration && runs < 100; ++runs) {
            p.clock -= p.duration;
            const bool ready = canRun(p, queues) && std::all_of(p.out.begin(), p.out.end(), [&](const auto& f) {
                                   const auto* q = find(queues, f.first);
                                   return q != nullptr && q->room() >= f.second;
                               });
            if (ready) {
                for (const auto& [name, amount] : p.in) {
                    queue(name)->count -= amount;
                }
                for (const auto& [name, amount] : p.out) {
                    queue(name)->count += amount;
                }
            }
        }
        p.clock = std::min(p.clock, p.duration);
    }
}

bool Industry::fills(const CargoQueue& q) const {
    return std::any_of(processes.begin(), processes.end(), [&](const Process& p) {
        return canRun(p, queues) && std::any_of(p.out.begin(), p.out.end(), [&](const auto& f) { return f.first == q.name; });
    });
}

bool Industry::empties(const CargoQueue& q) const {
    return std::any_of(processes.begin(), processes.end(), [&](const Process& p) {
        return canRun(p, queues) && std::any_of(p.in.begin(), p.in.end(), [&](const auto& f) { return f.first == q.name; });
    });
}

double moveGoods(CargoQueue& from, CargoQueue& to) {
    if (!to.takes(from.product) || (to.count > 0 && !to.product.id.empty() && to.product.id != from.product.id)) {
        return 0;  // not its goods, or it holds others
    }
    const double units = std::min(from.count, to.room());
    if (!(units > 0)) {
        return 0;
    }
    from.count -= units;
    to.count += units;
    to.product = from.product;
    return units;
}

}  // namespace trainz::game
