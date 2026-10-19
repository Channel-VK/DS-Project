
import json
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATA = ROOT / "Data"
EXPANDED = DATA / "scenarios" / "expanded"

EXPANDED.mkdir(parents=True, exist_ok=True)


def read_json(filename):
    with (DATA / filename).open("r", encoding="utf-8") as f:
        return json.load(f)


def write_json(path, data):
    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


# Load the original data.
buggies_data = read_json("campus_buggies.json")
requests_data = read_json("requests.json")
nodes_data = read_json("campus_nodes.json")
edges_data = read_json("campus_edges.json")
config_data = read_json("simulation_config.json")

buggies = buggies_data["buggies"]
requests = requests_data["requests"]
node_ids = [node["id"] for node in nodes_data["nodes"]]

# Add three new online buggies.
extra_buggies = [
    {
        "id": "BG6",
        "capacity": 6,
        "current_location": node_ids[0],
        "status": "AVAILABLE",
        "speed_mps": 5.5
    },
    {
        "id": "BG7",
        "capacity": 4,
        "current_location": node_ids[len(node_ids) // 2],
        "status": "AVAILABLE",
        "speed_mps": 5.0
    },
    {
        "id": "BG8",
        "capacity": 6,
        "current_location": node_ids[-1],
        "status": "AVAILABLE",
        "speed_mps": 5.5
    }
]

existing_buggy_ids = {b["id"] for b in buggies}
for buggy in extra_buggies:
    if buggy["id"] not in existing_buggy_ids:
        buggies.append(buggy)

# Add 100 reproducible requests.
rng = random.Random(20261019)
existing_ids = {r["id"] for r in requests}
target_count = len(requests) + 100
number = 1

while len(existing_ids) < target_count:
    request_id = f"X{number:03d}"
    number += 1

    if request_id in existing_ids:
        continue

    pickup = rng.choice(node_ids)
    destination = rng.choice(node_ids)

    while destination == pickup:
        destination = rng.choice(node_ids)

    requests.append({
        "id": request_id,
        "pickup": pickup,
        "destination": destination,
        "passengers": rng.randint(1, 4),
        "urgency": rng.randint(1, 5),
        "request_time": rng.randint(0, 1700)
    })
    existing_ids.add(request_id)

# Save all files in the original JSON wrapper format.
write_json(EXPANDED / "campus_buggies.json", {"buggies": buggies})
write_json(EXPANDED / "requests.json", {"requests": requests})
write_json(EXPANDED / "campus_nodes.json", nodes_data)
write_json(EXPANDED / "campus_edges.json", edges_data)
write_json(EXPANDED / "simulation_config.json", config_data)

print("Expanded scenario generated.")
print(f"Buggies: {len(buggies)}")
print(f"Requests: {len(requests)}")
print(f"Output: {EXPANDED}")
