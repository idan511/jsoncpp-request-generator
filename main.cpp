#include <iostream>
#include <json/json.h>
#include <unordered_map>
#include <utility>

int global_y = 123;

class json_request {
    std::string description;

public:
    explicit json_request(std::string desc) : description(std::move(desc)) {}

    virtual Json::Value execute(const Json::Value &params) = 0;

    Json::Value operator()(const Json::Value &params) {
        return execute(params);
    }

    [[nodiscard]] Json::Value help() const {
        Json::Value help_json;
        help_json["description"] = description;
        Json::Value params_json(Json::objectValue);
        generate_params_description(params_json);
        help_json["params"] = params_json;
        return help_json;
    }

    virtual void generate_params_description(Json::Value &params_json) const = 0;
};

template<typename T>
const char *get_param_example() {
    static_assert(sizeof(T) == 0, "get_param_example not implemented for this type");
}

template<>
const char *get_param_example<int>() {
    return "integer";
}

template<>
const char *get_param_example<int &>() {
    return "integer reference";
}


template<typename T>
T parse_json_param(const Json::Value &param) {
    static_assert(sizeof(T) == 0, "parse_json_param not implemented for this type");
}

template<>
int parse_json_param<int>(const Json::Value &param) {
    if (!param.isInt()) {
        throw std::invalid_argument("Expected integer parameter");
    }
    return param.asInt();
}

template<>
int &parse_json_param<int &>(const Json::Value &param) {
    return global_y; // Example of a reference parameter, could be any global or static variable
}

template<typename... Args, std::size_t... Is>
auto parse_params_to_tuple_impl(
        const Json::Value &params,
        std::index_sequence<Is...>
) {
    return std::make_tuple(
            parse_json_param<
                    std::tuple_element_t<Is, std::tuple<Args...>>
            >(params[(Json::ArrayIndex) Is])...
    );
}

template<typename... Args>
auto parse_params_to_tuple(const Json::Value &params) {
    constexpr std::size_t N = sizeof...(Args);
    if (!params.isArray()) {
        throw std::runtime_error("Expected JSON array for function parameters");
    }
    if (params.size() < static_cast<Json::ArrayIndex>(N)) {
        throw std::runtime_error("Not enough elements in JSON array to match function signature");
    }
    return parse_params_to_tuple_impl<Args...>(params, std::make_index_sequence<N>{});
}

template<typename... Args>
Json::Value invoke_func_by_json(
        const Json::Value &params,
        const std::function<Json::Value(Args...)> &func) {
    auto args_tuple = parse_params_to_tuple<Args...>(params);
    return std::apply(func, args_tuple);
}

struct param_description {
    std::string name;
    std::string description;
};

template<typename ...Args>
class json_request_impl : public json_request {
    std::function<Json::Value(Args...)> func;
    std::array<param_description, sizeof...(Args)> params;
public:
    json_request_impl(std::string desc, std::function<Json::Value(Args...)> f)
            : json_request(std::move(desc)), func(std::move(f)), params() {}

    json_request_impl(std::string desc, std::function<Json::Value(Args...)> f,
                      std::array<param_description, sizeof...(Args)> param_desc) : json_request(std::move(desc)),
                                                                                   func(std::move(f)),
                                                                                   params(
                                                                                           std::move(param_desc)) {}


    json_request_impl(std::string desc, std::function<Json::Value(Args...)> f,
                      std::initializer_list<param_description> param_desc_list)
            : json_request(std::move(desc)), func(std::move(f)) {
        if (param_desc_list.size() != sizeof...(Args)) {
            throw std::invalid_argument("Invalid number of parameter descriptions");
        }
        std::copy(param_desc_list.begin(), param_desc_list.end(), params.begin());
    }

    Json::Value execute(const Json::Value &args) override {
        if (args.size() != sizeof...(Args)) {
            throw std::invalid_argument("Invalid number of parameters");
        }
        if (args.isArray()) {
            return invoke_func_by_json<Args...>(args, func);
        } else if (args.isObject()) {
            Json::Value array_params(Json::arrayValue);
            for (const auto &param: params) {
                if (!args.isMember(param.name)) {
                    throw std::invalid_argument("Missing parameter: " + param.name);
                }
                array_params.append(args[param.name]);
            }
            return invoke_func_by_json<Args...>(array_params, func);
        } else {
            throw std::invalid_argument("Parameters must be an array or an object");
        }
    }

    Json::Value execute_internal(Args &&... args) {
        return func(std::forward<Args>(args)...);
    }

    void generate_params_description(Json::Value &params_json) const override {
        params_json = Json::Value(Json::objectValue);
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((params_json[params[Is].name] = Json::Value(Json::objectValue),
              params_json[params[Is].name]["type_example"] = get_param_example<std::tuple_element_t<Is, std::tuple<Args...>>>(),
              params_json[params[Is].name]["description"] = params[Is].description,
              params_json[params[Is].name]["index"] = static_cast<Json::ArrayIndex>(Is)), ...);
        }(std::make_index_sequence<sizeof...(Args)>{});
    }

};

#define GET_ARGS(type, name, desc) type name
#define GET_TYPE(type, name, desc) type
#define GET_DESCRIPTION(type, name, desc) { #name, desc }

#define JSON_REQUEST(name, desc, func) \
    Json::Value name##_base_func(test_request##_args(GET_ARGS)) func \
    json_request_impl<test_request##_args(GET_TYPE)> name(desc, name##_base_func, { \
        test_request##_args(GET_DESCRIPTION) \
    });


/**** Example usage ****/

#define test_request_args(F) \
    F(int, x, "First integer parameter"), \
    F(int&, y, "Second integer parameter")
JSON_REQUEST(test_request, "Test request", {
    int sum = x / y;
    sum *= 2;
    Json::Value result(Json::objectValue);
    result["result"] = sum;
    return result;
})

/**** Main function ****/

int main() {
    Json::Value params = Json::objectValue;
    params["x"] = 200;
    params["y"] = Json::nullValue;
    std::cout << "Executing request: " << test_request.help() << std::endl;
    std::cout << "Executing request: " << test_request(params) << std::endl;
    return 0;
}
