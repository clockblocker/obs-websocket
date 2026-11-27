#include <cassert>
#include <string>

#include <nlohmann/json.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <rfl/nlohmann.hpp>

struct SmokePayload {
	std::string name;
	int version;
};

int main()
{
	const SmokePayload original{"obs-websocket", 5};
	const nlohmann::json j = rfl::nlohmann::write(original);
	const auto parsed = rfl::nlohmann::read<SmokePayload>(j);
	assert(parsed.has_value());
	assert(parsed->name == "obs-websocket");
	assert(parsed->version == 5);

	const std::string wire = rfl::json::write(original);
	const auto fromWire = rfl::json::read<SmokePayload>(wire);
	assert(fromWire.has_value());
	assert(fromWire->name == "obs-websocket");
	assert(fromWire->version == 5);
	return 0;
}
