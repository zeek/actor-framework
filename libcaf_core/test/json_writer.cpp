// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#define CAF_SUITE json_writer

#include "caf/json_writer.hpp"

#include "core-test.hpp"

using namespace caf;

using namespace std::literals::string_literals;

namespace {

struct fixture {
  template <class T>
  expected<std::string>
  to_json_string(T&& x, size_t indentation,
                 bool skip_empty_fields
                 = json_writer::skip_empty_fields_default,
                 bool skip_object_type_annotation
                 = json_writer::skip_object_type_annotation_default) {
    json_writer writer;
    writer.indentation(indentation);
    writer.skip_empty_fields(skip_empty_fields);
    writer.skip_object_type_annotation(skip_object_type_annotation);
    if (writer.apply(std::forward<T>(x))) {
      auto buf = writer.str();
      return {std::string{buf.begin(), buf.end()}};
    } else {
      MESSAGE("partial JSON output: " << writer.str());
      return {writer.get_error()};
    }
  }
};

} // namespace

BEGIN_FIXTURE_SCOPE(fixture)

SCENARIO("the JSON writer converts builtin types to strings") {
  GIVEN("an integer") {
    auto x = 42;
    WHEN("converting it to JSON with any indentation factor") {
      THEN("the JSON output is the number") {
        CHECK_EQ(to_json_string(x, 0), "42"s);
        CHECK_EQ(to_json_string(x, 2), "42"s);
      }
    }
  }
  GIVEN("a string") {
    std::string x = R"_(hello "world"!)_";
    WHEN("converting it to JSON with any indentation factor") {
      THEN("the JSON output is the escaped string") {
        std::string out = R"_("hello \"world\"!")_";
        CHECK_EQ(to_json_string(x, 0), out);
        CHECK_EQ(to_json_string(x, 2), out);
      }
    }
    WHEN("it contains non-printable ASCII characters") {
      THEN("the characters are escaped in the JSON output") {
        x = detail::format("{}{}{}{}{}", static_cast<char>(0),
                           static_cast<char>(1), static_cast<char>(30),
                           static_cast<char>(31), static_cast<char>(32));
        // NOTE: empty space at the end corresponds to ASCII 32.
        auto out = R"_("\u0000\u0001\u001e\u001f ")_"s;
        check_eq(to_json_string(x, 0), out);
      }
    }
  }
  GIVEN("a list") {
    auto x = std::vector<int>{1, 2, 3};
    WHEN("converting it to JSON with indentation factor 0") {
      THEN("the JSON output is a single line") {
        std::string out = "[1, 2, 3]";
        CHECK_EQ(to_json_string(x, 0), out);
      }
    }
    WHEN("converting it to JSON with indentation factor 2") {
      THEN("the JSON output uses multiple lines") {
        std::string out = R"_([
  1,
  2,
  3
])_";
        CHECK_EQ(to_json_string(x, 2), out);
      }
    }
  }
  GIVEN("a dictionary") {
    std::map<std::string, std::string> x;
    x.emplace("a", "A");
    x.emplace("b", "B");
    x.emplace("c", "C");
    WHEN("converting it to JSON with indentation factor 0") {
      THEN("the JSON output is a single line") {
        CHECK_EQ(to_json_string(x, 0), R"_({"a": "A", "b": "B", "c": "C"})_"s);
      }
    }
    WHEN("converting it to JSON with indentation factor 2") {
      THEN("the JSON output uses multiple lines") {
        std::string out = R"_({
  "a": "A",
  "b": "B",
  "c": "C"
})_";
        CHECK_EQ(to_json_string(x, 2), out);
      }
    }
  }
  GIVEN("a message") {
    auto x = make_message(put_atom_v, "foo", 42);
    WHEN("converting it to JSON with indentation factor 0") {
      THEN("the JSON output is a single line") {
        std::string out = R"_([{"@type": "caf::put_atom"}, "foo", 42])_";
        CHECK_EQ(to_json_string(x, 0), out);
      }
    }
    WHEN("converting it to JSON with indentation factor 2") {
      THEN("the JSON output uses multiple lines") {
        std::string out = R"_([
  {
    "@type": "caf::put_atom"
  },
  "foo",
  42
])_";
        CHECK_EQ(to_json_string(x, 2), out);
      }
    }
  }
}

SCENARIO("the JSON writer converts simple structs to strings") {
  GIVEN("a dummy_struct object") {
    dummy_struct x{10, "foo"};
    WHEN("converting it to JSON with indentation factor 0") {
      THEN("the JSON output is a single line") {
        std::string out = R"_({"@type": "dummy_struct", "a": 10, "b": "foo"})_";
        CHECK_EQ(to_json_string(x, 0), out);
      }
    }
    WHEN("converting it to JSON with indentation factor 0 and omitting @type") {
      THEN("the JSON output is a single line") {
        std::string out = R"_({"a": 10, "b": "foo"})_";
        CHECK_EQ(to_json_string(x, 0, false, true), out);
      }
    }
    WHEN("converting it to JSON with indentation factor 2") {
      THEN("the JSON output uses multiple lines") {
        std::string out = R"_({
  "@type": "dummy_struct",
  "a": 10,
  "b": "foo"
})_";
        CHECK_EQ(to_json_string(x, 2), out);
      }
    }
    WHEN("converting it to JSON with indentation factor 2 and omitting @type") {
      THEN("the JSON output uses multiple lines") {
        std::string out = R"_({
  "a": 10,
  "b": "foo"
})_";
        CHECK_EQ(to_json_string(x, 2, false, true), out);
      }
    }
  }
}

