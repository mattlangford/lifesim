import argparse
import subprocess
import io
import random
import time
import pandas as pd

import matplotlib.pyplot as plt
import plotly.graph_objects as go
from plotly.subplots import make_subplots

def build():
    result = subprocess.run("clang++ -std=c++20 simulate.cc -o build/simulate",
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=True)
    assert result.returncode == 0, result.stdout + result.stderr
build()

parser = argparse.ArgumentParser(description="Executes ")
parser.add_argument("index", type=int, help="The index to load from the exps.csv dataset")
args = parser.parse_args()

build()

exps = pd.read_csv("exps.csv")

print(exps.iloc[args.index])
print()
print(f"Approximate start year {1971 + exps.iloc[args.index].start / 365.25:.0f}")
print()
print(exps.iloc[args.index].command)
result = subprocess.run(exps["command"].iloc[args.index] + " --verbose", stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=True)
output = result.stdout + result.stderr
assert result.returncode == 0, output

exp = pd.read_csv(io.StringIO(output))

fig = make_subplots(rows=2, cols=1, shared_xaxes=True)
row = 1

def plot(field, **kwargs):
    fig.add_trace(go.Scatter(x=exp["year"], y=exp[field], 
                             mode='lines', name=field,
                             line=kwargs),
                  row=row, col=1)


plot("market_value", color="blue")
plot("retirement_value", color="orange")


row = 2
plot("job_income")
plot("spending_expense")
plot("car_expense")
plot("child_expense")
plot("market_spending")
plot("market_contributed")
plot("retirement_spending")
plot("retirement_contributed")

# Set the layout of the figure
fig.update_layout(
    height=600,  # Adjust figure size
    width=1000,
    yaxis=dict(range=[0, 9E6]),  # Set y-axis range
)

fig.update_xaxes(title_text="Year", row=2, col=1)
fig.update_yaxes(title_text="Value", row=1, col=1)
fig.update_yaxes(title_text="Value", row=2, col=1)

fig.show()