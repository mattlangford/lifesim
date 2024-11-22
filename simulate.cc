#include "args.hh"

#include <random>
#include <iostream>
#include <iomanip>
#include <set>

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

class ModelBase {
public:
    using Ptr = std::unique_ptr<ModelBase>;

    ModelBase(std::string name,
              ArgumentParser& parser) : name_(std::move(name)) {
        parser.add_argument(arg_name("start"), {
            .callback=[this](const auto& p){ start_ = std::get<double>(p); },
            .description="The start year (optional).",
            .value = 0.0
        });
        parser.add_argument(arg_name("duration"), {
            .callback=[this](const auto& p){ duration_ = std::get<double>(p); },
            .description="How long to run this model for (optional).",
            .value = std::numeric_limits<double>::infinity()
        });
    }
    virtual ~ModelBase() = default;

public:
    const std::string& name() const { return name_; }
    std::string arg_name(const std::string& arg) { return "--" + name() + "-" + arg; }

    double year() const { return year_; }

    const auto& start() const { return start_; }
    const auto end() const { return start() + duration_; }
    void set_start(double start) { start_ = start; }

    virtual double update_to(double year) {
        double dt = year - set_year(year);

        if (year < start_) {
            return 0.0;
        } else if (year >= end()) {
            return 0.0;
        } else if (dt <= 0.0) {
            return 0.0;
        }

        return update(dt);
    }

    virtual ModelBase::Ptr clone() const = 0;

protected:
    virtual double update(double dt) { return 0.0; }
    double set_year(double year) { double prev = year_; year_ = year; return prev; }

private:
    const std::string name_;

    double start_ = 0.0;
    double duration_ = 0.0;

    double year_ = 0.0;
};

class FundBase : public ModelBase {
public:
    using Ptr = std::unique_ptr<FundBase>;

    FundBase(std::string name, ArgumentParser& parser) : ModelBase(std::move(name), parser) {
        parser.add_argument(arg_name("amount"), {
            .callback=[this](const auto& p){ amount_ = std::get<double>(p); },
            .description="The starting amount in dollars."
        });
        parser.add_argument(arg_name("limit"), {
            .callback=[this](const auto& p){ contribution_limit_ = std::get<double>(p); },
            .description="Annual contribution limit.",
            .value=0.0
        });
    }
    ~FundBase() override = default;

    const double amount() const { return amount_; }

    double buy(double amount)  { 
        if (amount < 0.0) {
            return 0.0;
        }

        if (contribution_limit_ > 0.0) {
            double& contributed = contributed_[std::floor(year())];

            const double remaining = contribution_limit_ - contributed;
            amount = std::min(amount, remaining);
            contributed += amount;
        }

        amount_ += amount;
        return amount;
    }

    double sell(double amount) { 
        if (year() < start()) {
            return 0.0;
        }
        if (amount < 0.0) {
            return 0.0;
        }

        if (amount_ >= amount) {
            amount_ -= amount;
            return amount;
        }

        const double removed = amount_;
        amount_ = 0;
        return removed;
    }

    double update_to(double year) override {
        double dt = year - set_year(year);
        amount_ = update_amount(amount_, dt);
        return amount_;
    }

    virtual void set_offset_percent(double percent) {}

protected:
    virtual double update_amount(double amount, double dt) const = 0;

private:
    std::map<size_t, double> contributed_;

    double contribution_limit_ = 0.0;
    double amount_ = 0.0;
};

class FixedRateFund final : public FundBase {
public:
    FixedRateFund(std::string name, ArgumentParser& parser) : FundBase(std::move(name), parser) {
        parser.add_argument(arg_name("rate"), {
            .callback=[this](const auto& p){ rate_ = std::get<double>(p); },
            .description="The annual percent rate of return."
        });
    }
    ~FixedRateFund() override = default;

    ModelBase::Ptr clone() const override { return std::make_unique<FixedRateFund>(*this); }

protected:
    double update_amount(double amount, double dt) const override {
        return amount * std::exp(rate_ * dt);
    }

private:
    double rate_ = 0.0;
};

