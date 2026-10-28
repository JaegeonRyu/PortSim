"""Read actual shipment CSVs; rates use simulation seconds, never playback FPS."""
import argparse
import csv
import json
import re
import statistics
from pathlib import Path


def measure(path, log=None):
    with Path(path).open(encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError("Shipment report is empty")
    number = lambda row, key: float(row[key])
    end = max(number(row, "FinalPlacementAtSeconds") for row in rows)
    handover = max(number(row, "STSHandoverAtSeconds") for row in rows)
    stats = {}
    for key in ("STSSeconds", "STSWaitingForAGVSeconds", "DispatchWaitSeconds",
                "ShipmentSeconds", "TrafficWaitSeconds", "YardWaitSeconds"):
        values = sorted(number(row, key) for row in rows)
        stats[key] = dict(mean=statistics.mean(values), median=statistics.median(values), maximum=max(values))
    productive = [number(row, "STSSeconds") - number(row, "STSWaitingForAGVSeconds") for row in rows]
    lanes = sorted({int(row["STSLane"]) for row in rows})
    result = dict(report=str(Path(path).resolve()), count=len(rows), last_placement_seconds=end,
                  last_handover_seconds=handover, stats=stats,
                  mean_sts_non_agv_wait_seconds=statistics.mean(productive),
                  fleet_yard_per_hour=len(rows)*3600/end,
                  mean_sts_yard_per_hour=len(rows)*3600/end/len(lanes),
                  mean_sts_handover_per_hour=len(rows)*3600/handover/len(lanes), lanes=[])
    for lane in lanes:
        selected = [row for row in rows if int(row["STSLane"]) == lane]
        result["lanes"].append(dict(sts=lane, count=len(selected),
                                  last_handover_seconds=max(number(row, "STSHandoverAtSeconds") for row in selected),
                                  yard_per_hour_common_window=len(selected)*3600/end))
    if log:
        text = Path(log).read_text(encoding="utf-8-sig", errors="replace")
        cycles = {}
        for kind, ident, cargo, elapsed, wait, stages in re.findall(
                r"CRANE_CYCLE: kind=(\w+) id=(\d+) cargo=(\S+) elapsed=([\d.]+) agv_wait=([\d.]+) stages=([\d.,]+)", text):
            # Reset checks may repeat a cargo; use its last completed measurement.
            cycles[kind, ident, cargo] = [float(value) for value in stages.split(",")]
        result["cycle_stage_mean_seconds"] = {}
        for kind in ("STS", "RMG"):
            matching = [value for key, value in cycles.items() if key[0] == kind]
            if matching:
                result["cycle_stage_mean_seconds"][kind] = [statistics.mean(values) for values in zip(*matching)]
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("report")
    parser.add_argument("--log")
    parser.add_argument("--baseline")
    parser.add_argument("--output")
    args = parser.parse_args()
    result = measure(args.report, args.log)
    if args.baseline:
        baseline = measure(args.baseline)
        with open(args.report, encoding="utf-8-sig") as handle:
            actual_ids = {row["ContainerID"] for row in csv.DictReader(handle)}
        with open(args.baseline, encoding="utf-8-sig") as handle:
            baseline_ids = {row["ContainerID"] for row in csv.DictReader(handle)}
        if actual_ids != baseline_ids:
            raise ValueError("Comparison requires identical cargo IDs")
        result["baseline"] = baseline
        result["elapsed_reduction_percent"] = 100*(1-result["last_placement_seconds"]/baseline["last_placement_seconds"])
    encoded = json.dumps(result, indent=2, ensure_ascii=False)
    if args.output:
        Path(args.output).write_text(encoded+"\n", encoding="utf-8")
    print(encoded)
