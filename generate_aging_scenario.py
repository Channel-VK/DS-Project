
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATA = ROOT / "Data"
OUT = DATA / "scenarios" / "aging"
OUT.mkdir(parents=True, exist_ok=True)


def load_json(name):
    with (DATA / name).open("r", encoding="utf-8") as f:
        return json.load(f)


def save_json(name, data):
    with (OUT / name).open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


nodes = load_json("campus_nodes.json")
edges = load_json("campus_edges.json")

# All these IDs exist in the original campus graph.
fleet = {
    "buggies": [
        {
            "id": "AGE1",
            "capacity": 6,
            "current_location": 1,
            "status": "AVAILABLE",
            "speed_mps": 5.5
        }
    ]
}

requests = {
    "requests": [
        {
            "id": "BLOCKER",
            "pickup": 1,
            "destination": 2,
            "passengers": 1,
            "urgency": 10,
            "request_time": 0
        },
        {
            "id": "AGED",
            "pickup": 2,
            "destination": 9,
            "passengers": 1,
            "urgency": 1,
            "request_time": 0
        },
        {
            "id": "NEW",
            "pickup": 2,
            "destination": 9,
            "passengers": 1,
            "urgency": 5,
            "request_time": 20
        }
    ]
}

config = {
    "algorithm": "greedy",
    "routing": "dijkstra",
    "output_format": "json",
    "urgency_weight": 1.0,
    "aging_weight": 0.5,
    "time_step_s": 10,
    "max_simulation_time_s": 1800
}

save_json("campus_buggies.json", fleet)
save_json("requests.json", requests)
save_json("campus_nodes.json", nodes)
save_json("campus_edges.json", edges)
save_json("simulation_config.json", config)

print("Updated aging scenario generated.")
print("Expected order: BLOCKER first, then AGED before NEW")
print(f"Saved to: {OUT}")
