#include "io/sys/Event.hpp"
namespace io {
auto Event::All(size_t key) -> Event { return Event {key, true, true}; }
auto Event::Readable(size_t key) -> Event { return Event {key, true, false}; }
auto Event::Writable(size_t key) -> Event { return Event {key, false, true}; }
auto Event::None(size_t key) -> Event { return Event {key, false, false}; }


} // namespace io