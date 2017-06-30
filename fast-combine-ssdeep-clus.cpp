/*

	fast-ssdeep-clus : Parallel ssdeep clustering kit

	fast-combine-ssdeep-clus.cpp
	Simple combining clustering program

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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
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


static_assert(
	digest_blocksize::number_of_blockhashes < numeric_limits<size_t>::max(),
	"digest_blocksize::number_of_blockhashes must be less than max(size_t).");
static constexpr const size_t blocksize_upper = size_t(digest_blocksize::number_of_blockhashes) + 1;

static size_t       nthreads = SSDEEP_THREADS;
static filesig_t*   filesigs;
static size_t       filesigs_size1;
static size_t       filesigs_size2;
static size_t       filesigs_index2[blocksize_upper];
static atomic_flag  filesigs_wspin  = ATOMIC_FLAG_INIT;
static int          threshold = SSDEEP_THRESHOLD;

static atomic_size_t cluster_to_allocate;
static atomic_size_t progress_next;
static atomic_size_t progress_finished;


static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s SSDEEP_TO_ADD SSDEEP_ORIGINAL [-t THRESHOLD] [-n THREADS] [-c COMMENT]\n",
		prog ? prog : "fast-combine-ssdeep-clus");
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

static void construct_blocksize_index(size_t* index, size_t i0, size_t i1)
{
	if (i0 == i1)
	{
		for (size_t k = 0; k < blocksize_upper; k++)
			index[k] = i1;
		return;
	}
	unsigned p = digest_blocksize::natural_to_index(filesigs[i0].ndigest.blocksize());
	for (unsigned k = 0; k <= p; k++)
		index[k] = i0;
	for (size_t i = i0 + 1; i < i1; i++)
	{
		unsigned q = digest_blocksize::natural_to_index(filesigs[i].ndigest.blocksize());
		if (p != q)
		{
			for (unsigned k = p + 1; k <= q; k++)
				index[k] = i;
			p = q;
		}
	}
	for (unsigned k = p + 1; k <= blocksize_upper; k++)
		index[k] = i1;
}

int main(int argc, char** argv)
{
	bool print_progress = true;
	int interval = SSDEEP_PROGINTV;
	char* comment;
	auto t_start = chrono::system_clock::now();
	// Read arguments
	if (argc < 3)
		usage(argv[0]);
	comment = argv[1];
	try
	{
		for (int i = 3; i < argc; i++)
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
		set<digest_ra_unorm_t> udigests1;
		set<digest_ra_unorm_t> udigests2;
		// Read digest file
		{
			set<digest_ra_unorm_t> udigests1_tmp;
			if (!read_digests(udigests1_tmp, argv[1]))
				return 1;
			if (!read_digests(udigests2, argv[2]))
				return 1;
			// udigests1 = udigests1_tmp - udigests2
			set_difference(
				udigests1_tmp.begin(), udigests1_tmp.end(),
				udigests2.begin(), udigests2.end(),
				inserter(udigests1, udigests1.end()));
		}
		// Preprocess digest and prepare for clustering
		if (!udigests1.size())
			return 0; // no clusters to make
		filesigs_size1 = udigests1.size();
		filesigs_size2 = udigests2.size();
		// Check size overflow
		if (size_t(filesigs_size1 + filesigs_size2) < filesigs_size1)
		{
			// filesigs_size1 + filesigs_size2 (original) must not overflow.
			fprintf(stderr, "error: too much signatures to match.\n");
			return 1;
		}
		filesigs_size2 += filesigs_size1;
		if (size_t(filesigs_size2 + nthreads) < filesigs_size2)
		{
			// filesigs_size2 (combined with size1) + nthreads must not overflow.
			fprintf(stderr, "error: too much signatures or threads.\n");
			return 1;
		}
		// Construct signature database
		filesigs = new filesig_t[filesigs_size2];
		{
			size_t i = 0;
			for (auto p = udigests1.begin(); p != udigests1.end(); p++, i++)
			{
				filesigs[i].udigest = *p;
				digest_ra_t::normalize(filesigs[i].ndigest, *p);
				filesigs[i].cluster_no.store(0);
			}
			for (auto p = udigests2.begin(); p != udigests2.end(); p++, i++)
			{
				filesigs[i].udigest = *p;
				digest_ra_t::normalize(filesigs[i].ndigest, *p);
				filesigs[i].cluster_no.store(0);
			}
		}
		// Construct block size index
		construct_blocksize_index(filesigs_index2, filesigs_size1, filesigs_size2);
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
		if (progress == filesigs_size1)
			break;
		this_thread::sleep_for(chrono::seconds(interval));
	}
	// Join all threads
	for (size_t i = 0; i < nthreads; i++)
		threads[i].join();
	// Finalization
	sort(filesigs, filesigs + filesigs_size2, sort_filesig_cluster);
	for (size_t i = 0, c0 = 0; i < filesigs_size2; i++)
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
		if (i == filesigs_size2 - 1)
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
			(unsigned long long)filesigs_size1,
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
		for (size_t k = 0; k < filesigs_size2; k++)
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
	for (size_t idxA; (idxA = progress_next++) < filesigs_size1;)
	{
		filesig_t*              sigA       = &filesigs[idxA];
		digest_blocksize_t      blocksizeA = sigA->ndigest.blocksize();
		unsigned                bindexA = digest_blocksize::natural_to_index(blocksizeA);
		digest_position_array_t digestA(sigA->ndigest);
		size_t indexB0 = 0;
		size_t indexB1 = filesigs_index2[bindexA];
		size_t indexB2 = filesigs_index2[bindexA + 1];
		size_t indexB3 = 0;
		if (bindexA != 0)
		{
			indexB0 = filesigs_index2[bindexA - 1];
			for (size_t idxB = indexB0; idxB < indexB1; idxB++)
			{
				filesig_t* sigB = &filesigs[idxB];
				// cluster
				size_t clusA = sigA->cluster_no.load();
				size_t clusB = sigB->cluster_no.load();
				if (clusB && clusA == clusB)
					continue;
				auto score = digestA.compare_near_gt<COMPARISON_VERSION>(sigB->ndigest);
				if (score > threshold)
					cluster(idxA, idxB);
			}
		}
		if (true)
		{
			for (size_t idxB = indexB1; idxB < indexB2; idxB++)
			{
				filesig_t* sigB = &filesigs[idxB];
				// cluster
				size_t clusA = sigA->cluster_no.load();
				size_t clusB = sigB->cluster_no.load();
				if (clusB && clusA == clusB)
					continue;
				auto score = digestA.compare_near_eq<COMPARISON_VERSION>(sigB->ndigest);
				if (score > threshold)
					cluster(idxA, idxB);
			}
		}
		if (bindexA != digest_blocksize::number_of_blockhashes - 1)
		{
			indexB3 = filesigs_index2[bindexA + 2];
			for (size_t idxB = indexB2; idxB < indexB3; idxB++)
			{
				filesig_t* sigB = &filesigs[idxB];
				// cluster
				size_t clusA = sigA->cluster_no.load();
				size_t clusB = sigB->cluster_no.load();
				if (clusB && clusA == clusB)
					continue;
				auto score = digestA.compare_near_lt<COMPARISON_VERSION>(sigB->ndigest);
				if (score > threshold)
					cluster(idxA, idxB);
			}
		}
		++progress_finished;
	}
}
