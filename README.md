# CampusBuggyDispatch: Campus Electric-Buggy Routing & Dispatch

## Project by: Debuggers

**Course / Project:** Data Structures CSLR31 Project  
**Language:** C

---

## Table of Contents

1. Project overview & motivation
2. Key features (with technical detail)
3. System architecture & component map
4. Data formats & JSON schemas
5. Command-line interface & example runs
6. Build, run & configuration (CMake details)
7. Module & code walkthrough
8. Project files and documentation guide
9. Testing, validation & sample scenarios
10. Contribution guidelines & development notes
11. Limitations, future scope, acknowledgements & references
12. Contact & support

---

## 1. Project overview & motivation

**CampusBuggyDispatch** is a modular, C-based simulation of an electric-buggy dispatch system for a campus environment. It models campus locations and connecting roads as a weighted graph, maintains a fleet of buggies, processes passenger requests and evaluates how different dispatch strategies allocate vehicles.

A campus contains destinations separated by considerable distances, while the number of shared buggies is limited. When several requests arrive, manual allocation may result in long waiting times or inefficient vehicle movement. The project explores how graph algorithms and scheduling strategies can make this process more systematic.

The design focuses on:

- A graph-based representation of campus locations and roads.
- Dijkstra's shortest-path algorithm for route calculation.
- Fleet availability and passenger-capacity checks.
- Request prioritisation using urgency and waiting-time ageing.
- Multiple dispatch policies that can be compared using the same input scenario.
- Simulation output in JSON or CSV format.

**Project scope:** This repository implements a command-line simulation using static JSON input data and estimated road travel times. It is not a live GPS tracking service or a deployed passenger booking application.

---

## 2. Key features (technical detail)

- **Campus Graph and Route Calculation**
  - Campus locations are represented as graph vertices.
  - Roads are represented as weighted edges with distance and average travel time.
  - Edges can be marked bidirectional or one-way.
  - Dijkstra's algorithm finds the minimum-distance route between two node IDs and returns the route's distance and associated travel time.

- **Fleet Management**
  - Each buggy has an ID, passenger capacity, current location, availability status and speed.
  - A buggy is considered for assignment only when it is available and its capacity is sufficient for the request.
  - A buggy marked `NOT_AVAILABLE` in the input remains offline during the simulation.

- **Passenger Request Management**
  - Each request stores its ID, pickup node, destination node, passenger count, urgency and arrival time.
  - Requests are considered only after their `request_time` has been reached.
  - Assigned requests are skipped in later dispatch ticks.

- **Dispatch Strategies**
  - `shortest`: selects the feasible request/buggy pair with the lowest combined pickup ETA and passenger-trip duration.
  - `greedy`: selects the request with the highest urgency-and-age priority, using its nearest eligible buggy.
  - `regret`: adds a regret term based on the difference between the nearest and second-nearest eligible buggy ETAs to the request priority.
  - `smart`: currently uses the same regret-based selection mode as `regret`.

- **Priority Ageing**
  - Request priority is calculated using urgency and elapsed waiting time:

  \[
  P(r)=w_u\,U(r)+w_a\,\max(0,t_{\mathrm{now}}-t_{\mathrm{request}})
  \]

  - `urgency_weight` controls the contribution of urgency.
  - `aging_weight` controls the contribution of waiting time.

- **Simulation Engine**
  - The simulation advances in fixed time increments.
  - Completed trips release buggies and update their current locations.
  - Assignments are made at each simulation tick while eligible requests and buggies remain.
  - A request is not assigned if its pickup and passenger trip cannot finish within the configured simulation horizon.

- **Result Export**
  - Results can be written as JSON or CSV.
  - The summary includes total, served and unserved requests, average waiting time and average buggy utilisation.
  - Per-request output records the assigned buggy, wait time and pickup ETA.

---

## 3. System architecture & component map

### Top-level flow

1. `main.c` reads the simulation configuration and determines the data directory.
2. The graph module loads campus nodes and edges from JSON files.
3. The dispatch module loads buggy fleet information and passenger requests.
4. The simulation initialises each buggy's availability, busy time and destination state.
5. At each simulation tick, completed trips are released and eligible requests are considered.
6. The dispatch module checks buggy availability, passenger capacity and route reachability.
7. Dijkstra's algorithm calculates pickup routes and passenger trip routes.
8. The selected dispatch strategy chooses a request and buggy.
9. The simulation updates buggy state and records request outcomes.
10. The output module writes the final summary and per-request results in the configured format.

### Component map

