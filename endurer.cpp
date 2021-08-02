#include <stdio.h>

#include <algorithm>
#include <cassert>
#include <fstream>

#include "util.h"
#include "endurer.h"


Endurer::Endurer(int argc, char* argv[])
{
    parse_and_validate_args(argc, argv);

}

Endurer::~Endurer()
{
    for (auto& w : write_sets) delete w;
    for (auto& m : memories) delete m;
}

void
Endurer::parse_and_validate_args(int argc, char* argv[])
{
    int c;
    optind = 0; // global: clear previous getopt() state, if any
    opterr = 0; // global: don't explicitly warn on unrecognized args
    int n_args_parsed = 0;

    // sentinels
    mode = "";
    page_size = -1;
    cell_write_endurance = -1;
    remap_period = -1;


    // parse
    while ((c = getopt(argc, argv, "m:p:c:r:i:t:")) != -1) {
        try {
            switch (c) {
                case 'm':
                    mode = optarg;
                    std::transform(mode.begin(), mode.end(), mode.begin(),
                            ::tolower);
                    break;
                case 'p':
                    page_size = std::stol(optarg);
                    break;
                case 'c':
                    cell_write_endurance = std::stol(optarg);
                    break;
                case 'r':
                    remap_period = std::stod(optarg);
                    break;
                case 'i':
                    input_filepaths.emplace_back(optarg);
                    break;
                case 't':
                    input_time_units.emplace_back(std::stod(optarg));
                    break;
                case '?':
                    print_message_and_die("unrecognized argument");
            }
        }
        catch (...) {
            print_message_and_die("generic arg parse failure");
        }
        ++n_args_parsed;
    }

    // and validate
    // the executable itself (1) plus each arg matched w/its preceding flag (*2)
    int argc_expected = 1 + (2 * n_args_parsed);
    if (argc != argc_expected)
            print_message_and_die("each argument must be accompanied by a "
            "flag");

    if (mode != "time" and mode != "write" and mode != "lifetime")
        print_message_and_die("mode must be either 'time', 'write', or "
                "'lifetime': <-m MODE>");
    if (page_size == -1)
        print_message_and_die("must supply page size: <-p PAGE_SIZE>");
    if (cell_write_endurance == -1)
        print_message_and_die("must supply cell write endurance: <-c ENDU>");
    if (mode != "lifetime" and remap_period == -1)
        print_message_and_die("must supply remap period (in time units or "
                "write units, depending on mode): <-r PERIOD>");
    if (input_filepaths.size() == 0)
            print_message_and_die("must supply input file(s): <-i INPUT_FILE>"
            " [-i INPUT_FILE]...");
    if (input_time_units.size() == 0)
            print_message_and_die("must supply input time units (in "
            "instructions/cycles/seconds): <-t TIME_UNITS> [-t TIME_UNITS]...");
    if (input_filepaths.size() != input_time_units.size())
            print_message_and_die("must specify an indentical number of input"
            " files (-i) and input time units (-t)");


    n_nodes = input_filepaths.size();
}

void
Endurer::run()
{
    read_input_files();
    create_node_memories();

    if (mode == "write") do_sim_write();
#if 0
    else if (mode == "time") do_sim_time();
    else if (mode == "lifetime") do_sim_lifetime();
#endif
    else print_message_and_die("NYI: mode unsupported");

    print_stats();
}

void
Endurer::read_input_files()
{
    // need these b/c we pull references out for individual vector elements
    write_sets_n_pages.reserve(input_filepaths.size());
    write_sets.reserve(input_filepaths.size());

    for (size_t i = 0; i < input_filepaths.size(); ++i) {
        auto& filepath = input_filepaths[i];
        auto& write_set = write_sets[i];
        auto& write_set_n_pages = write_sets_n_pages[i];

        size_t input_file_size;

        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs.is_open()) print_message_and_die("could not open input file");

        // get the file size
        ifs.seekg(0, std::ios::end);
        input_file_size = ifs.tellg();
        ifs.seekg(0, std::ios::beg);

        if (input_file_size % sizeof(uint64_t) != 0)
                print_message_and_die("malformed input file; its size should be"
                " a multiple of %zu", sizeof(uint64_t));

        write_set_n_pages = input_file_size / sizeof(uint64_t);

        // allocate the application-side vector
        write_set = new uint64_t[write_set_n_pages];

        // copy the contents from the file
        ifs.read((char*) write_set, input_file_size);
    }
}


