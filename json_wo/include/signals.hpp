#ifndef SIGNALS_HPP
#define SIGNALS_HPP

#include <boost/signals2.hpp>

namespace signals {

template <typename... Args>
struct Signals {
    //callable object as input to connect
    using Slot = std::function<void(Args...)>;
    using Key = std::size_t;
    std::size_t connect(std::function<void(Args...)> slot) {
        Key key = ++next_;
        slots_.emplace(key, std::move(slot));
        return key;
    }
    void emit(Args... args) {
        //iterate through the map and call each slot
        for( auto& [_, f]: slots_ ) {
            f(args...);
        }
    }
    void disconnect(Key key) {
        slots_.erase(key);
    }
    void clear() {
        slots_.clear();
    }
    Key next_{0};
    std::unordered_map<Key, Slot> slots_;
};

} // namespace signals

#endif // SIGNALS_HPP