class MarketFund final : public FundBase {
public:
    MarketFund(std::string name, ArgumentParser& parser) : FundBase(std::move(name), parser) {
        if (file_ == nullptr) {
            file_ = std::make_shared<FileData>();

            // Memory map the market fund file
            file_->fd = open("market_data.bin", O_RDONLY);
            if (file_->fd == -1) {
                throw std::runtime_error("Failed to open market_data.bin");
            }

            struct stat file_stat;
            if (fstat(file_->fd, &file_stat) == -1) {
                throw std::runtime_error("Failed to get file stat");
            }

            void* map = mmap(0, file_stat.st_size, PROT_READ, MAP_SHARED, file_->fd, 0);
            if (map == MAP_FAILED) {
                throw std::runtime_error("Failed to map file");
            }

            file_->data = static_cast<float*>(map);
            file_->data_size = file_stat.st_size / sizeof(float);
        }
        wrap_around_multiplier_ = file_->data[file_->data_size - 1] / file_->data[0];
    }

    ~MarketFund() override = default;

    size_t data_size() const { return file_->data_size; }

    void set_offset_percent(double percent) override { day_offset_ = percent * data_size(); }
    ModelBase::Ptr clone() const override { return std::make_unique<MarketFund>(*this); }

protected:
    double update_amount(double amount, double dt) const override {
        return lookup(year() + dt) * amount / lookup(year());
    }

private:
    double lookup(double year) const {
        double day = year * 365.25 + day_offset_;
        size_t before = std::floor(day);

        // Easy case, within the orignal data
        if (before < file_->data_size) {
            return file_->data[before];
        }

        if (before >= 2 * file_->data_size) {
            throw std::runtime_error("Invalid after index.");
        }

        return wrap_around_multiplier_ * file_->data[before % file_->data_size];
    }

private:
    struct FileData {
        int fd;
        float* data;
        size_t data_size;

        ~FileData() {
            if (data) munmap(data, data_size * sizeof(double));
            close(fd);
        }
    };
    static std::shared_ptr<FileData> file_;

    double wrap_around_multiplier_ = 0.0;
    double day_offset_ = 0;
};

std::shared_ptr<MarketFund::FileData> MarketFund::file_;

class Job final : public ModelBase {
public:
    Job(std::string name, ArgumentParser& parser) : ModelBase(std::move(name), parser) {
        parser.add_argument(arg_name("salary"), {
            .callback=[this](const auto& p){ salary_ = std::get<double>(p); },
            .description="The starting amount in dollars."
        });
        parser.add_argument(arg_name("rate"), {
            .callback=[this](const auto& p){ rate_ = std::get<double>(p); },
            .description="The annual percent rate of return.",
            .value = 0.0,
        });
    }
    ~Job() override = default;

    ModelBase::Ptr clone() const override { return std::make_unique<Job>(*this); }
protected:
    double update(double dt) override {
        double previous = year() - dt;
        if (std::floor(previous) != std::floor(year())) {
            salary_ *= std::exp(rate_);
        }

        return dt * salary_;
    }

private:
    double salary_ = 0.0;
    double rate_ = 0.0;
};

class Spending final : public ModelBase {
public:
    Spending(std::string name, ArgumentParser& parser) : ModelBase(std::move(name), parser) {
        parser.add_argument(arg_name("annual"), {
            .callback=[this](const auto& p){ annual_ = std::get<double>(p); },
            .description="The annual spending rate."
        });
        parser.add_argument(arg_name("rate"), {
            .callback=[this](const auto& p){ rate_ = std::get<double>(p); },
            .description="The increase rate per year.",
            .value = 0.0
        });
        parser.add_argument(arg_name("is-exp"), {
            .callback=[this](const auto& p){ linear_ = !std::get<bool>(p); },
            .description="Is the model expoential (as opposed to linear).",
            .is_flag = true,
        });
    }
    ~Spending() override = default;

    ModelBase::Ptr clone() const override { return std::make_unique<Spending>(*this); }
protected:
    double update(double dt) override {
        if (linear_) {
            annual_ += dt * rate_;
        } else {
            annual_ *= std::exp(rate_ * dt);
        }

        return dt * annual_;
    }

private:
    double annual_ = 0.0;
    double rate_ = 0.0;

    bool linear_ = true;
};

