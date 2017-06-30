/*

	fast-ssdeep-clus : Parallel ssdeep clustering kit

	sort-clusters.cpp
	Cluster sorting program

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

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <iostream>
#include <limits>
#include <list>
#include <string>

#include <ffuzzy.hpp>

using namespace std;
using namespace ffuzzy;


// using parsed digest object with comparison is slow!
typedef struct
{
	char *str;
	unsigned long block_size;
} digest_to_sort_t;

inline bool compare_ssdeep_hashes(
	const digest_to_sort_t& d1,
	const digest_to_sort_t& d2
)
{
	if (d1.block_size < d2.block_size)
		return true;
	if (d1.block_size > d2.block_size)
		return false;
	return strcmp(d1.str, d2.str) < 0;
}

inline bool compare_ssdeep_cluster_list(
	list<digest_to_sort_t>* c1,
	list<digest_to_sort_t>* c2
)
{
	return compare_ssdeep_hashes(
		*(c1->cbegin()),
		*(c2->cbegin())
	);
}


static_assert(digest_long_unorm_t::max_natural_chars < numeric_limits<size_t>::max(),
	"digest_long_unorm_t::max_natural_chars + 1 must be in range of size_t.");


int main(int argc, char** argv)
{
	list<list<digest_to_sort_t>*> all_clusters;
	{
		char buf[digest_long_unorm_t::max_natural_chars + 1];
		size_t buflen;
		list<digest_to_sort_t>* cluster = new list<digest_to_sort_t>();
		while (fgets(buf, digest_long_unorm_t::max_natural_chars + 1, stdin))
		{
			buflen = strlen(buf);
			if (buflen)
			{
				char *bufend = buf + buflen - 1;
				if (*bufend != '\n')
				{
					fprintf(stderr, "error: buffer overflow.\n");
					return 1;
				}
				*bufend = '\0';
				if (buflen == 1)
				{
					if (cluster->size())
					{
						cluster->sort(compare_ssdeep_hashes);
						all_clusters.push_back(cluster);
						cluster = new list<digest_to_sort_t>();
					}
				}
				else
				{
					digest_to_sort_t d;
					d.str = new char[buflen];
					d.block_size = strtoul(buf, nullptr, 10);
					strcpy(d.str, buf);
					cluster->push_back(d);
				}
			}
		}
		delete cluster;
	}
	all_clusters.sort(compare_ssdeep_cluster_list);
	for (auto p : all_clusters)
	{
		for (auto q : *p)
		{
			puts(q.str);
			#if 0
			delete[] q.str;
			#endif
		}
		puts("");
		#if 0
		delete p;
		#endif
	}
	return 0;
}