SCENARIO("the JSON writer converts nested structs to strings") {
  GIVEN("a rectangle object") {
    auto x = rectangle{{100, 200}, {10, 20}};
    WHEN("converting it to JSON with indentation factor 0") {
      THEN("the JSON output is a single line") {
        std::string out = R"({"@type": "rectangle", )"
                          R"("top-left": {"x": 100, "y": 200}, )"
                          R"("bottom-right": {"x": 10, "y": 20}})";
        CHECK_EQ(to_json_string(x, 0), out);
      }
    }
    WHEN("converting it to JSON with indentation factor 2") {
      THEN("the JSON output uses multiple lines") {
        std::string out = R"_({
  "@type": "rectangle",
  "top-left": {
    "x": 100,
    "y": 200
  },
  "bottom-right": {
    "x": 10,
    "y": 20
  }
})_";
        CHECK_EQ(to_json_string(x, 2), out);
      }
    }
  }
}

SCENARIO("the JSON writer converts structs with member dictionaries") {
  GIVEN("a phone_book object") {
    phone_book x;
    x.city = "Model City";
    x.entries["Bob"] = 555'6837;
    x.entries["Jon"] = 555'9347;
    WHEN("converting it to JSON with indentation factor 0") {
      THEN("the JSON output is a single line") {
        std::string out = R"({"@type": "phone_book",)"
                          R"( "city": "Model City",)"
                          R"( "entries": )"
                          R"({"Bob": 5556837,)"
                          R"( "Jon": 5559347}})";
        CHECK_EQ(to_json_string(x, 0), out);
      }
    }
    WHEN("converting it to JSON with indentation factor 2") {
      THEN("the JSON output uses multiple lines") {
        std::string out = R"({
  "@type": "phone_book",
  "city": "Model City",
  "entries": {
    "Bob": 5556837,
    "Jon": 5559347
  }
})";
        CHECK_EQ(to_json_string(x, 2), out);
      }
    }
  }
}

SCENARIO("the JSON writer omits or nulls missing values") {
  GIVEN("a dummy_user object without nickname") {
    dummy_user user;
    user.name = "Bjarne";
    WHEN("converting it to JSON with skip_empty_fields = true (default)") {
      THEN("the JSON output omits the field 'nickname'") {
        std::string out = R"({"@type": "dummy_user", "name": "Bjarne"})";
        CHECK_EQ(to_json_string(user, 0), out);
      }
    }
    WHEN("converting it to JSON with skip_empty_fields = false") {
      THEN("the JSON output includes the field 'nickname' with a null value") {
        std::string out
          = R"({"@type": "dummy_user", "name": "Bjarne", "nickname": null})";
        CHECK_EQ(to_json_string(user, 0, false), out);
      }
    }
  }
}

SCENARIO("the JSON writer annotates variant fields") {
  GIVEN("a widget object with rectangle shape") {
    widget x;
    x.color = "red";
    x.shape = rectangle{{10, 10}, {20, 20}};
    WHEN("converting it to JSON with indentation factor 0") {
      THEN("the JSON is a single line containing '@shape-type = rectangle'") {
        std::string out = R"({"@type": "widget", )"
                          R"("color": "red", )"
                          R"("@shape-type": "rectangle", )"
                          R"("shape": )"
                          R"({"top-left": {"x": 10, "y": 10}, )"
                          R"("bottom-right": {"x": 20, "y": 20}}})";
        CHECK_EQ(to_json_string(x, 0), out);
      }
    }
    WHEN("converting it to JSON with indentation factor 2") {
      THEN("the JSON is multiple lines containing '@shape-type = rectangle'") {
        std::string out = R"({
  "@type": "widget",
  "color": "red",
  "@shape-type": "rectangle",
  "shape": {
    "top-left": {
      "x": 10,
      "y": 10
    },
    "bottom-right": {
      "x": 20,
      "y": 20
    }
  }
})";
        CHECK_EQ(to_json_string(x, 2), out);
      }
    }
  }
  GIVEN("a widget object with circle shape") {
    widget x;
    x.color = "red";
    x.shape = circle{{15, 15}, 5};
    WHEN("converting it to JSON with indentation factor 0") {
      THEN("the JSON is a single line containing '@shape-type = circle'") {
        std::string out = R"({"@type": "widget", )"
                          R"("color": "red", )"
                          R"("@shape-type": "circle", )"
                          R"("shape": )"
                          R"({"center": {"x": 15, "y": 15}, )"
                          R"("radius": 5}})";
        CHECK_EQ(to_json_string(x, 0), out);
      }
    }
    WHEN("converting it to JSON with indentation factor 2") {
      THEN("the JSON is multiple lines containing '@shape-type = circle'") {
        std::string out = R"({
  "@type": "widget",
  "color": "red",
  "@shape-type": "circle",
  "shape": {
    "center": {
      "x": 15,
      "y": 15
    },
    "radius": 5
  }
})";
        CHECK_EQ(to_json_string(x, 2), out);
      }
    }
  }
}

SCENARIO("the JSON compresses empty lists and objects") {
  GIVEN("a map with an empty list value") {
    std::map<std::string, std::vector<int>> obj;
    obj["xs"] = std::vector<int>{};
    obj["ys"] = std::vector<int>{1, 2, 3};
    WHEN("converting it to JSON with indentation factor 2") {
      THEN("the JSON contains a compressed representation of the empty list") {
        std::string out = R"({
  "xs": [],
  "ys": [
    1,
    2,
    3
  ]
})";
        CHECK_EQ(to_json_string(obj, 2, true, true), out);
      }
    }
  }
  GIVEN("an empty map") {
    std::map<std::string, std::vector<int>> obj;
    WHEN("converting it to JSON with indentation factor 2") {
      THEN("the JSON contains a compressed representation of the empty map") {
        CHECK_EQ(to_json_string(obj, 2, true, true), "{}"s);
      }
    }
  }
}

END_FIXTURE_SCOPE()
