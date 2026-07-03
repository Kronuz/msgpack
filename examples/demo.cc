#include "msgpack.h"
#include "msgpack_patcher.h"

#include <iostream>
#include <string>

#include "rapidjson/document.h"

int main() {
	MsgPack doc = {
		{ "name", "Neo" },
		{ "tags", { "chosen" } },
		{ "active", true },
	};

	doc["tags"].push_back("operator");
	std::string wire = doc.serialise();
	MsgPack copy = MsgPack::unserialise(wire);

	rapidjson::Document json;
	json.Parse(R"json({"from_json":"yes","answer":42})json");
	MsgPack from_json(json);
	rapidjson::Document back_to_json = from_json.as_document();

	MsgPack patch = MsgPack::ARRAY();
	patch.push_back(MsgPack({ { "op", "replace" }, { "path", "/name" }, { "value", "Trinity" } }));
	apply_patch(patch, copy);

	std::cout << "patched: " << copy.to_string() << '\n';
	std::cout << "json round-trip answer: " << back_to_json["answer"].GetInt() << '\n';
}
