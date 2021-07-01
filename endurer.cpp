#include <stdio.h>

#include <fstream>

#include "util.h"
#include "endurer.h"


Endurer::Endurer(int argc, char* argv[])
{
    parse_and_validate_args(argc, argv);

}

Endurer::~Endurer()
{
    if (write_set != nullptr) delete write_set;
    if (memory != nullptr) delete memory;
}

void
Endurer::parse_and_validate_args(int argc, char* argv[])
{
    int c;
    optind = 0; // global: clear previous getopt() state, if any
    opterr = 0; // global: don't explicitly warn on unrecognized args

    // sentinels
    page_size = -1;
    cell_write_endurance = -1;
    remap_write_period = -1;
    input_time_units = 0;
    input_filepath = "";


    // parse
    while ((c = getopt(argc, argv, "p:c:r:i:t:")) != -1) {
        try {
            switch (c) {
                case 'p':
                    page_size = std::stol(optarg);
                    break;
                case 'c':
                    cell_write_endurance = std::stol(optarg);
                    break;
                case 'r':
                    remap_write_period = std::stol(optarg);
                    break;
                case 'i':
                    input_filepath = optarg;
                    break;
                case 't':
                    input_time_units = std::stod(optarg);
                    break;
                case '?':
                    print_message_and_die("unrecognized argument");
            }
        }
        catch (...) {
            print_message_and_die("generic arg parse failure");
        }
    }

    // and validate
    if (page_size == -1)
        print_message_and_die("must supply page size: <-p PAGE_SIZE>\n");
    if (cell_write_endurance == -1)
        print_message_and_die("must supply cell write endurance: <-c ENDU>\n");
    if (remap_write_period == -1)
        print_message_and_die("must supply remap write count period: "
                "<-r PERIOD>\n");
    if (input_filepath == "")
            print_message_and_die("must supply input file: <-i INPUT_FILE>\n");
    if (input_time_units == 0) print_message_and_die("must supply input time units "
            "(in instructions/cycles/seconds): <-t TIME_UNITS>\n");
}

void
Endurer::run()
{
    read_input_file();
    create_memory();
    do_sim();
    print_stats();
}

void
Endurer::read_input_file()
{
    size_t input_file_size;

    std::ifstream ifs(input_filepath, std::ios::binary);
    if (!ifs.is_open()) print_message_and_die("could not open input file\n");

    // get the file size
    ifs.seekg(0, std::ios::end);
    input_file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    if (input_file_size % sizeof(uint64_t) != 0)
            print_message_and_die("malformed input file; its size should be a "
            "multiple of %zu\n", sizeof(uint64_t));

    write_set_n_pages = input_file_size / sizeof(uint64_t);

    // allocate the application-side vector
    write_set = new uint64_t[write_set_n_pages];

    // copy the contents from the file
    ifs.read((char*) write_set, input_file_size);
}


/*
 * For simulation purposes, create a memory that is the next-power-of-two
 * larger than the size of the write set (unless it is already a perfect power
 * of two; then, just make it that exact size).
 */
void
Endurer::create_memory()
{
    // find the next highest power-of-two
    size_t write_set_msb_bit_pos = ((8 * sizeof(uint64_t)) - 1) -
            __builtin_clzl(write_set_n_pages);
    bool write_set_is_power_of_two = __builtin_popcount(write_set_n_pages) == 1;

    size_t memory_n_pages_log2 = write_set_is_power_of_two ?
            write_set_msb_bit_pos : write_set_msb_bit_pos + 1;
    memory_n_pages = 1 << memory_n_pages_log2;

    memory = new mem_t[memory_n_pages];

    // zero the memory counters
    for (size_t i = 0; i < memory_n_pages; ++i) memory[i] = { 0, 0 };

    // set up the PRNG and distribution here, as we range from [0, mem size)
    rand_gen.seed(RAND_SEED);
    decltype(rand_dist.param()) range(0, memory_n_pages - 1);
    rand_dist.param(range);
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
    for (size_t i = 0; i < memory_n_pages; ++i) {
        memory[i].total_writes += EXTRA_WRITES_PER_REMAP;
        memory[i].period_writes = 0;
    }

    curr_offset = rand_dist(rand_gen);

    ++n_remaps;
}

/*
 * Actually loop and perform the simulation.
 */
void
Endurer::do_sim()
{
    // outer loop: apply write set to memory at shifted offset
    while (true) {

        bool should_remap = false;
        bool should_terminate = false;
        // inner loop: apply page writes to individual pages
        for (size_t i = 0; i < write_set_n_pages; ++i) {
            uint64_t new_writes = write_set[i];

            size_t mem_idx = (i + curr_offset) % memory_n_pages;

            memory[mem_idx].period_writes += new_writes;
            memory[mem_idx].total_writes += new_writes;

            if (memory[mem_idx].period_writes >= remap_write_period) {
                should_remap = true;
            }

            if (memory[mem_idx].total_writes >= cell_write_endurance) {
                should_terminate = true;
            }
        }

        if (should_remap) do_remap();
        if (should_terminate) break;

        ++n_iterations;
    }
}

/*
 * Computes derived stats.
 */
void
Endurer::compute_stats()
{
    wss_pages = write_set_n_pages;
    wss_bytes = wss_pages * page_size;
    wss_gib = (double) wss_bytes / (double) (1024 * 1024 * 1024);

    uint64_t gib = (1024 * 1024 * 1024);
    double mems_per_gib = (double) gib / (double) (memory_n_pages * page_size);
    printf("mems. per GiB: %zf\n", mems_per_gib);

    n_iterations_per_gib = n_iterations * mems_per_gib;
    time_per_gib = input_time_units * n_iterations_per_gib;

    stats_final = true;
}

void
Endurer::print_stats()
{
    // compute first if we haven't yet
    if (!stats_final) compute_stats();

    printf("WSS: %zu pages (%zu bytes; %zf GiB)\n", wss_pages, wss_bytes,
            wss_gib);
    printf("n. remaps: %zu\n", n_remaps);
    printf("n. iterations: %zu\n", n_iterations);
    printf("n. iterations per GiB: %zf\n", n_iterations_per_gib);
    printf("time (in instructions, cycles, or s) per GiB: %zf\n", time_per_gib);
}


int
main(int argc, char* argv[])
{
    Endurer endu(argc, argv);

    endu.run();

    return 0;
}
