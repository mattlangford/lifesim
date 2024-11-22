import argparse
import subprocess
import io
import random
import time
import pandas as pd

parser = argparse.ArgumentParser(description="Executes ")
parser.add_argument('--current-market-value', '-m', type=float, required=True, help="Current market investment value")
parser.add_argument('--current-retirement-value', '-r', type=float, required=True, help="Current retirment value")
parser.add_argument('--current-salary', '-s', type=float, required=True, help="Current retirment value")

parser.add_argument('--sim-count', type=int, default=100, help="How many simulations to run with each set of parameters")
parser.add_argument('--experiment-count', type=int, default=1000, help="How many sets of parameters to try")
args = parser.parse_args()

INSURANCE = 12 * 500
CAR_VALUE = 90000

def run_models(work_years, spend_rate, child_offset, interchild_offset, car_offset, spend_base, seed=42):
    spending_amount = 12 * spend_base + INSURANCE
    salary = args.current_salary - INSURANCE

    command = "./build/simulate "
    command += f"--market-amount {args.current_market_value:.2f} "
    command += f"--retirement-amount {args.current_retirement_value:.2f} "
    # command += f"--retirement-start {59 - 29} "
    command += f"--retirement-limit {24000} "
    command += f"--child-start {child_offset:.2f} "
    command += f"--child-total {485000 + 4 * 11260} " # Base cost, plus college
    command += f"--child-duration {18} "
    command += f"--child2-start {child_offset + interchild_offset:.2f} "
    command += f"--child2-total {485000 + 4 * 11260} " # Base cost, plus college
    command += f"--child2-duration {18} "
    command += f"--car-down {CAR_VALUE * 0.1:.2f} " # 10% down
    command += f"--car-total {CAR_VALUE * 0.9:.2f} "
    command += f"--car-start {car_offset} "
    command += f"--car-duration {3} "
    command += f"--job-salary {salary:.2f} "
    command += f"--job-duration {work_years:.2f} "
    command += f"--job-rate {0.05} "
    command += f"--spending-annual {spending_amount:.2f} "
    command += f"--spending-rate {spend_rate:.2f} "
    command += f"--sim-years {50} "
    command += f"--sim-seed {seed} "

    result = subprocess.run(command + f"--sim-count {args.sim_count}", stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=True)
    output = result.stdout + result.stderr
    assert result.returncode == 0, output

    exp = pd.read_csv(io.StringIO(output))
    exp["work_years"] = work_years
    exp["spend_rate"] = spend_rate
    exp["child_offset"] = child_offset
    exp["interchild_offsete"] = interchild_offset
    exp["car_offset"] = car_offset
    exp["spend_base"] = spend_base

    if "start" in exp:
        commands = []
        for start in exp["start"]:
            commands.append(command + f"--sim-year-start {start}")
        exp["command"] = commands
    else:
        exp["command"] = command    
    return exp

# Build
result = subprocess.run("clang++ -std=c++20 simulate.cc -O3 -o build/simulate",
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=True)
assert result.returncode == 0, result.stdout + result.stderr

start = time.time()

exps = []
datapoints = args.experiment_count
for e in range(datapoints):
    work_years = random.uniform(0, 12)
    spend_rate = random.uniform(10, 200)
    spend_base = random.uniform(3000, 6000)
    child_offset = random.uniform(1, 10)
    interchild_offset = random.uniform(1, 5)
    car_offset = random.uniform(5, 10)
    seed = random.uniform(0.0, 1.0)

    exps.append(run_models(work_years, spend_rate, child_offset, interchild_offset, car_offset, spend_base, seed))

    if e % 100 == 0:
        idx = args.sim_count * e
        dt = time.time() - start
        print(f"At experiement {idx}/{args.sim_count * datapoints} running at {idx / dt:.2f}/s")

exps = pd.concat(exps).reindex()
print(f"Generated all {len(exps)} experiements at {len(exps) / (time.time() - start):.2f}/s")
exps.to_csv("exps.csv", index=False)
exps