class Cost final : public ModelBase {
public:
    Cost(std::string name, ArgumentParser& parser) : ModelBase(std::move(name), parser) {
        parser.add_argument(arg_name("total"), {
            .callback=[this](const auto& p){ total_ = remaining_ = std::get<double>(p); },
            .description="The annual spending rate."
        });
        parser.add_argument(arg_name("down"), {
            .callback=[this](const auto& p){ down_ = std::get<double>(p); },
            .description="The intial amount down, on the start of this cost.",
            .value = 0.0
        });
        parser.add_argument(arg_name("close"), {
            .callback=[this](const auto& p){ close_ = std::get<double>(p); },
            .description="Cost to close, on the end of this cost.",
            .value = 0.0
        });
    }
    ~Cost() override = default;

    double update_to(double year) override {
        const double dt = year - set_year(year);
        if (year < start()) {
            return 0.0;
        }
        if (year > end()) {
            double amount = remaining_ + close_;
            remaining_ = 0;
            close_ = 0;
            return amount;
        }

        if (down_ > 0.0) {
            total_ -= down_;
            remaining_ -= down_;
            double amount = down_;
            down_ = 0.0;
            return amount;
        }

        double amount = dt * total_ / (end() - start());
        amount = std::min(remaining_, amount);
        remaining_ -= amount;

        return amount;
    }

    ModelBase::Ptr clone() const override { return std::make_unique<Cost>(*this); }

private:
    double total_ = 0.0;
    double remaining_ = 0.0;
    double down_ = 0.0;
    double close_ = 0.0;
};

template <typename T>
std::set<std::unique_ptr<T>> clone_set(const std::set<std::unique_ptr<T>>& input) {
    std::set<std::unique_ptr<T>> output;
    for (const auto& in : input) {
        // Since clone() returns a ModelBase*, we need to up convert that back up a T* to return.
        if (T* ptr = dynamic_cast<T*>(in->clone().release())) {
            output.insert(std::unique_ptr<T>(ptr));
        } else {
            throw std::runtime_error("Unable to cast T::clone() return to T*");
        }
    }
    return output;
}
template <typename T>
std::vector<std::unique_ptr<T>> clone_vector(const std::vector<std::unique_ptr<T>>& input) {
    std::vector<std::unique_ptr<T>> output;
    output.reserve(input.size());
    for (const auto& in : input) {
        // Since clone() returns a ModelBase*, we need to up convert that back up a T* to return.
        if (T* ptr = dynamic_cast<T*>(in->clone().release())) {
            output.emplace_back(std::unique_ptr<T>(ptr));
        } else {
            throw std::runtime_error("Unable to cast T::clone() return to T*");
        }
    }
    return output;
}

