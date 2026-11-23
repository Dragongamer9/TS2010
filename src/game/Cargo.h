// Goods: the queues of vehicles and industries (how many units of which product they hold) and an
// industry's processes, read from the assets' own configs (`queues`, `processes`). The moving of
// goods is the port's own logic; the game's industry scripts are not run.
#pragma once

#include "content/AssetConfig.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace trainz::game {

struct Product {
    std::string id;        // the product asset's KUID as text (the newest version's)
    std::string category;  // its product category's KUID as text, "" if none
    double unitKg = 0;     // `mass`: kg for one unit
};
// Looks a product up by the KUID a config names.
using ProductLookup = std::function<Product(const trainz::content::Kuid&)>;

struct CargoQueue {
    std::string name;
    double size = 0, count = 0;           // units
    Product product;                      // what it holds (id "" when its config names none)
    std::vector<std::string> products;    // others it takes (allowed-products), by id
    std::vector<std::string> categories;  // and whole categories (allowed-categories)
    double room() const { return size > count ? size - count : 0; }
    bool takes(const Product& p) const;
};
std::vector<CargoQueue> parseQueues(const trainz::content::ConfigNode& config, const ProductLookup& product);

struct Process {
    bool enabled = false;
    double duration = 0, clock = 0;                        // seconds
    std::vector<std::pair<std::string, double>> in, out;   // queue name, units each time
};
struct Industry {
    std::vector<CargoQueue> queues;
    std::vector<Process> processes;
    static Industry parse(const trainz::content::ConfigNode& config, const ProductLookup& product);
    CargoQueue* queue(const std::string& name);
    // Each enabled process: every `duration` seconds, if its inputs are all there and its outputs
    // have room, takes the inputs and adds the outputs.
    void step(double seconds);
    // Whether a process that can still run would add to this queue, or take from it.
    bool fills(const CargoQueue& q) const;
    bool empties(const CargoQueue& q) const;
};

// Moves as much as it can of `from`'s product into `to` (up to its room); returns the units moved.
double moveGoods(CargoQueue& from, CargoQueue& to);

// The industries orders name, by the name in the order.
using Industries = std::map<std::string, Industry>;

}  // namespace trainz::game