```text
                 +----------------------+
                 |       main.c         |
                 | Simulation control   |
                 +----------+-----------+
                            |
              +-------------+-------------+
              |                           |
    +---------v----------+      +---------v----------+
    | GraphRouting       |      | Dispatch           |
    | graph.h / graph.c  |<---->| dispatch.h/.c      |
    | Graph + Dijkstra   |      | Fleet + Requests   |
    +--------------------+      | Dispatch policies  |
                                +--------------------+
                                           |
                                +----------v-----------+
                                | Simulation state     |
                                | Time, buggy status,  |
                                | assignments, metrics |
                                +----------+-----------+
                                           |
                                +----------v-----------+
                                | Output               |
                                | outputwriter.h/.c    |
                                | JSON / CSV results   |
                                +----------------------+

       lib/cJSON.c and lib/cJSON.h support JSON parsing/output.
```

### Performance and algorithm notes

- The graph uses an adjacency-list representation.
- Dijkstra's algorithm is used with non-negative edge weights.
- Dispatch decisions are evaluated at each configured simulation time step.
- Request priority is based on urgency and elapsed waiting time; `regret`/`smart` add a term based on alternative buggy ETAs.
- The simulator uses static edge travel-time values supplied in the input files. It does not query live traffic or GPS services.

---

## 4. Data formats & JSON schemas

The program expects five JSON files in the selected data directory:

- `simulation_config.json`
- `campus_nodes.json`
- `campus_edges.json`
- `campus_buggies.json`
- `requests.json`

The default files are stored in `Data/`. The `Data/scenarios/aging/` and `Data/scenarios/expanded/` directories contain alternative complete scenarios.

### Simulation configuration

Example:

```json
{
  "algorithm": "shortest",
  "routing": "dijkstra",
  "aging_weight": 0.5,
  "urgency_weight": 1.0,
  "time_step_s": 10,
  "max_simulation_time_s": 1800,
  "output_format": "csv"
}
```

| Field | Meaning |
|---|---|
| `algorithm` | Dispatch policy: `shortest`, `greedy`, `regret` or `smart`. |
| `routing` | Routing label recorded in the output; the implemented route calculation is Dijkstra. |
| `aging_weight` | Weight applied to elapsed waiting time in request priority. |
| `urgency_weight` | Weight applied to request urgency. |
| `time_step_s` | Number of seconds between simulation ticks. |
| `max_simulation_time_s` | Maximum simulation duration. |
| `output_format` | Result format: `json` or `csv`. |

### Campus nodes — `campus_nodes.json`

Example:

```json
{
  "nodes": [
    {
      "id": 1,
      "name": "Orion",
      "lat": 10.7597475,
      "lon": 78.8107876
    },
    {
      "id": 2,
      "name": "Logos",
      "lat": 10.7610128,
      "lon": 78.8141708
    }
  ]
}
```

Each node has a unique integer `id`, a location `name`, and latitude/longitude values. Request pickup, destination and buggy current-location fields refer to these node IDs.

### Campus roads — `campus_edges.json`

Example:

```json
{
  "edges": [
    {
      "from": 1,
      "to": 2,
      "distance_m": 494,
      "avg_time_s": 380,
      "bidirectional": true
    }
  ]
}
```

- `from`, `to`: IDs of the connected nodes.
- `distance_m`: edge distance in metres; used as the Dijkstra route weight.
- `avg_time_s`: estimated time for traversing the edge in seconds.
- `bidirectional`: if `true`, the edge is available in both directions.

### Buggy fleet — `campus_buggies.json`

Example:

```json
{
  "buggies": [
    {
      "id": "BG1",
      "capacity": 6,
      "current_location": 1,
      "status": "AVAILABLE",
      "speed_mps": 5.5
    }
  ]
}
```

- `id`: unique buggy identifier.
- `capacity`: maximum passenger capacity.
- `current_location`: current campus node ID.
- `status`: `AVAILABLE` or `NOT_AVAILABLE`.
- `speed_mps`: buggy speed field in metres per second.

### Passenger requests — `requests.json`

Example:

```json
{
  "requests": [
    {
      "id": "R1",
      "pickup": 22,
      "destination": 21,
      "passengers": 1,
      "urgency": 2,
      "request_time": 13
    }
  ]
}
```

- `id`: unique request identifier.
- `pickup`: pickup node ID.
- `destination`: destination node ID.
- `passengers`: number of passengers in the request.
- `urgency`: numeric urgency value used by the priority calculation.
- `request_time`: request arrival time in simulation seconds.

**Important:** The current repository reads the `urgency` value from the JSON input. It does not automatically infer urgency from a destination or passenger category.

---

## 5. Command-line interface & example runs

The program is a command-line simulation. It does not expose HTTP endpoints or a network API.

### Default scenario

Run from the repository root:

```bash
./build/buggy_dispatch
```

The program uses the `Data/` directory by default.

### Run the ageing scenario

```bash
./build/buggy_dispatch Data/scenarios/aging
```

### Run the expanded scenario

```bash
./build/buggy_dispatch Data/scenarios/expanded
```

On Windows with a typical Visual Studio CMake generator:

```powershell
.\build\Release\buggy_dispatch.exe
.\build\Release\buggy_dispatch.exe Data/scenarios/aging
```

The scenario directory must contain all five required JSON files. The algorithm and output format are read from that directory's `simulation_config.json`; they are not separate command-line flags.

### Output

The output file is written in the selected data directory:

- `output_format: "json"` writes `results.json`.
- `output_format: "csv"` writes `results.csv`.

The console also prints assignment information and the final summary.

---

## 6. Build, run & configuration (CMake details)

### Requirements

- C compiler such as GCC or Clang.
- CMake 3.15 or later.
- Terminal / command prompt.

The project includes the cJSON source files under `lib/`, so a separate cJSON installation is not required.

### Build locally

From the repository root (the directory containing `CMakeLists.txt`):

```bash
cmake -S . -B build
cmake --build build
```

This creates the main executable target `buggy_dispatch` and the test executable target `test_all`.

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

Or execute the integration test program directly:

```bash
./build/test_all
```

### Configuration notes

Edit the `simulation_config.json` in the data directory you intend to run. For example, to compare dispatch policies, change:

```json
"algorithm": "shortest"
```

to one of:

```json
"algorithm": "greedy"
```

```json
"algorithm": "regret"
```

```json
"algorithm": "smart"
```

Keep `time_step_s` and `max_simulation_time_s` positive. The output format must be exactly `json` or `csv`.

---

## 7. Module & code walkthrough

This section describes the key source files in the repository.

- **`main.c`**  
  Program entry point and simulation coordinator. Loads configuration, graph, fleet and requests; advances the simulation clock; releases completed trips; invokes dispatch; tracks buggy busy time and request outcomes; and calls the output writer.

- **`GraphRouting/graph.h`**  
  Declares the graph-related structures (`Node`, `Edge`, `AdjNode`, `Graph`, `Route`) and public functions such as `load_graph`, `dijkstra`, `free_graph` and `free_route`.

- **`GraphRouting/graph.c`**  
  Implements graph loading, adjacency-list construction and Dijkstra's shortest-path calculation. The returned route contains node IDs, total distance and total travel time.

- **`Dispatch/dispatch.h`**  
  Declares `Buggy`, `Request` and `AssignmentResult`, along with fleet/request loading, priority calculation and dispatch function interfaces.

- **`Dispatch/dispatch.c`**  
  Loads buggy and request JSON data, computes request priority, filters unavailable or undersized buggies, finds eligible buggy ETAs and applies the selected dispatch policy.

- **`Output/outputwriter.h`**  
  Declares `RequestOutcome` and `Metrics`, plus the JSON and CSV output functions.

- **`Output/outputwriter.c`**  
  Serialises the simulation summary and per-request outcomes to JSON or CSV.

- **`lib/cJSON.h` / `lib/cJSON.c`**  
  Bundled cJSON library used for parsing input JSON and writing JSON results.

- **`tests/test_all.c`**  
  Integration test program for the main graph, dispatch and priority behaviours.

- **`Dispatch/test_dispatch.c` and `GraphRouting/test_graph.c`**  
  Additional module-level test source files present in the repository. The CMake configuration currently registers `test_all` as the CTest integration test target.

---

## 8. Project files and documentation guide

The repository is organised into the following main directories:

```text
DS-Project-master/
├── CMakeLists.txt
├── main.c
├── GraphRouting/
│   ├── graph.h
│   ├── graph.c
│   ├── test_graph.c
│   ├── Instructions.txt
│   └── Workflow.txt
├── Dispatch/
│   ├── dispatch.h
│   ├── dispatch.c
│   ├── test_dispatch.c
│   ├── stub_graph.c
│   ├── Instructions.txt
│   └── Workflow.txt
├── Output/
│   ├── outputwriter.h
│   └── outputwriter.c
├── lib/
│   ├── cJSON.h
│   └── cJSON.c
├── Data/
│   ├── campus_nodes.json
│   ├── campus_edges.json
│   ├── campus_buggies.json
│   ├── requests.json
│   ├── simulation_config.json
│   ├── data_meaning.txt
│   └── scenarios/
│       ├── aging/
│       └── expanded/
├── tests/
│   └── test_all.c
├── generate_aging_scenario.py
└── generate_scenarios.py
```

### Data field guide — [`Data/data_meaning.txt`](Data/data_meaning.txt)

**What it is:** A plain-language explanation of the fields used in the request, result and configuration JSON files.

**Use:** Refer to it before modifying input data or interpreting the generated result files.

**What's inside:**
- Meaning of request fields such as `pickup`, `destination`, `passengers`, `urgency` and `request_time`.
- Meaning of result fields such as `served`, `unserved`, `avg_wait_time_s`, `avg_buggy_utilization_pct`, `wait_time_s` and `eta_s`.
- Explanation of `urgency_weight`, `aging_weight`, `time_step_s` and `max_simulation_time_s`.