/*
 * For simulation purposes, create a memory that is the next-power-of-two
 * larger than the size of the write set (unless it is already a perfect power
 * of two; then, just make it that exact size).
 * For multiple nodes, the memory size used across all of them will be the
 * largest required by any individual write set.
 */
void
Endurer::create_node_memories()
{
    // find a common size for all memories (greatest of any needed)
    for (size_t i = 0; i < n_nodes; ++i) {
        auto& write_set_n_pages = write_sets_n_pages[i];

        // find the next highest power-of-two
        size_t write_set_msb_bit_pos = ((8 * sizeof(uint64_t)) - 1) -
                __builtin_clzl(write_set_n_pages);
        bool write_set_is_power_of_two =
                __builtin_popcount(write_set_n_pages) == 1;

        size_t memory_n_pages_log2 = write_set_is_power_of_two ?
                write_set_msb_bit_pos : write_set_msb_bit_pos + 1;
        size_t memory_n_pages = 1 << memory_n_pages_log2;

        this->memory_n_pages = MAX(this->memory_n_pages, memory_n_pages);
    }

    // now that we've agreed upon a standard size for all node memories,
    // allocate them.
    memories.reserve(n_nodes);
    for (size_t i = 0; i < n_nodes; ++i) {
        auto& memory = memories[i];
        memory = new mem_t[memory_n_pages];

        // zero the memory counters
        for (size_t j = 0; j < memory_n_pages; ++j) memory[j] = { 0, 0 };
    }

    // set up the PRNG and distribution here, as we range from [0, mem size)
    rand_gen.seed(RAND_SEED);
    decltype(rand_dist.param()) range(0, memory_n_pages - 1);
    rand_dist.param(range);
}

/*
 * Returns the write set that should be mapped onto the given node,
 * with respect to the current cluster-wide shift.
 */
inline uint32_t
Endurer::get_write_set_idx(uint32_t node_idx)
{
    return (node_idx + cluster_node_shift) % n_nodes;
}

/*
 * For all pages in memory:
 * 1. adds EXTRA_WRITES_PER_REMAP to total_writes.
 * 2. resets period_writes to 0.
 * 3. generates a new offset.
 */
void
Endurer::do_remap()
{
    // bump write counter for all pages in all node memories
    for (uint32_t i = 0; i < n_nodes; ++i) {
        auto& memory = memories[i];

        for (size_t j = 0; j < memory_n_pages; ++j) {
            memory[j].total_writes += EXTRA_WRITES_PER_REMAP;
            memory[j].period_writes = 0;
        }
    }

    // remap within all nodes
    for (size_t i = 0; i < n_nodes; ++i) {
        intra_node_offsets[i] = rand_dist(rand_gen);
    }
    // round-robin amongst cluster nodes
    cluster_node_shift = (cluster_node_shift + 1) % n_nodes;

    ++n_remaps;
}

/*
 * Write-triggered simulation mode.
 */
void
Endurer::do_sim_write()
{
    // resize(), not reserve! we need these to have default values (0) initially
    intra_node_offsets.resize(n_nodes);
    runtimes.resize(n_nodes);

    // outer loop: iterate through all nodes and apply write sets to them
    // until one triggers a remap
    bool should_terminate = false;
    while (!should_terminate) {

        bool should_remap = false;
        for (size_t node = 0; node < n_nodes; ++node) {
            // node-related variables
            auto& intra_node_offset = intra_node_offsets[node];
            auto& memory = memories[node];
            auto& runtime = runtimes[node];

            // write-set-related variables
            uint32_t write_set_idx = get_write_set_idx(node);
            auto& write_set = write_sets[write_set_idx];
            auto& write_set_n_pages = write_sets_n_pages[write_set_idx];
            auto& input_time = input_time_units[write_set_idx];


            // inner loop: apply page writes to individual pages
            for (size_t page = 0; page < write_set_n_pages; ++page) {
                uint64_t new_writes = write_set[page];

                size_t mem_idx = (page + intra_node_offset) % memory_n_pages;

                memory[mem_idx].period_writes += new_writes;
                memory[mem_idx].total_writes += new_writes;

                if (memory[mem_idx].period_writes >= remap_period) {
                    should_remap = true;
                }

                if (memory[mem_idx].total_writes >= cell_write_endurance) {
                    should_terminate = true;
                }
            }

            runtime += input_time;

            if (should_terminate) break;
        }

        if (should_terminate) break;
        if (should_remap) do_remap();
        ++n_iterations;

        // print progress
        if (n_iterations % 5 == 0) {
            double avg_runtime = std::accumulate(runtimes.begin(),
                    runtimes.end(), 0.0) / runtimes.size();
            printf("At %zu iterations: %zu remaps; avg. runtime %f\n",
                    n_iterations, n_remaps, avg_runtime);
        }
    }
}

