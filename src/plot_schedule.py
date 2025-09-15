import pandas as pd
import matplotlib.pyplot as plt

# Load log
df = pd.read_csv("../schedule_log.csv")   # safer path with /
df = df.sort_values("t_us")

# Build intervals
intervals = []
prev_tid, prev_time = None, None

for _, row in df.iterrows():
    t = row["t_us"]
    e = row["event"]
    tid = row["tid"]

    if e == "run":
        if prev_tid is not None:
            intervals.append((prev_time, t, prev_tid, "switch"))
        prev_tid, prev_time = tid, t
    elif e in ("yield", "finish", "qexpire", "halt"):
        if prev_tid is not None:
            intervals.append((prev_time, t, prev_tid, e))
            prev_tid = None

# Intervals DataFrame
df_int = pd.DataFrame(intervals, columns=["start", "end", "tid", "stop_reason"])
df_int["start"] -= df_int["start"].min()
df_int["end"]   -= df_int["end"].min()

# Map threads to rows
thread_ids = sorted(df_int["tid"].unique())
thread_map = {tid: i for i, tid in enumerate(thread_ids)}

# Colors for threads
colors = plt.cm.tab10.colors
# Marker styles for stop reasons
markers = {
    "yield": "o",       # circle
    "finish": "s",      # square
    "qexpire": "X",     # X mark
    "halt": "P",        # plus-filled
    "switch": None      # internal switch, no marker
}

# Plot
fig, ax = plt.subplots(figsize=(10, 5))

for _, row in df_int.iterrows():
    # Draw run interval
    ax.barh(thread_map[row["tid"]],
            row["end"] - row["start"],
            left=row["start"],
            height=0.4,
            color=colors[row["tid"] % len(colors)],
            edgecolor="black")

    # Draw marker for stop reason
    if row["stop_reason"] in markers and markers[row["stop_reason"]] is not None:
        ax.scatter(row["end"],
                   thread_map[row["tid"]],
                   marker=markers[row["stop_reason"]],
                   color="black",
                   s=60,
                   zorder=3)

ax.set_yticks(list(thread_map.values()))
ax.set_yticklabels([f"Thread {tid}" for tid in thread_map.keys()])
ax.set_xlabel("Time (µs from start)")
ax.set_title("Thread Scheduling Timeline (with stop reasons)")

plt.tight_layout()
plt.show()
