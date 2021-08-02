/*
 * Takes in a write histogram trace and performs the offline portion of
 * page-level ENDUReR.
 * NOTE: currently assumes a 1:1 mapping of num. nodes to num. input write sets.
 */
#pragma once

#include <stdint.h>
#include <unistd.h>

#include <random>
#include <string>
#include <vector>


class Endurer {
    public:
        Endurer(int argc, char* argv[]);
        Endurer(const Endurer& e) = delete;
        Endurer& operator=(const Endurer& e) = delete;
        Endurer(Endurer&& e) = delete;
        Endurer& operator=(Endurer&& e) = delete;
        ~Endurer();

        void parse_and_validate_args(int argc, char* argv[]);
        void read_input_files();
        void create_node_memories();
        void do_sim_write();
        void do_sim_time();
        void do_sim_lifetime();
        void do_remap();
        void compute_stats();
        void print_stats();
        void run();

    private:
        uint32_t get_write_set_idx(uint32_t node_idx);

        typedef struct {
            uint64_t period_writes;
            uint64_t total_writes;
        } mem_t;

        std::string mode;
        int64_t page_size;
        int64_t cell_write_endurance;
        double remap_period;
        std::vector<double> input_time_units;
        std::vector<std::string> input_filepaths;

        static constexpr uint64_t EXTRA_WRITES_PER_REMAP = 1;
        static constexpr uint64_t RAND_SEED = 8;

        uint32_t n_nodes = 0;
        uint32_t cluster_node_shift = 0;

        std::vector<uint64_t*> write_sets;
        std::vector<uint64_t> write_sets_n_pages;

        std::vector<mem_t*> memories;
        uint64_t memory_n_pages = 0;

        std::mt19937 rand_gen;
        std::uniform_int_distribution<uint64_t> rand_dist;

        std::vector<uint64_t> intra_node_offsets;
        std::vector<double> runtimes;
        uint64_t n_iterations = 0;
        uint64_t n_remaps = 0;

        // derived stats
        bool stats_final = false;
        std::vector<uint64_t> wss_bytes;
        std::vector<double> wss_gib;

        double mems_per_gib = 0;
        double n_iterations_per_gib = 0;
        double time_unscaled = 0;
        double time_per_gib = 0;
};
