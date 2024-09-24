#pragma once

#include <iostream>
#include <functional>
#include <map>
#include <variant>

class ArgumentParser {
public:
    struct NoArg{};
    using Parsed = std::variant<
        bool,
        double
    >;

    using Callback = std::function<void(const Parsed& parsed)>;

    struct Argument {
        Callback callback = [](const Parsed& p){};

        std::string description = "";

        bool is_flag = false;

        // Populated with a default value
       std::optional<Parsed> value;
    };

public:
    ArgumentParser() {
        add_argument("--help", Argument{
            .callback = [this](const auto& b) { if (std::get<bool>(b)) help(); },
            .description="Shows this message.",
            .is_flag=true});
    }

    void help(std::string error="") {
        if (!error.empty()) {
            std::cout << error << "\n";
        }

        std::cout << "Arguments:\n";
        for (const auto& [name, arg] : args_) {
            std::cout << "\t" << name << "";
            if (arg.value) {
                if (auto s = format_arg(*arg.value); !s.empty()) { std::cout << " [" << s << "]"; }
            }
            if (!arg.description.empty()) { std::cout << ": " << arg.description; }
            std::cout << "\n";
        }

        exit(1);
    }

    void parse(int argc, const char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);

            auto arg_it = args_.find(arg);
            if (arg_it == args_.end()) {
                help("No argument named '" + arg + "' was registered.");
            }

            if (arg_it->second.is_flag) {
                arg_it->second.value = true;
            } else if (i + 1 < argc){
                if(auto parsed = parse_arg(argv[i + 1])) {
                    arg_it->second.value = parsed.value();
                }
                i++;
            }
        }

        for (auto& [name, arg] : args_) {
            try {
                if (arg.is_flag) {
                    arg.callback(arg.value.has_value());
                } else if (arg.value) {
                    arg.callback(*arg.value);
                } else {
                    help("Argument '" + name + "' is missing argument");
                }
            } catch(const std::runtime_error& ex) {
                help("Exception when handling '" + name + "': " + ex.what());
            }
        }
    }

    void add_argument(std::string name, Argument arg) {
        if (name.empty() || !name.at(0)) {
            throw std::runtime_error("Invalid argument name '" + name + "' must start with '-'");
        }
        args_[std::move(name)] = std::move(arg);
    }

private:
    std::optional<Parsed> parse_arg(const std::string& str) const {
        try {
            return std::stod(str);
        } catch (const std::invalid_argument& ex) {
            // assume its something else
        }

        return std::nullopt;
    }

    std::string format_arg(const Parsed& parsed) const {
        struct Visitor {
            std::string operator()(const double& v) const { return std::to_string(v); }
            std::string operator()(const bool& v) const { return std::to_string(v); }
        };
        return std::visit(Visitor{}, parsed);
    }

private:
    std::map<std::string, Argument> args_;
};