int main(int argc, const char** argv) {
    std::cout << std::setprecision(2);

    ArgumentParser parser;

    double years = 1.0;
    parser.add_argument("--sim-years", {
        .callback=[&years](const auto& p){ years = std::get<double>(p); },
        .description = "how many simulated years to run.",
        .value = years
    });
    bool verbose = false;
    parser.add_argument("--verbose", {
        .callback=[&verbose](const auto& p){ verbose = std::get<bool>(p); },
        .description = "show detailed information",
        .is_flag=true
    });
    size_t sim_count = 1;
    parser.add_argument("--sim-count", {
        .callback=[&sim_count](const auto& p){ sim_count = std::get<double>(p); },
        .description = "how many random date-offset simulations to run",
        .value = static_cast<double>(sim_count)
    });
    size_t seed = 42;
    parser.add_argument("--sim-seed", {
        .callback=[&seed](const auto& p){ seed = std::get<double>(p); },
        .description = "random number generator seed",
        .value=static_cast<double>(seed)
    });
    double start = -1.0;
    parser.add_argument("--sim-year-start", {
        .callback=[&start](const auto& p){ start = std::get<double>(p); },
        .description = "acts as an override to the random start year (in percent duration)",
        .value=static_cast<double>(start)
    });

    std::set<ModelBase::Ptr> base_income_models;
    base_income_models.insert(std::make_unique<Job>("job", parser));

    std::set<ModelBase::Ptr> base_expense_models;
    base_expense_models.insert(std::make_unique<Spending>("spending", parser));
    base_expense_models.insert(std::make_unique<Cost>("child", parser));
    base_expense_models.insert(std::make_unique<Cost>("child2", parser));
    base_expense_models.insert(std::make_unique<Cost>("car", parser));

    // In the order that funds will be contributed to  (reverse withdrawl order)
    std::vector<FundBase::Ptr> base_market_models;
    base_market_models.push_back(std::make_unique<MarketFund>("market", parser));
    base_market_models.push_back(std::make_unique<MarketFund>("retirement", parser));

    parser.parse(argc, argv);

    std::mt19937 rng(seed);

    if (verbose) {
        std::cout << "id,year,";
        for (const auto& income : base_income_models) {
            std::cout << income->name() << "_income,";
        }
        for (const auto& expense : base_expense_models) {
            std::cout << expense->name() << "_expense,";
        }
        for (const auto& market : base_market_models) {
            std::cout << market->name() << "_contributed," << market->name() << "_spending," << market->name() << "_value,";
        }
        std::cout << "bankrupt\n";
    } else {
        std::cout << "start,final,status,retirement_value\n";
    }

    for (size_t id = 0; id < sim_count; ++id) {
        // Clone the models so we can mutate them.
        std::set<ModelBase::Ptr> income_models = clone_set(base_income_models);
        std::set<ModelBase::Ptr> expense_models = clone_set(base_expense_models);
        std::vector<FundBase::Ptr> market_models = clone_vector(base_market_models);

        // Set the offset percent for this simulation.
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        const double percent = start > 0.0 ? start : dist(rng);
        for (auto& market : market_models) {
            market->set_offset_percent(percent);
        }

        bool bankrupt = false;
        std::optional<double> retirement_value;

        constexpr double PERIOD = 1 / 52.0;
        for (size_t i = 1; i < years / PERIOD; ++i) {
            const double year = i * PERIOD;

            if (verbose) {
                std::cout << id << "," << std::setprecision(5) << year << "," << std::fixed;
            }

            // Compute total income, from all jobs.
            double total_income = 0.0;
            for (auto& income : income_models) {
                const double this_income = income->update_to(year);
                total_income += this_income;

                if (verbose) {
                    std::cout << this_income << ",";
                }
            }

            // If we're out of job money, consider this retirment. This should probably update to use the job duration.
            if (total_income == 0.0 && !retirement_value) {
                for (auto& market : market_models) {
                    retirement_value = retirement_value.value_or(0.0) + market->amount();
                }
            }

            // Total expenses that need to be offset.
            double total_expenses = 0.0;
            for (auto& expense : expense_models) {
                const double this_expense = expense->update_to(year);
                total_expenses += this_expense;

                if (verbose) {
                    std::cout << this_expense << ",";
                }
            }

            // How much we can invest into market account and need to spend from market accounts
            double to_invest = std::max(total_income - total_expenses, 0.0);
            double to_spend = std::max(total_expenses - total_income, 0.0);
            std::vector<double> market_contributed;
            if (verbose) { market_contributed.resize(market_models.size()); }
            for (size_t i = 0; i < market_models.size(); ++i) {
                size_t reverse_i = market_models.size() - 1 - i;
                market_models[reverse_i]->update_to(year);

                double contributed = market_models[reverse_i]->buy(to_invest);
                to_invest -= contributed;
                market_contributed[reverse_i] = contributed;
            }
            for (size_t i = 0; i < market_models.size(); ++i) {
                double spend = market_models[i]->sell(to_spend);
                to_spend -= spend;

                if (verbose) {
                    std::cout << market_contributed[i] << "," << spend << "," << market_models[i]->amount() << ",";
                }
            }

            // Bankrupt if we haven't covered the full set of expenses.
            if (to_spend > 0.0) {
                bankrupt = true;
            }

            if (verbose) {
                std::cout << bankrupt << "\n";
            }
        }
        
        if (!verbose) {
            double total_amount = 0.0;
            for (auto& market : market_models) {
                total_amount += market->amount();
            }

            std::cout << std::setprecision(5) << std::fixed << percent << "," << std::setprecision(2)
                << total_amount << ","
                << (bankrupt ? "bankrupt" : "okay") << ","
                << retirement_value.value_or(std::numeric_limits<double>::quiet_NaN()) << "\n";
        }
    }
}