### Module workflow notes

- [`Dispatch/Instructions.txt`](Dispatch/Instructions.txt) and [`Dispatch/Workflow.txt`](Dispatch/Workflow.txt) contain dispatch-module notes.
- [`GraphRouting/Instructions.txt`](GraphRouting/Instructions.txt) and [`GraphRouting/Workflow.txt`](GraphRouting/Workflow.txt) contain graph-routing module notes.

---

## 9. Testing, validation & datasets

### Integration tests

The CMake project registers the `integration_tests` test with CTest. Run:

```bash
ctest --test-dir build --output-on-failure
```

The test source is `tests/test_all.c`. It covers key behaviours such as:

- Request priority and waiting-time ageing.
- Campus graph loading and shortest-path routing.
- Dispatch assignment.
- Passenger-capacity restrictions.
- Regret-based dispatch behaviour.

### Supplied scenarios

- **`Data/`** — default campus graph, buggy fleet, request list and simulation configuration.
- **`Data/scenarios/aging/`** — scenario configured to use the `greedy` strategy and ageing-related weights.
- **`Data/scenarios/expanded/`** — expanded test scenario with its own graph, fleet, requests and configuration.

Each scenario is self-contained and should be run by passing its directory path to the executable.

### Result interpretation

| Result field | Meaning |
|---|---|
| `total_requests` | Total number of requests loaded. |
| `served` | Requests assigned and included in the simulation's served outcomes. |
| `unserved` | Requests not served within the simulation horizon. |
| `avg_wait_time_s` | Average request-to-pickup time for served requests, including pickup ETA. |
| `avg_buggy_utilization_pct` | Busy buggy time as a percentage of the available fleet time within the simulation window. |
| `wait_time_s` | Request-specific time from request arrival until buggy pickup. |
| `eta_s` | Estimated time for the assigned buggy to reach the pickup point. |

Compare strategies using the same graph, fleet, request set and simulation horizon. A low average wait time should be considered alongside served/unserved counts and buggy utilisation.

---

## 10. Contribution guidelines & development notes

The project uses modular C programming:

- Header files (`.h`) expose function declarations and shared structures.
- Source files (`.c`) contain function definitions.
- `main.c` coordinates the modules rather than containing all routing and dispatch logic.
- Keep module interfaces consistent when changing function arguments or shared structures.
- Add or update tests when changing graph, dispatch or simulation behaviour.

Suggested development workflow:

1. Create a separate Git branch for the change.
2. Identify the module responsible for the feature.
3. Update the relevant header and source files.
4. Build the project using CMake.
5. Run the integration tests using CTest.
6. Run the default and relevant scenario inputs.
7. Review generated JSON/CSV output before committing.

Use consistent indentation, meaningful function and variable names, comments for non-obvious logic and appropriate memory cleanup.

---

## 11. Limitations, future scope, acknowledgements & references

### Current limitations

- Input data is loaded from static JSON files; there is no live request ingestion.
- Buggy positions are updated by the simulation, not received from GPS hardware.
- Road travel times are supplied in the graph data and are not updated using live traffic.
- Urgency is provided as a numeric input; it is not automatically inferred from trip purpose.
- The project is a command-line simulation and does not include a passenger-facing mobile application, operator dashboard or HTTP API.
- `smart` currently follows the same regret-based dispatch mode as `regret`.

### Future scope

- Integrate live GPS updates from campus buggies.
- Add a passenger interface for submitting pickup and destination requests.
- Build an operator dashboard for fleet monitoring.
- Update route travel times using live traffic or measured trip data.
- Derive urgency from configurable rules, such as accessibility needs or destination categories.
- Explore more advanced ride pooling and multi-stop route optimisation.
- Evaluate the system using real, anonymised campus mobility data.

### Acknowledgements

We acknowledge the course instructor and teaching assistants for their guidance in data structures, algorithms and modular programming. The project also uses the bundled cJSON library for JSON parsing and generation.

### References

1. E. W. Dijkstra, “A note on two problems in connexion with graphs,” *Numerische Mathematik*, vol. 1, pp. 269–271, 1959.
2. CMake Documentation: https://cmake.org/documentation/
3. GCC Documentation: https://gcc.gnu.org/onlinedocs/
4. cJSON repository and documentation: https://github.com/DaveGamble/cJSON
5. United Nations, Sustainable Development Goals: https://sdgs.un.org/goals
6. UNESCO, Sustainable Development Goals: https://www.unesco.org/en/sdgs

---

## 12. Contact & support

For questions, issues or contributions, contact the project team or open an issue in the project's GitHub repository.

- **Team name:** Debuggers
- **Team members:** Krishnendu, Tharuni, Vishak


---
