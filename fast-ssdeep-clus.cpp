/*

	fast-ssdeep-clus : Parallel ssdeep clustering kit

	fast-ssdeep-clus.cpp
	Simple clustering program

	Copyright (C) 2017 Tsukasa OI <floss_ssdeep@irq.a4lg.com>


	Permission to use, copy, modify, and/or distribute this software for
	any purpose with or without fee is hereby granted, provided that the
	above copyright notice and this permission notice appear in all copies.

	THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
	WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
	MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
	ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
	WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
	ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
	OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/
/*
	Note:
	This program skips certain error checks.
	So, this program is not suited to accept arbitrary file.
*/

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

//#define FFUZZYPP_DEBUG
#include <ffuzzy.hpp>

using namespace std;
using namespace ffuzzy;


static constexpr const comparison_version COMPARISON_VERSION = comparison_version::latest;

#define SSDEEP_THRESHOLD  79
#define SSDEEP_THREADS    1
#define SSDEEP_PROGINTV   1


struct filesig_t
{
	digest_ra_t       ndigest;
	digest_ra_unorm_t udigest;
	atomic_size_t     cluster_no;
public:
	filesig_t(void) : ndigest(), udigest(), cluster_no(0) {}
	filesig_t& operator=(const filesig_t& other)
	{
		ndigest = other.ndigest;
		udigest = other.udigest;
		// atomic is not copy copy constructible by default.
		cluster_no.store(other.cluster_no.load());
		return *this;
	}
	filesig_t(const filesig_t& other)
	{
		*this = other;
	}
};

static inline bool sort_filesig_cluster(const filesig_t& a, const filesig_t& b)
{
	return a.cluster_no < b.cluster_no;
}


static size_t       nthreads = SSDEEP_THREADS;
static filesig_t*   filesigs;
static size_t       filesigs_size;
static atomic_flag  filesigs_wspin  = ATOMIC_FLAG_INIT;
static int          threshold = SSDEEP_THRESHOLD;

static atomic_size_t cluster_to_allocate;
static atomic_size_t progress_next;
static atomic_size_t progress_finished;


static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s SSDEEP_LIST [-t THRESHOLD] [-n THREADS] [-c COMMENT]\n",
		prog ? prog : "fast-ssdeep-clus");
	exit(1);
}

static void cluster_main(void);

static bool read_digests(set<digest_ra_unorm_t>& udigests, const char* filename)
{
	try
	{
		ifstream fin;
		string str;
		fin.open(filename, ios::in);
		if (fin.fail())
		{
			fprintf(stderr, "error: failed to open file.\n");
			return false;
		}
		while (getline(fin, str))
		{
			digest_ra_unorm_t digest(str);
			if (!digest.is_natural())
			{
				fprintf(stderr, "error: parsed digest is not natural.\n");
				return false;
			}
			udigests.insert(digest);
		}
	}
	catch (digest_parse_error)
	{
		fprintf(stderr, "error: cannot parse digest.\n");
		return false;
	}
	return true;
}