/*
 * Time-triggered simulation mode.
 */
#if 0
void
Endurer::do_sim_time()
{
    double remap_timer = 0;

    // outer loop: apply write set to memory at shifted offset
    while (true) {

        bool should_terminate = false;
        // inner loop: apply page writes to individual pages
        for (size_t i = 0; i < write_set_n_pages; ++i) {
            uint64_t new_writes = write_set[i];

            size_t mem_idx = (i + curr_offset) % memory_n_pages;

            // don't need to track period writes
            memory[mem_idx].total_writes += new_writes;

            if (memory[mem_idx].total_writes >= cell_write_endurance) {
                should_terminate = true;
            }
        }

        if (should_terminate) break;

        ++n_iterations;

        remap_timer += input_time_units;

        if (remap_timer >= remap_period) {
            do_remap();
            remap_timer = 0;
        }
    }
}

/*
 * Simple lifetime estimate with no remapping.
 */
void
Endurer::do_sim_lifetime()
{
    uint64_t max_n_writes = 0;
    uint64_t total_n_writes = 0;

    for (size_t i = 0; i < write_set_n_pages; ++i) {
        if (write_set[i] > max_n_writes) max_n_writes = write_set[i];
        total_n_writes += write_set[i];
    }
    printf("most-written word in histogram had this many writes: %zu\n",
            max_n_writes);
    printf("total number of writes in histogram (sum): %zu\n", total_n_writes);

    double multiple_of_input_time = (double) cell_write_endurance /
            (double) max_n_writes;

    time_unscaled = multiple_of_input_time * input_time_units;
}
#endif


/*
 * Computes derived stats.
 */
void
Endurer::compute_stats()
{
    wss_bytes.reserve(n_nodes);
    wss_gib.reserve(n_nodes);

    uint64_t gib = (1024 * 1024 * 1024);
    time_unscaled = std::numeric_limits<double>::max();

    for (size_t i = 0; i < n_nodes; ++i) {
        auto& write_set_n_pages = write_sets_n_pages[i];

        wss_bytes[i] = write_set_n_pages * page_size;
        wss_gib[i] = (double) wss_bytes[i] / (double) gib;

        // find the min runtime across all nodes (should be only one-off)
        time_unscaled = MIN(time_unscaled, runtimes[i]);
    }

    mems_per_gib = (double) gib / (double) (memory_n_pages * page_size);

    n_iterations_per_gib = n_iterations * mems_per_gib;
    time_per_gib = time_unscaled * mems_per_gib;

    stats_final = true;
}

void
Endurer::print_stats()
{
    // compute first if we haven't yet
    if (!stats_final) compute_stats();

    printf("WSS stats:\n");
    for (size_t i = 0; i < n_nodes; ++i) {
        printf("WSS %zu: %zu pages (%zu bytes; %f GiB)\n", i,
                write_sets_n_pages[i], wss_bytes[i], wss_gib[i]);

    }

    printf("mems. per GiB: %f\n", mems_per_gib);


    if (mode == "lifetime") {
        printf("time (in instructions, cycles, or s): %f\n", time_unscaled);
    }
    else {
        printf("n. remaps: %zu\n", n_remaps);
        printf("n. iterations: %zu\n", n_iterations);
        printf("n. iterations per GiB: %f\n", n_iterations_per_gib);
        printf("time (in instructions, cycles, or s) per GiB: %f\n",
                time_per_gib);
    }
}


int
main(int argc, char* argv[])
{
    Endurer endu(argc, argv);

    endu.run();

    return 0;
}
