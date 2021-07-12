/*
 * Takes in a write histogram trace and performs the offline portion of
 * page-level ENDUReR.
 */
#pragma once

#include <stdint.h>
#include <unistd.h>

#include <random>
#include <string>


class Endurer {
    public:
        Endurer(int argc, char* argv[]);
        Endurer(const Endurer& e) = delete;
        Endurer& operator=(const Endurer& e) = delete;
        Endurer(Endurer&& e) = delete;
        Endurer& operator=(Endurer&& e) = delete;
        ~Endurer();

        void parse_and_validate_args(int argc, char* argv[]);
        void read_input_file();
        void create_memory();
        void do_sim_write();
        void do_sim_time();
        void do_sim_lifetime();
        void do_remap();
        void compute_stats();
        void print_stats();
        void run();

    private:
        typedef struct {
            uint64_t period_writes;
            uint64_t total_writes;
        } mem_t;

        std::string mode;
        int64_t page_size;
        int64_t cell_write_endurance;
        double remap_period;
        double input_time_units;
        std::string input_filepath;

        static constexpr uint64_t EXTRA_WRITES_PER_REMAP = 1;
        static constexpr uint64_t RAND_SEED = 8;

        uint64_t* write_set = nullptr;
        uint64_t write_set_n_pages = 0;

        mem_t* memory = nullptr;
        uint64_t memory_n_pages = 0;

        std::mt19937 rand_gen;
        std::uniform_int_distribution<uint64_t> rand_dist;

        uint64_t curr_offset = 0;
        uint64_t n_iterations = 0;
        uint64_t n_remaps = 0;

        // derived stats
        bool stats_final = false;
        uint64_t wss_pages = 0;
        uint64_t wss_bytes = 0;
        double wss_gib = 0;
        double n_iterations_per_gib = 0;
        double time_unscaled = 0;
        double time_per_gib = 0;
};