int main(int argc, char** argv)
{
	bool print_progress = true;
	int interval = SSDEEP_PROGINTV;
	char* comment;
	auto t_start = chrono::system_clock::now();
	// Read arguments
	if (argc < 2)
		usage(argv[0]);
	comment = argv[1];
	try
	{
		for (int i = 2; i < argc; i++)
		{
			size_t idx;
			char* arg;
			if (!strcmp(argv[i], "-t"))
			{
				if (++i == argc)
					usage(argv[0]);
				arg = argv[i];
				threshold = stoi(arg, &idx, 10);
				if (arg[idx])
					usage(argv[0]);
				if (threshold > 99 || threshold < 0)
					throw out_of_range("threshold is out of range");
			}
			else if (!strcmp(argv[i], "-n"))
			{
				if (++i == argc)
					usage(argv[0]);
				arg = argv[i];
				int n_threads = stoi(arg, &idx, 10);
				if (arg[idx])
					usage(argv[0]);
				if (n_threads < 1 || n_threads > numeric_limits<size_t>::max())
					throw out_of_range("number of threads is out of range");
				nthreads = n_threads;
			}
			else if (!strcmp(argv[i], "-i"))
			{
				if (++i == argc)
					usage(argv[0]);
				arg = argv[i];
				interval = stoi(arg, &idx, 10);
				if (arg[idx])
					usage(argv[0]);
				if (interval < 1)
					throw out_of_range("check interval is out of range");
			}
			else if (!strcmp(argv[i], "-c"))
			{
				if (++i == argc)
					usage(argv[0]);
				comment = argv[i];
			}
			else if (!strcmp(argv[i], "-np"))
			{
				print_progress = false;
			}
			else
			{
				usage(argv[0]);
			}
		}
	}
	catch (invalid_argument)
	{
		fprintf(stderr, "error: invalid argument is given.\n");
		return 1;
	}
	catch (out_of_range)
	{
		fprintf(stderr, "error: out of range argument is given.\n");
		return 1;
	}
	// Read file and preprocess
	{
		set<digest_ra_unorm_t> udigests;
		// Read digest file
		if (!read_digests(udigests, argv[1]))
			return 1;
		// Preprocess digest and prepare for clustering
		if (!udigests.size())
			return 0; // no clusters to make
		filesigs_size = udigests.size();
		if (size_t(filesigs_size + nthreads) < filesigs_size)
		{
			// filesigs_size + nthreads must not overflow.
			fprintf(stderr, "error: too much signatures or threads.\n");
			return 1;
		}
		filesigs = new filesig_t[filesigs_size];
		size_t i = 0;
		for (auto p = udigests.begin(); p != udigests.end(); p++, i++)
		{
			filesigs[i].udigest = *p;
			digest_ra_t::normalize(filesigs[i].ndigest, *p);
			filesigs[i].cluster_no.store(0);
		}
	}
	// Initialize multi-threading environment
	cluster_to_allocate = 1;
	progress_next       = 0;
	progress_finished   = 0;
	vector<thread> threads;
	for (size_t i = 0; i < nthreads; i++)
		threads.push_back(thread(cluster_main));
	// Wait for completion
	while (true)
	{
		size_t progress = progress_finished.load();
		if (print_progress)
		{
			auto t_current = chrono::system_clock::now();
			auto t_delta = chrono::duration_cast<chrono::seconds>(t_current - t_start);
			unsigned long long t_count = static_cast<unsigned long long>(t_delta.count());
			fprintf(stderr,
				"%5llu:%02llu:%02llu %12llu  [(threshold=%d) %s]\n",
				(unsigned long long)(t_count / 3600u),
				(unsigned long long)((t_count / 60u) % 60u),
				(unsigned long long)(t_count % 60u),
				(unsigned long long)progress,
				threshold, comment);
		}
		if (progress == filesigs_size)
			break;
		this_thread::sleep_for(chrono::seconds(interval));
	}
	// Join all threads
	for (size_t i = 0; i < nthreads; i++)
		threads[i].join();
	// Finalization
	sort(filesigs, filesigs + filesigs_size, sort_filesig_cluster);
	for (size_t i = 0, c0 = 0; i < filesigs_size; i++)
	{
		char buf[digest_ra_unorm_t::max_natural_chars];
		size_t c1 = filesigs[i].cluster_no.load();
		if (!c1)
			continue;
		if (c0 != c1 && c0)
			puts("");
		c0 = c1;
		filesigs[i].udigest.pretty_unsafe(buf);
		puts(buf);
		if (i == filesigs_size - 1)
			puts("");
	}
	if (print_progress)
	{
		auto t_current = chrono::system_clock::now();
		auto t_delta = chrono::duration_cast<chrono::seconds>(t_current - t_start);
		unsigned long long t_count = static_cast<unsigned long long>(t_delta.count());
		fprintf(stderr,
			"%5llu:%02llu:%02llu %12llu  [(threshold=%d) %s]\n",
			(unsigned long long)(t_count / 3600u),
			(unsigned long long)((t_count / 60u) % 60u),
			(unsigned long long)(t_count % 60u),
			(unsigned long long)filesigs_size,
			threshold, comment);
	}
	// Quit
	return 0;
}

static inline void lock_spin(atomic_flag& spinlock)
{
	while (spinlock.test_and_set(memory_order_acquire)) {}
}

static inline void unlock_spin(atomic_flag& spinlock)
{
	spinlock.clear(memory_order_release);
}

static void cluster(size_t idxA, size_t idxB)
{
	lock_spin(filesigs_wspin);
	filesig_t* sigA = &filesigs[idxA];
	filesig_t* sigB = &filesigs[idxB];
	size_t clusA = sigA->cluster_no.load();
	size_t clusB = sigB->cluster_no.load();
	if      (!clusA && !clusB)
	{
		size_t clusC = cluster_to_allocate++;
		sigA->cluster_no.store(clusC);
		sigB->cluster_no.store(clusC);
	}
	else if ( clusA && !clusB)
	{
		sigB->cluster_no.store(clusA);
	}
	else if (!clusA &&  clusB)
	{
		sigA->cluster_no.store(clusB);
	}
	else if (clusA != clusB)
	{
		for (size_t k = 0; k < filesigs_size; k++)
		{
			if (filesigs[k].cluster_no.load() == clusA)
				filesigs[k].cluster_no.store(clusB);
		}
	}
	unlock_spin(filesigs_wspin);
}

static void cluster_main(void)
{
	// cluster
	for (size_t idxA; (idxA = progress_next++) < filesigs_size;)
	{
		filesig_t*              sigA       = &filesigs[idxA];
		digest_blocksize_t      blocksizeA = sigA->ndigest.blocksize();
		digest_position_array_t digestA(sigA->ndigest);
		size_t idxB = idxA + 1;
		for (; idxB < filesigs_size; idxB++)
		{
			filesig_t* sigB = &filesigs[idxB];
			if (blocksizeA != sigB->ndigest.blocksize())
				break;
			// cluster
			size_t clusA = sigA->cluster_no.load();
			size_t clusB = sigB->cluster_no.load();
			if (clusB && clusA == clusB)
				continue;
			auto score = digestA.compare_near_eq<COMPARISON_VERSION>(sigB->ndigest);
			if (score > threshold)
				cluster(idxA, idxB);
		}
		if (!digest_blocksize::is_safe_to_double(blocksizeA))
			continue;
		digest_blocksize_t blocksizeB = blocksizeA * 2;
		for (; idxB < filesigs_size; idxB++)
		{
			filesig_t* sigB = &filesigs[idxB];
			if (blocksizeB != sigB->ndigest.blocksize())
				break;
			// cluster
			size_t clusA = sigA->cluster_no.load();
			size_t clusB = sigB->cluster_no.load();
			if (clusB && clusA == clusB)
				continue;
			auto score = digestA.compare_near_lt<COMPARISON_VERSION>(sigB->ndigest);
			if (score > threshold)
				cluster(idxA, idxB);
		}
		++progress_finished;
	}
}
