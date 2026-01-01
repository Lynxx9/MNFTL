
However, reproducing such a platform is costly and difficult. Therefore, this project uses **FlashSim**, a widely used open-source SSD simulator, to focus on **FTL-level behavior** without OS, filesystem, or I/O scheduling interference.

---

## 2. Simulator Choice: FlashSim

FlashSim is chosen because:

- It is widely adopted in FTL-related research
- It allows precise control of NAND flash parameters
- It excludes OS-level overhead such as scheduling, interrupts, and block I/O queues

This allows the experiments to focus solely on:

- Mapping mechanisms
- Garbage collection behavior
- Valid page copy and erase overhead

---

## 3. Implemented FTLs

The following FTLs are evaluated in this project:

- **PFTL** (Page-level FTL)
- **DFTL** (Demand-based FTL)
- **BAST** (Block Associative Sector Translation)
- **FAST** (Fully Associative Sector Translation)
- **MNFTL** (proposed in the paper)

Due to incomplete algorithmic details for GFTL and SFTL in the original paper, they are not re-implemented in this project.

MNFTL is implemented based on the key design principles described in the paper:
- Concentrated mapping
- Postponed reclamation

---

## 4. NAND Flash Configuration

All experiments follow the same flash configuration as described in the original paper:

- Page size: 4 KB  
- Pages per block: 64  
- Logical blocks / Physical blocks: 1024 / 1802  
- Page read latency: 1700 µs  
- Page write latency: 3300 µs  
- Block erase latency: 5000 µs  

This ensures the simulation closely matches the behavior of real MLC NAND flash.

---

## 5. Workloads and Benchmarks

Since FlashSim does not include a filesystem or OS, benchmarks are implemented using **trace-driven workloads** to reproduce access patterns.

### 5.1 Bonnie

Simulates large sequential file access patterns such as system initialization or multimedia storage.

- Phase 1: Sequential write
- Phase 2: Sequential read

Purpose: Measure baseline performance with minimal garbage collection interference.

---

### 5.2 Postmark

Represents metadata-intensive workloads such as mail servers and databases.

- Prefill phase: Sequential write over the entire working set
- Random access phase: High ratio of random read/write with frequent overwrites

Purpose: Stress garbage collection and mapping updates.

---

### 5.3 Tiobench

Simulates concurrent and interleaved I/O access patterns.

- Writes to multiple logical blocks are interleaved
- Free pages are exhausted quickly
- Garbage collection is triggered frequently

Purpose: Evaluate FTL behavior under high-pressure workloads.

---

## 6. Arrival Model

FlashSim is event-driven and supports request queues by default.  
To align with the assumptions made in the original paper, this project uses a **synchronous arrival model**:

- A new request is issued only after the previous request completes
- Queueing delay is eliminated
- Only mapping, flash operations, and garbage collection latency are measured

This enables fair comparison across different FTL designs.

---

## 7. Program Structure

### Core Implementation
- `mn_ftl.cpp`  
  Implementation of MNFTL, including mapping management and garbage collection logic.

### Benchmark Drivers
- `run_bonnie.cpp`  
  Executes the Bonnie-style sequential workload.
- `run_postmark.cpp`  
  Executes the Postmark random overwrite workload.
- `run_tiotech.cpp`  
  Executes the Tiobench-style interleaved workload.
- `run_ufliptrace.cpp`  
  Executes trace-based workloads.

### Testing Utilities
- `run_correctness.cpp`  
  Performs basic correctness tests to verify mapping consistency and data integrity.
- `run_test.cpp`  
  General testing entry point for FTL evaluation.

---

## 8. Evaluation Metrics

The following metrics are collected:

- Average read time
- Average write time
- Average response time
- Number of valid page copies
- Block erase count

These metrics are used to evaluate both performance and flash wear characteristics.

---

## 9. Key Observations

- MNFTL slightly increases read latency due to OOB-based mapping lookup
- MNFTL significantly reduces write latency and overall response time
- Valid page copy and erase count are consistently lower in MNFTL
- Performance advantages become more pronounced as workload intensity and data volume increase

These results support the original paper’s claim that MNFTL effectively reduces garbage collection overhead.

---

## 10. Conclusion

This project demonstrates that MNFTL achieves better overall performance by reducing garbage collection cost rather than minimizing per-request mapping overhead.

By focusing on valid page copy reduction and efficient reclamation, MNFTL improves both system performance and flash endurance across a wide range of workloads.

---

## References

- Y. Kim et al., *MNFTL: An Efficient Flash Translation Layer for MLC NAND Flash Memory*
- FlashSim: https://github.com/MatiasBjorling/flashsim
